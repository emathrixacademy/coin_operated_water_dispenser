#include <Arduino.h>
#include "coin_acceptor.h"
#include "faults.h"

// Coin acceptor -- pulse capture and denomination resolution.
//
// The ISR does two things and returns: bump a counter, stamp the time. All
// interpretation is in update().

static volatile uint16_t s_pulses = 0;
static volatile uint32_t s_last_pulse_ms = 0;
// When the current train began. Gives the train's average pulse period, which
// sets the adaptive end-of-train gap -- see COIN_PULSE_GAP_FACTOR.
static volatile uint32_t s_first_pulse_ms = 0;

// Learned average pulse period of this coinslot, ms. 0 until a multi-pulse
// train of a real denomination has been seen.
static uint16_t s_period_ms = 0;

// Two independent inhibit sources. The physical line is asserted if EITHER is
// set. Tracked separately rather than counted so that releasing the diverter
// window can never clear a fault lockout.
static bool s_window_inhibit = false;
static bool s_fault_inhibit = false;

static coin_t s_pending = COIN_NONE;
static bool s_available = false;

static uint8_t s_overmax_streak = 0;
static uint16_t s_discarded = 0;

// Pulse-width filter (bench 2026-10-29: credit was climbing with no coin
// inserted -- noise on the coin line counted as P1 pulses). The coinslot's
// real pulses are tens of milliseconds long; a spike from a motor or relay is
// microseconds. So the ISR times each LOW pulse and counts only those between
// COIN_PULSE_MIN_US and COIN_PULSE_MAX_US. Still just timestamps and a counter.
static volatile uint32_t s_fall_us = 0;
static volatile bool s_low = false;
static volatile uint32_t s_last_width_us = 0;   // diagnostics
static volatile uint16_t s_rejected = 0;        // diagnostics
// Longest LOW seen since the last status read, accepted or not: shows what
// the coinslot really sends when every pulse is being rejected.
static volatile uint32_t s_longest_us = 0;      // diagnostics

// Bench 2026-10-30: in the cabinet the line chatters HIGH for microseconds in
// the middle of a real pulse. Timing only the last LOW stretch cut each pulse
// into fragments shorter than COIN_PULSE_MIN_US, so a P5 counted 3 pulses or
// 1. A HIGH shorter than COIN_PULSE_MERGE_US is now chatter inside the same
// pulse; the pulse ends only once the line has stayed HIGH that long.
static volatile uint32_t s_rise_us = 0;
static volatile bool s_rise_pending = false;   // HIGH, not yet long enough to end the pulse

#ifdef DEBUG
// Bench: every LOW over 1 ms, accepted or not, printed from update(). Shows
// the coinslot's real waveform when coins are misread.
#define PULSE_LOG_N 16
static volatile uint32_t s_plog_w[PULSE_LOG_N];
static volatile uint32_t s_plog_at[PULSE_LOG_N];
static volatile uint8_t s_plog_head = 0, s_plog_tail = 0;
#endif

// Interrupts off (called from the ISR, or from update() with them disabled).
static void commit_pulse(uint32_t w) {
#ifdef DEBUG
  if (w >= 1000UL && w < 2000000UL) {
    const uint8_t n = (uint8_t)((s_plog_head + 1) % PULSE_LOG_N);
    if (n != s_plog_tail) {
      s_plog_w[s_plog_head] = w;
      s_plog_at[s_plog_head] = millis();
      s_plog_head = n;
    }
  }
#endif
  s_last_width_us = w;
  if (w > s_longest_us) s_longest_us = w;
  if (w < COIN_PULSE_MIN_US || w > COIN_PULSE_MAX_US) {
    if (s_rejected < 0xFFFF) s_rejected++;
    return;
  }
  if (s_pulses == 0) s_first_pulse_ms = millis();
  s_pulses++;
  // millis() does not advance inside an ISR, but reading it is safe and gives
  // the time the burst was last seen, which is all the gap timer needs.
  s_last_pulse_ms = millis();
}

static void coin_isr() {
  const uint32_t now_us = micros();
  if (digitalRead(PIN_COIN_PULSE) == LOW) {
    if (s_rise_pending && (uint32_t)(now_us - s_rise_us) < COIN_PULSE_MERGE_US) {
      s_rise_pending = false;   // chatter: the same pulse goes on
      s_low = true;
      return;
    }
    if (s_rise_pending) commit_pulse(s_rise_us - s_fall_us);
    s_rise_pending = false;
    s_fall_us = now_us;
    s_low = true;
    return;
  }
  if (!s_low) return;   // a rise with no fall before it is half a glitch
  s_low = false;
  s_rise_us = now_us;
  s_rise_pending = true;
}

