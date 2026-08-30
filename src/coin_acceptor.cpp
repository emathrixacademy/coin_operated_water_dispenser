#include <Arduino.h>
#include "coin_acceptor.h"
#include "faults.h"

// Coin acceptor -- pulse capture and denomination resolution.
//
// The ISR does two things and returns: bump a counter, stamp the time. All
// interpretation is in update().

static volatile uint16_t s_pulses = 0;
static volatile uint32_t s_last_pulse_ms = 0;

// Two independent inhibit sources. The physical line is asserted if EITHER is
// set. Tracked separately rather than counted so that releasing the diverter
// window can never clear a fault lockout.
static bool s_window_inhibit = false;
static bool s_fault_inhibit = false;

static coin_t s_pending = COIN_NONE;
static bool s_available = false;

static uint8_t s_overmax_streak = 0;
static uint16_t s_discarded = 0;

static void coin_isr() {
  s_pulses++;
  // millis() does not advance inside an ISR, but reading it is safe and gives
  // the time the burst was last seen, which is all the gap timer needs.
  s_last_pulse_ms = millis();
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

  attachInterrupt(digitalPinToInterrupt(PIN_COIN_PULSE), coin_isr, FALLING);
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
  if (s_window_inhibit || s_fault_inhibit) {
    drain_pulses();
    return;
  }

  noInterrupts();
  const uint16_t pulses = s_pulses;
  const uint32_t last = s_last_pulse_ms;
  interrupts();

  if (pulses == 0) return;

  // Wait for the burst to go idle before interpreting it. Resolving early would
  // read a 4-pulse P20 as a P1 followed by a P10.
  if ((uint32_t)(millis() - last) < COIN_PULSE_GAP_MS) return;

  drain_pulses();

  if (pulses > COIN_PULSE_MAX) {
    // A P20 at 4 pulses is the longest legitimate train. Beyond COIN_PULSE_MAX
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
}

bool coin_acceptor_available() {
  return s_available;
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
