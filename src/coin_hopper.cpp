#include <Arduino.h>
#include <string.h>
#include "coin_hopper.h"
#include "change_plan.h"
#include "relay.h"
#include "persist.h"
#include "rtc.h"

// Coin hoppers -- change payout, outlet counting, jam retry.
//
// A payout is not complete because the hopper was told to run. It is complete
// when the outlet sensor counts the coins out.

static enum : uint8_t {
  HOP_IDLE = 0,
  HOP_RUNNING,   // motor on, counting
  HOP_SETTLING,  // motor off, waiting for coins still in the throat
  HOP_DONE       // COMPLETE or JAMMED, awaiting clear()
} s_phase = HOP_IDLE;

static hopper_id_t s_hopper = HOPPER_P1;
static uint16_t s_commanded = 0;   // total for this payout
static uint16_t s_counted = 0;     // total counted across all attempts
static uint8_t s_retry = 0;
static uint32_t s_attempt_ms = 0;
static uint32_t s_settle_ms = 0;
static bool s_pulse_on = false;     // HOPPER_PULSE_MODE: run line state
static uint32_t s_pulse_ms = 0;     // when it last changed
static uint8_t s_pulses = 0;        // pulses since the last coin counted
static payout_result_t s_result = PAYOUT_IDLE;

// Outlet sensor debounce, per hopper.
static uint8_t s_last_level = HIGH;
static uint32_t s_last_edge_ms = 0;

static uint8_t count_pin() {
  return (s_hopper == HOPPER_P1) ? PIN_HOPPER_P1_COUNT : PIN_HOPPER_P5_COUNT;
}

static uint8_t run_pin() {
  return (s_hopper == HOPPER_P1) ? PIN_HOPPER_P1_RUN : PIN_HOPPER_P5_RUN;
}

static coin_dest_t dest_of(hopper_id_t h) {
  return (h == HOPPER_P1) ? DEST_P1_HOPPER : DEST_P5_HOPPER;
}

static void motor(bool on) {
  relay_write(run_pin(), on);
}

static void motors_all_off() {
  relay_write(PIN_HOPPER_P1_RUN, false);
  relay_write(PIN_HOPPER_P5_RUN, false);
}

// Poll the outlet sensor. Coins leave at 5-10/sec, slow enough to poll, which
// is why neither of the two interrupts is spent here. This depends on the loop
// staying fast -- no delay() outside setup(). Unused while
// HOPPER_COUNT_BY_PULSE is 1, kept compiled for the day the sensors work.
__attribute__((unused)) static void poll_outlet() {
  const uint8_t level = digitalRead(count_pin());
  const uint32_t now = millis();

  if (level == s_last_level) return;
  // A real coin cannot follow the previous one within HOPPER_COUNT_DEBOUNCE_MS.
  // Anything faster is contact bounce, and counting bounce as coins makes the
  // machine believe it paid change it did not pay.
  if ((uint32_t)(now - s_last_edge_ms) < HOPPER_COUNT_DEBOUNCE_MS) return;

  s_last_level = level;
  s_last_edge_ms = now;
  if (level == LOW) s_counted++;  // falling edge = one coin out
}

// Terminate the payout, reconcile the inventory, and record a jam if short.
static void finish(bool jammed) {
  motors_all_off();

  // ---------------------------------------------------------------------
  // Inventory decrements by coins COUNTED, never by coins commanded.
  //
  // Done here rather than in clear() so the books are correct the instant the
  // payout ends, even if the caller is slow to clear. If the sensor did not
  // see a coin leave, the machine still owns it.
  // ---------------------------------------------------------------------
  if (s_counted > 0) {
    persist_inventory_add(dest_of(s_hopper), (int16_t)(-(int32_t)s_counted));
  }

  if (jammed) {
    // Record what was actually paid, so the technician clearing the jam knows
    // how much of the user's change reached the tray.
    history_entry_t e;
    memset(&e, 0, sizeof(e));
    e.timestamp = rtc_timestamp();   // RTC epoch, or RTC_TIMESTAMP_INVALID -- never uptime
    e.tag = EVT_CHANGE_JAM;
    e.denomination = (uint8_t)((s_hopper == HOPPER_P1) ? COIN_P1 : COIN_P5);
    // Both fields are centavos (types.h), not coin counts.
    const money_t unit = (s_hopper == HOPPER_P1) ? COIN_VALUE_P1 : COIN_VALUE_P5;
    e.amount_in = (money_t)s_commanded * unit;   // commanded
    e.change_out = (money_t)s_counted * unit;    // actually counted out
    persist_history_add(&e);
  }

  s_result = jammed ? PAYOUT_JAMMED : PAYOUT_COMPLETE;
  s_phase = HOP_DONE;

#ifdef DEBUG
  Serial.print(s_hopper == HOPPER_P1 ? F("[change] P1 motor OFF, counted ")
                                     : F("[change] P5 motor OFF, counted "));
  Serial.print(s_counted);
  Serial.print(F("/"));
  Serial.print(s_commanded);
  Serial.println(jammed ? F(" -- CHANGE JAM") : F(" -- done"));
#endif
}