// Ends a pulse whose line has been HIGH for COIN_PULSE_MERGE_US.
static void settle_pulse() {
  noInterrupts();
  if (s_rise_pending && (uint32_t)(micros() - s_rise_us) >= COIN_PULSE_MERGE_US) {
    s_rise_pending = false;
    commit_pulse(s_rise_us - s_fall_us);
  }
  interrupts();
}

uint32_t coin_acceptor_last_width_us() { return s_last_width_us; }
uint16_t coin_acceptor_rejected_pulses() { return s_rejected; }

uint32_t coin_acceptor_take_longest_us() {
  noInterrupts();
  const uint32_t w = s_longest_us;
  s_longest_us = 0;
  interrupts();
  return w;
}

// Single choke point for the inhibit line. Every path that changes an inhibit
// flag goes through here, so the pin can never disagree with the flags.
static void apply_inhibit() {
  digitalWrite(PIN_COIN_INHIBIT, (s_window_inhibit || s_fault_inhibit) ? HIGH : LOW);
}

// Discard whatever is in the pulse counter without crediting it.
static void drain_pulses() {
  noInterrupts();
  s_pulses = 0;
  interrupts();
}

// How long the pulse line must be idle before a train is complete.
static uint32_t gap_ms() {
  if (s_period_ms == 0) return COIN_PULSE_GAP_MS;
  uint32_t g = (uint32_t)s_period_ms * COIN_PULSE_GAP_FACTOR;
  if (g < COIN_PULSE_GAP_MIN_MS) g = COIN_PULSE_GAP_MIN_MS;
  if (g > COIN_PULSE_GAP_MS) g = COIN_PULSE_GAP_MS;
  return g;
}

static coin_t resolve(uint16_t pulses) {
  switch (pulses) {
    case COIN_PULSES_P1:  return COIN_P1;
    case COIN_PULSES_P5:  return COIN_P5;
    case COIN_PULSES_P10: return COIN_P10;
    case COIN_PULSES_P20: return COIN_P20;
    // SPEC 3.1: an unmapped count that is still inside COIN_PULSE_MAX is a real
    // coin the acceptor accepted but this firmware does not have a mapping for.
    // Credit the minimum, route to profit. Discarding it -- which is what this
    // did before -- takes the user's coin and gives them nothing.
    default:              return COIN_UNKNOWN;
  }
}

void coin_acceptor_begin() {
  pinMode(PIN_COIN_PULSE, INPUT_PULLUP);
  pinMode(PIN_COIN_INHIBIT, OUTPUT);

  // Inhibited until the machine decides otherwise. Booting into an accepting
  // state would take a coin before the inventory has even been validated.
  s_fault_inhibit = true;
  s_window_inhibit = false;
  apply_inhibit();

  attachInterrupt(digitalPinToInterrupt(PIN_COIN_PULSE), coin_isr, CHANGE);
}

