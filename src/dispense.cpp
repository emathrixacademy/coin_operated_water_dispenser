#include <Arduino.h>
#include "dispense.h"
#include "flow.h"

// Milestone 2: contract only. Implementation lands in Milestone 4.
//
// Implementation notes:
//   - resume() continues toward the ORIGINAL target from the volume already
//     delivered. It does not reset flow accumulation. A resume that restarts
//     the count gives away free water on every replaced bottle.
//   - VALVE_CLOSE_SETTLE_MS of in-flight water after the target is reached is
//     counted toward what the user received, so they are never charged for
//     water measured after their bottle was full.
//   - No price is computed here. Delivered millilitres go to billing, which
//     rounds down first.

static void valve_write(bool open) {
  digitalWrite(PIN_VALVE, open ? RELAY_ON : RELAY_OFF);
  flow_set_valve_open(open);
}

void dispense_begin() {
  pinMode(PIN_VALVE, OUTPUT);
  valve_write(false);
}

void dispense_update() {
  // TODO(M4): drive the pour, watch the target, handle stall and settle.
}

void dispense_start(volume_t target_ml) {
  (void)target_ml;
  // TODO(M4)
}

void dispense_pause() {
  // TODO(M4): close the valve immediately, hold the delivered count.
}

void dispense_resume() {
  // TODO(M4): reopen toward the original target, do NOT reset delivered.
}

void dispense_abort() {
  // TODO(M4)
}

dispense_result_t dispense_status() {
  return DISPENSE_IDLE;
}

volume_t dispense_delivered() {
  return 0;
}

volume_t dispense_target() {
  return 0;
}

bool dispense_valve_open() {
  return false;
}

void dispense_clear() {
  // TODO(M4)
}
