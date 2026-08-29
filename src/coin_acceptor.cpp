#include <Arduino.h>
#include "coin_acceptor.h"

// Milestone 2: contract only. Implementation lands in Milestone 3.
//
// Implementation notes carried forward so they are not rediscovered:
//   - ISR on PIN_COIN_PULSE sets a volatile counter and returns. Nothing else.
//   - update() waits for COIN_PULSE_GAP_MS of idle before resolving the train.
//   - Trains longer than COIN_PULSE_MAX are discarded as noise, not credited.
//   - Fault inhibits and the per-coin lockout are tracked separately, so
//     releasing the coin window cannot release a fault lockout.

static volatile uint16_t s_pulses = 0;
static bool s_coin_inhibit = false;
static bool s_fault_inhibit = false;

#ifdef DEBUG
static uint16_t s_invalid_count = 0;
#endif

static void coin_isr() {
  s_pulses++;
}

void coin_acceptor_begin() {
  pinMode(PIN_COIN_PULSE, INPUT_PULLUP);
  pinMode(PIN_COIN_INHIBIT, OUTPUT);
  digitalWrite(PIN_COIN_INHIBIT, HIGH);  // inhibited until the machine is ready
  attachInterrupt(digitalPinToInterrupt(PIN_COIN_PULSE), coin_isr, FALLING);
  (void)s_pulses;
}

void coin_acceptor_update() {
  // TODO(M3): resolve pulse trains, apply the lockout window.
}

bool coin_acceptor_available() {
  return false;
}

coin_t coin_acceptor_take_coin() {
  return COIN_NONE;
}

void coin_acceptor_inhibit() {
  s_fault_inhibit = true;
  digitalWrite(PIN_COIN_INHIBIT, HIGH);
}

void coin_acceptor_uninhibit() {
  s_fault_inhibit = false;
  if (!s_coin_inhibit) digitalWrite(PIN_COIN_INHIBIT, LOW);
}

bool coin_acceptor_is_inhibited() {
  return s_coin_inhibit || s_fault_inhibit;
}

money_t coin_value(coin_t coin) {
  switch (coin) {
    case COIN_P1:  return COIN_VALUE_P1;
    case COIN_P5:  return COIN_VALUE_P5;
    case COIN_P10: return COIN_VALUE_P10;
    case COIN_P20: return COIN_VALUE_P20;
    default:       return 0;
  }
}

#ifdef DEBUG
uint16_t coin_acceptor_invalid_count() {
  return s_invalid_count;
}
#endif
