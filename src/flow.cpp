#include <Arduino.h>
#include "flow.h"

// Flow sensor -- pulse capture and volume accumulation. SPEC 5.1, 5.2.
//
// THE FLOW SENSOR IS A CUTOFF, NOT A CASHIER.
//
// NOTHING IN THIS FILE MAY RETURN MONEY. The target was fixed from the user's
// coins before the valve opened; this module only counts toward it.

// Written by the ISR, read by update() with interrupts briefly disabled.
static volatile uint32_t s_pulses = 0;

// Pulses already folded into s_ml. The difference against s_pulses is what is
// still to be converted.
static uint32_t s_converted = 0;

static volume_t s_ml = 0;

// Division remainder carried between calls -- SPEC 5.1.
//
// The pulse ratio does not divide cleanly and there is no float available. If
// the remainder were dropped per call, the truncation error would compound once
// per pulse: at 450 pulses/litre a 2000 mL pour is 900 pulses, and losing up to
// 2 mL on each of them would be a larger error than the sensor's own tolerance.
// Carrying it keeps the total error under one millilitre across a whole pour.
static uint32_t s_carry = 0;

static bool s_valve_open = false;

// Last time a pulse actually arrived. Stall detection measures against this,
// and it only means anything while the valve is open.
static uint32_t s_last_flow_ms = 0;

#if DISPENSE_TIMED
// Timed mode: run time of the dispense pump since the last reset, banked at
// each pump-off, plus the current run.
static uint32_t s_run_ms = 0;
static uint32_t s_on_since = 0;

static volume_t timed_ml(uint32_t run_ms) {
  // 2000 mL at even 60 s per 100 mL is 1.2e6 ms; x100 stays far below 2^32.
  return (volume_t)((run_ms * 100UL) / (uint32_t)DISPENSE_MS_PER_100ML);
}
#endif

static void flow_isr() {
  s_pulses++;
}

// Read the volatile counter safely.
//
// s_pulses is 32-bit on an 8-bit MCU, so it takes four instructions to read. A
// pulse landing mid-read corrupts the value -- half old, half new. Disabling
// interrupts for the duration is the whole reason this is a function rather
// than a bare read.
static uint32_t read_pulses() {
  noInterrupts();
  const uint32_t p = s_pulses;
  interrupts();
  return p;
}

void flow_begin() {
  pinMode(PIN_FLOW_PULSE, INPUT_PULLUP);
#if !DISPENSE_TIMED
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_PULSE), flow_isr, FALLING);
#else
  (void)flow_isr;
#endif
  flow_reset();
}

void flow_update() {
  const uint32_t now = millis();

#if DISPENSE_TIMED
  if (s_valve_open) s_ml = timed_ml(s_run_ms + (now - s_on_since));
  return;
#endif

  const uint32_t pulses = read_pulses();

  if (pulses != s_converted) {
    const uint32_t fresh = pulses - s_converted;   // wraps correctly
    s_converted = pulses;

    // fresh is bounded by the loop rate against the pulse rate -- single digits
    // in practice -- so fresh * ML_PER_PULSE_NUM cannot approach the 32-bit
    // ceiling. It is computed here rather than per pulse so the carry only has
    // to survive one division.
    const uint32_t scaled = (fresh * (uint32_t)ML_PER_PULSE_NUM) + s_carry;
    s_ml   += (volume_t)(scaled / (uint32_t)ML_PER_PULSE_DEN);
    s_carry =            scaled % (uint32_t)ML_PER_PULSE_DEN;

    s_last_flow_ms = now;
  }
}

void flow_reset() {
  noInterrupts();
  s_pulses = 0;
  interrupts();

  s_converted = 0;
  s_ml = 0;
  s_carry = 0;
  s_last_flow_ms = millis();
#if DISPENSE_TIMED
  s_run_ms = 0;
  s_on_since = millis();
#endif
}

volume_t flow_ml() {
  return s_ml;
}

uint32_t flow_pulses() {
  return read_pulses();
}

bool flow_is_stalled() {
#if DISPENSE_TIMED
  return false;   // no sensor, nothing to detect a stall with -- see config.h
#endif
  // Only meaningful while the valve is open. An idle machine with no pulses is
  // not a stall, it is a machine nobody is using.
  if (!s_valve_open) return false;
  return (uint32_t)(millis() - s_last_flow_ms) >= FLOW_STALL_TIMEOUT_MS;
}

void flow_set_valve_open(bool open) {
  // Re-arm the stall timer on the opening edge. Without this, a valve opened
  // after a long idle would be judged against the last pulse of the PREVIOUS
  // pour and trip instantly.
  //
  // This also covers the never-started case: the timer starts running the
  // moment the valve opens, so a pour where nothing arrives at all trips at
  // FLOW_STALL_TIMEOUT_MS like any other stall.
  if (open && !s_valve_open) {
    s_last_flow_ms = millis();
  }
#if DISPENSE_TIMED
  // The clock follows the pump exactly: start on the on-edge, bank on the
  // off-edge, so time with no bottle under the nozzle is never counted.
  {
    const uint32_t now = millis();
    if (open && !s_valve_open) {
      s_on_since = now;
    } else if (!open && s_valve_open) {
      s_run_ms += now - s_on_since;
      s_ml = timed_ml(s_run_ms);
    }
  }
#endif
  s_valve_open = open;
}