static void start_attempt() {
  s_attempt_ms = millis();
  s_last_level = digitalRead(count_pin());
  s_last_edge_ms = millis();
  motor(true);
  s_pulse_on = true;
  s_pulse_ms = millis();
  s_pulses = 0;
  s_phase = HOP_RUNNING;
}

// Bench check from the test console ("hop1 <s>" / "hop5 <s>"): run one hopper
// motor for a few seconds with no counting and no inventory change, to see the
// relay click and the motor turn. Idle only -- never during a payout.
static uint32_t s_manual_since = 0;
static uint32_t s_manual_ms = 0;
static uint8_t s_manual_pin = 0;

// Pulse timing, [0] = P1, [1] = P5. Starts at the config.h values; the test
// console can change it live while calibrating, so each trial needs no
// reflash. Not saved: copy the values that work back into config.h.
static uint16_t s_pulse_on_ms[2]  = { HOPPER_PULSE_ON_MS,  HOPPER_P5_PULSE_ON_MS };
static uint16_t s_pulse_off_ms[2] = { HOPPER_PULSE_OFF_MS, HOPPER_P5_PULSE_OFF_MS };

void coin_hopper_set_pulse(hopper_id_t hopper, bool on, uint16_t ms) {
  const uint8_t k = (hopper == HOPPER_P5) ? 1 : 0;
  if (ms < 50) ms = 50;
  if (ms > 10000) ms = 10000;
  if (on) s_pulse_on_ms[k] = ms; else s_pulse_off_ms[k] = ms;
}

uint16_t coin_hopper_pulse_ms(hopper_id_t hopper, bool on) {
  const uint8_t k = (hopper == HOPPER_P5) ? 1 : 0;
  return on ? s_pulse_on_ms[k] : s_pulse_off_ms[k];
}

void coin_hopper_manual(hopper_id_t hopper, uint8_t seconds) {
  if (s_phase != HOP_IDLE) return;
  motors_all_off();
  if (seconds == 0) { s_manual_ms = 0; return; }
  if (seconds > 10) seconds = 10;
  s_manual_pin = (hopper == HOPPER_P1) ? PIN_HOPPER_P1_RUN : PIN_HOPPER_P5_RUN;
  relay_write(s_manual_pin, true);
  s_manual_since = millis();
  s_manual_ms = seconds * 1000UL;
}

void coin_hopper_begin() {
  pinMode(PIN_HOPPER_P1_COUNT, INPUT_PULLUP);
  pinMode(PIN_HOPPER_P5_COUNT, INPUT_PULLUP);
  motors_all_off();
  s_phase = HOP_IDLE;
  s_result = PAYOUT_IDLE;
}

