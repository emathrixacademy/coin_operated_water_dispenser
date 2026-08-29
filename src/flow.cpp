#include <Arduino.h>
#include "flow.h"

// Milestone 2: contract only. Implementation lands in Milestone 4.
//
// Implementation notes:
//   - ISR sets a volatile counter and returns.
//   - update() converts pulses to mL as pulses * NUM / DEN, carrying the
//     remainder between calls so truncation error stays under 1 mL across a
//     pour instead of accumulating once per pulse.
//   - Read the volatile counter with interrupts briefly disabled, or a pulse
//     arriving mid-read corrupts a multi-byte value on an 8-bit MCU.
//   - Stall detection only arms while the valve is open.
//
// NOTHING IN THIS FILE MAY RETURN MONEY.

static volatile uint32_t s_pulses = 0;

static void flow_isr() {
  s_pulses++;
}

void flow_begin() {
  pinMode(PIN_FLOW_PULSE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_PULSE), flow_isr, FALLING);
}

void flow_update() {
  // TODO(M4): accumulate mL with remainder carry, run stall detection.
}

void flow_reset() {
  noInterrupts();
  s_pulses = 0;
  interrupts();
}

volume_t flow_ml() {
  return 0;
}

uint32_t flow_pulses() {
  noInterrupts();
  uint32_t p = s_pulses;
  interrupts();
  return p;
}

bool flow_is_stalled() {
  return false;
}

void flow_set_valve_open(bool open) {
  (void)open;
  // TODO(M4): arm or disarm the stall timer.
}