void coin_acceptor_update() {
  // ---------------------------------------------------------------------
  // REJECT, NEVER QUEUE.
  //
  // While inhibited the acceptor should be rejecting coins mechanically to the
  // return tray, so no pulses should arrive at all. If any do -- a coin already
  // in flight when the inhibit asserted, or noise -- they are dropped on every
  // pass and never accumulate.
  //
  // This is what makes scenarios.md case 14 hold. If the counter were merely
  // left alone during the window, the pulses would still be sitting there when
  // the window closed and would then resolve into a phantom coin, or worse,
  // merge with the next real coin's train and credit the wrong denomination.
  // ---------------------------------------------------------------------
  settle_pulse();
#ifdef DEBUG
  while (s_plog_tail != s_plog_head) {
    noInterrupts();
    const uint32_t w = s_plog_w[s_plog_tail], at = s_plog_at[s_plog_tail];
    s_plog_tail = (uint8_t)((s_plog_tail + 1) % PULSE_LOG_N);
    interrupts();
    Serial.print(F("[pulse] t="));
    Serial.print(at);
    Serial.print(F("ms low="));
    Serial.print(w / 1000UL);
    Serial.print(F("ms"));
    Serial.println((w >= COIN_PULSE_MIN_US && w <= COIN_PULSE_MAX_US) ? F(" ok") : F(" REJECTED"));
  }
#endif
  if (s_window_inhibit || s_fault_inhibit) {
    drain_pulses();
    return;
  }

  noInterrupts();
  const uint16_t pulses = s_pulses;
  const uint32_t last = s_last_pulse_ms;
  const uint32_t first = s_first_pulse_ms;
  interrupts();

  if (pulses == 0) return;

  // Wait for the burst to go idle before interpreting it. Resolving early would
  // read a 20-pulse P20 as a P10 followed by a P10.
  if ((uint32_t)(millis() - last) < gap_ms()) return;

  // Remove only the pulses just read. A pulse that landed after the snapshot
  // is the start of the next coin; zeroing it would read that coin short.
  noInterrupts();
  s_pulses -= pulses;
  interrupts();

#ifdef DEBUG
  // Coin timing for bench calibration. "train" is first pulse to last; "ready"
  // is first pulse to now, i.e. how long after the coin started reporting the
  // gates can begin to move. Compare against how long the coin takes to reach
  // gate 1.
  {
    const uint32_t now = millis();
    Serial.print(F("[coin] pulses="));
    Serial.print(pulses);
    Serial.print(F(" train="));
    Serial.print(last - first);
    Serial.print(F("ms"));
    if (pulses > 1) {
      Serial.print(F(" avg_period="));
      Serial.print((last - first) / (uint32_t)(pulses - 1));
      Serial.print(F("ms"));
    }
    Serial.print(F(" ready="));
    Serial.print(now - first);
    Serial.print(F("ms gap="));
    Serial.print(gap_ms());
    Serial.println(F("ms"));
  }
#endif

  if (pulses > COIN_PULSE_MAX) {
    // A P20 at COIN_PULSES_P20 is the longest legitimate train. Beyond COIN_PULSE_MAX
    // this is noise or a stuck output line -- credit nothing.
    s_discarded++;
    if (s_overmax_streak < 0xFF) s_overmax_streak++;

    // SPEC 3.1: do not discard forever.
    //
    // A stuck acceptor output that silently swallows every coin is
    // indistinguishable from a dead acceptor from the user's side -- they put
    // money in and nothing happens, with no fault shown and no way to know the
    // machine will never respond. Raise a service fault instead of eating money
    // in silence.
    //
    // faults_raise() inhibits this module before it returns, so the early-out
    // at the top of update() takes over from here and this cannot re-enter.
    if (s_overmax_streak >= COIN_OVERMAX_FAULT_MAX) {
      faults_raise(FAULT_ACCEPTOR);
    }
    return;
  }

  // Every in-range train now resolves to something creditable -- resolve()
  // returns COIN_UNKNOWN rather than COIN_INVALID for an unmapped count -- so
  // reaching here at all proves the output line is alive. Clear the streak.
  s_overmax_streak = 0;
  s_pending = resolve(pulses);
  s_available = true;

  // Learn the pulse period, only from a train that was a real denomination --
  // noise and unknown counts must not teach the gap anything.
  if (pulses > 1 && s_pending != COIN_UNKNOWN) {
    const uint32_t p = (last - first) / (uint32_t)(pulses - 1);
    s_period_ms = (p > 0xFFFFUL) ? 0xFFFF : (uint16_t)p;
  }
}

bool coin_acceptor_available() {
  return s_available;
}

bool coin_acceptor_idle() {
  noInterrupts();
  const uint16_t pulses = s_pulses;
  interrupts();
  return pulses == 0 && !s_available;
}

coin_t coin_acceptor_peek() {
  return s_available ? s_pending : COIN_NONE;
}

coin_t coin_acceptor_take_coin() {
  if (!s_available) return COIN_NONE;
  s_available = false;
  const coin_t c = s_pending;
  s_pending = COIN_NONE;
  return c;
}

void coin_acceptor_inhibit() {
  s_fault_inhibit = true;
  apply_inhibit();
  // Anything mid-flight is abandoned. A lockout must not resolve into a credit.
  drain_pulses();
  s_available = false;
  s_pending = COIN_NONE;
}

void coin_acceptor_uninhibit() {
  s_fault_inhibit = false;
  apply_inhibit();
}

void coin_acceptor_window_inhibit() {
  s_window_inhibit = true;
  apply_inhibit();
}

void coin_acceptor_window_release() {
  s_window_inhibit = false;
  // Drop anything that arrived during the window before re-enabling, so the
  // release cannot hand a stale burst to the next update().
  drain_pulses();
  apply_inhibit();
}

bool coin_acceptor_is_inhibited() {
  return s_window_inhibit || s_fault_inhibit;
}

uint8_t coin_acceptor_overmax_streak() {
  return s_overmax_streak;
}

uint16_t coin_acceptor_discarded_count() {
  return s_discarded;
}