void coin_hopper_update() {
  if (s_manual_ms && (uint32_t)(millis() - s_manual_since) >= s_manual_ms) {
    relay_write(s_manual_pin, false);
    s_manual_ms = 0;
  }
  switch (s_phase) {
    case HOP_RUNNING: {
      const uint16_t before = s_counted;
#if !HOPPER_COUNT_BY_PULSE
      poll_outlet();
#endif

      if (s_counted >= s_commanded) {
        motor(false);
        s_settle_ms = millis();
        s_phase = HOP_SETTLING;
        return;
      }

#if HOPPER_PULSE_MODE
      // Our hoppers pay ONE coin per start and then stop themselves, however
      // long the run line is held (bench, 2026-10-29). So the line is pulsed:
      // on for HOPPER_PULSE_ON_MS, off for HOPPER_PULSE_OFF_MS, again and again
      // until the outlet sensor has counted them all. Every counted coin is
      // progress and re-arms the attempt timer, so the timeout means "no coin
      // for HOPPER_TIMEOUT_MS", not "too slow overall".
      if (s_counted != before) s_attempt_ms = millis();
      {
        const uint32_t in_phase = millis() - s_pulse_ms;
        const uint8_t k = (s_hopper == HOPPER_P5) ? 1 : 0;
        const uint32_t on_ms  = s_pulse_on_ms[k];
        const uint32_t off_ms = s_pulse_off_ms[k];
        if (s_pulse_on && in_phase >= on_ms) {
          motor(false);
          s_pulse_on = false;
          s_pulse_ms = millis();
#if HOPPER_COUNT_BY_PULSE
          // No working outlet sensor: one completed pulse is taken as one coin.
          // See HOPPER_COUNT_BY_PULSE for what that gives up. A counted coin is
          // progress, so the attempt timer re-arms here too -- otherwise a
          // payout longer than HOPPER_TIMEOUT_MS would stop for a needless retry.
          // The P1 hopper turns once per two pulses; see config.h.
          s_attempt_ms = millis();
          if (++s_pulses < (k ? HOPPER_P5_PULSES_PER_COIN : HOPPER_P1_PULSES_PER_COIN)) return;
          s_pulses = 0;
          s_counted++;
          if (s_counted >= s_commanded) {
            s_settle_ms = millis();
            s_phase = HOP_SETTLING;
            return;
          }
#endif
        } else if (!s_pulse_on && in_phase >= off_ms) {
          motor(true);
          s_pulse_on = true;
          s_pulse_ms = millis();
        }
      }
#else
      (void)before;
#endif

#if HOPPER_PULSE_MODE
      // A coin is counted once per pulse cycle, so the timeout must outlast one
      // full cycle or a slow (calibrated) cycle reads as a jam.
      const uint8_t kt = (s_hopper == HOPPER_P5) ? 1 : 0;
      uint32_t timeout_ms = (uint32_t)s_pulse_on_ms[kt] + s_pulse_off_ms[kt] + 1000;
      if (timeout_ms < HOPPER_TIMEOUT_MS) timeout_ms = HOPPER_TIMEOUT_MS;
#else
      const uint32_t timeout_ms = HOPPER_TIMEOUT_MS;
#endif
      if ((uint32_t)(millis() - s_attempt_ms) >= timeout_ms) {
        // Short on this attempt. Stop the motor and let the throat clear
        // before judging -- see HOP_SETTLING.
        motor(false);
        s_settle_ms = millis();
        s_phase = HOP_SETTLING;
      }
      return;
    }

    case HOP_SETTLING: {
      // A coin already in the outlet throat when the motor cut still has to
      // fall past the sensor. Declaring the count short before it lands makes
      // the machine retry a payout that actually succeeded, and overpay.
#if !HOPPER_COUNT_BY_PULSE
      poll_outlet();
#endif
      if ((uint32_t)(millis() - s_settle_ms) < HOPPER_SETTLE_MS) return;

      if (s_counted >= s_commanded) {
        finish(false);
        return;
      }

      if (s_retry >= HOPPER_RETRY_MAX) {
        finish(true);
        return;
      }

      // Retry pays the SHORTFALL only. Re-commanding the original count would
      // pay the user twice for the coins that did come out.
      s_retry++;
#ifdef DEBUG
      Serial.print(F("[change] short, retry "));
      Serial.println(s_retry);
#endif
      start_attempt();
      return;
    }

    case HOP_IDLE:
    case HOP_DONE:
    default:
      return;
  }
}

void coin_hopper_dispense(hopper_id_t hopper, uint16_t count) {
  if (s_phase != HOP_IDLE) return;
  if (count == 0) return;
  s_manual_ms = 0;   // a real payout takes over from any bench run

  s_hopper = hopper;
  s_commanded = count;
  s_counted = 0;
  s_retry = 0;
  s_result = PAYOUT_RUNNING;
#ifdef DEBUG
  Serial.print(hopper == HOPPER_P1 ? F("[change] P1 motor ON, paying ")
                                   : F("[change] P5 motor ON, paying "));
  Serial.println(count);
#endif
  start_attempt();
}

payout_result_t coin_hopper_status() {
  return s_result;
}

uint16_t coin_hopper_counted() {
  return s_counted;
}

uint16_t coin_hopper_commanded() {
  return s_commanded;
}

void coin_hopper_clear() {
  if (s_phase != HOP_DONE) return;
  s_phase = HOP_IDLE;
  s_result = PAYOUT_IDLE;
  s_commanded = 0;
  s_counted = 0;
  s_retry = 0;
}

bool coin_hopper_is_busy() {
  return s_phase == HOP_RUNNING || s_phase == HOP_SETTLING;
}

uint16_t coin_hopper_count(hopper_id_t hopper) {
  const inventory_t *inv = persist_inventory();
  if (!inv) return 0;
  return (hopper == HOPPER_P1) ? inv->p1_count : inv->p5_count;
}

bool coin_hopper_is_low() {
  return coin_hopper_count(HOPPER_P1) < HOPPER_LOW_P1 ||
         coin_hopper_count(HOPPER_P5) < HOPPER_LOW_P5;
}

bool coin_hopper_can_cover(money_t centavos) {
  uint16_t p1 = 0, p5 = 0;
  return coin_hopper_plan(centavos, &p1, &p5);
}

// Thin hardware wrapper. The arithmetic lives in change_plan(), which has no
// Arduino dependency and IS unit tested -- see change_plan.h for why. This
// function's only job is to read the two stock counts. Keep it that way.
bool coin_hopper_plan(money_t centavos, uint16_t *out_p1, uint16_t *out_p5) {
  change_plan_t plan;
  const bool ok = change_plan(centavos,
                              coin_hopper_count(HOPPER_P1),
                              coin_hopper_count(HOPPER_P5),
                              &plan);
  if (out_p1) *out_p1 = plan.p1;
  if (out_p5) *out_p5 = plan.p5;
  return ok;
}
