#include <Arduino.h>
#include "water_level.h"

// Milestone 2: contract only. Implementation lands in Milestone 4.
//
// Implementation notes:
//   - The gallon float inhibit is checked at the point the pump output is
//     WRITTEN, not only where the decision is made. A single choke point means
//     no future edit can add a path that energises the pump without passing the
//     check. Dry running destroys the pump -- scenarios.md case 15.
//   - Debounce all three floats over FLOAT_DEBOUNCE_MS.
//   - Enforce PUMP_MIN_OFF_MS between runs and PUMP_MAX_RUN_MS as a ceiling.
//   - DS18B20 reads are asynchronous: request, then collect ~750 ms later. Never
//     block waiting for a conversion.

static void pump_write(bool want_on) {
  // Single choke point for the pump output. The safety inhibit lives here.
  const bool gallon_empty = (digitalRead(PIN_FLOAT_GALLON) == LOW);
  const bool allow = want_on && !gallon_empty;
  digitalWrite(PIN_PUMP, allow ? RELAY_ON : RELAY_OFF);
}

void water_level_begin() {
  pinMode(PIN_FLOAT_TANK_MID, INPUT_PULLUP);
  pinMode(PIN_FLOAT_TANK_HIGH, INPUT_PULLUP);
  pinMode(PIN_FLOAT_GALLON, INPUT_PULLUP);
  pinMode(PIN_PUMP, OUTPUT);
  pump_write(false);
}

void water_level_update() {
  // TODO(M4): debounce floats, run pump control through pump_write().
}

bool water_tank_below_mid() {
  return false;
}

bool water_tank_at_high() {
  return false;
}

bool water_gallon_empty() {
  return false;
}

bool water_is_locked_out() {
  return false;
}

bool water_pump_running() {
  return false;
}

bool water_pump_overrun() {
  return false;
}

int8_t water_temperature_c() {
  return TEMP_INVALID_C;
}

bool water_cooling_active() {
  return false;
}
