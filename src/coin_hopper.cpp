#include <Arduino.h>
#include <string.h>
#include "coin_hopper.h"
#include "change_plan.h"
#include "persist.h"

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
  digitalWrite(run_pin(), on ? RELAY_ON : RELAY_OFF);
}

static void motors_all_off() {
  digitalWrite(PIN_HOPPER_P1_RUN, RELAY_OFF);
  digitalWrite(PIN_HOPPER_P5_RUN, RELAY_OFF);
}

// Poll the outlet sensor. Coins leave at 5-10/sec, slow enough to poll, which
// is why neither of the two interrupts is spent here. This depends on the loop
// staying fast -- no delay() outside setup().
static void poll_outlet() {
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
    e.timestamp = millis() / 1000UL;
    e.tag = EVT_CHANGE_JAM;
    e.denomination = (uint8_t)((s_hopper == HOPPER_P1) ? COIN_P1 : COIN_P5);
    e.amount_in = (money_t)s_commanded;   // commanded
    e.change_out = (money_t)s_counted;    // actually counted out
    persist_history_add(&e);
  }

  s_result = jammed ? PAYOUT_JAMMED : PAYOUT_COMPLETE;
  s_phase = HOP_DONE;
}

static void start_attempt() {
  s_attempt_ms = millis();
  s_last_level = digitalRead(count_pin());
  s_last_edge_ms = millis();
  motor(true);
  s_phase = HOP_RUNNING;
}

void coin_hopper_begin() {
  pinMode(PIN_HOPPER_P1_COUNT, INPUT_PULLUP);
  pinMode(PIN_HOPPER_P5_COUNT, INPUT_PULLUP);
  pinMode(PIN_HOPPER_P1_RUN, OUTPUT);
  pinMode(PIN_HOPPER_P5_RUN, OUTPUT);
  motors_all_off();
  s_phase = HOP_IDLE;
  s_result = PAYOUT_IDLE;
}

void coin_hopper_update() {
  switch (s_phase) {
    case HOP_RUNNING: {
      poll_outlet();

      if (s_counted >= s_commanded) {
        motor(false);
        s_settle_ms = millis();
        s_phase = HOP_SETTLING;
        return;
      }

      if ((uint32_t)(millis() - s_attempt_ms) >= HOPPER_TIMEOUT_MS) {
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
      poll_outlet();
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

  s_hopper = hopper;
  s_commanded = count;
  s_counted = 0;
  s_retry = 0;
  s_result = PAYOUT_RUNNING;
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
