#include <Arduino.h>
#include "dispense.h"
#include "flow.h"

// Dispense -- valve control, cutoff, partial-stop handling. SPEC 4.3, 5.5.
//
// NO PRICE IS COMPUTED HERE. The target was fixed by billing from the user's
// coins before the valve opened. This module opens the valve, counts toward
// that target, and closes it. Delivered millilitres go back to billing, which
// rounds them DOWN first. Rounding lives there, not here.

static dispense_result_t s_status = DISPENSE_IDLE;

static volume_t s_target = 0;

// Millilitres delivered toward the current target, accumulated across pauses.
//
// flow_reset() zeroes the sensor's accumulator on each open, so the running
// total has to be banked here whenever the valve closes. Without that, a resume
// would restart the count and give away free water on every replaced bottle.
static volume_t s_banked = 0;

static bool s_valve_open = false;
static uint32_t s_settle_started = 0;
static bool s_settling = false;

#ifdef DEBUG
static volume_t delivered_now();
static volume_t s_logged_ml = 0;   // last 100 mL step printed
static void log_pour(const __FlashStringHelper *what) {
  Serial.print(F("[pour] "));
  Serial.print(what);
  Serial.print(F(" "));
  Serial.print(delivered_now());
  Serial.print(F("/"));
  Serial.print(s_target);
  Serial.println(F(" mL"));
}
#endif

// =========================================================================
// The single choke point for the valve output.
//
// flow_set_valve_open() is called from here and nowhere else, so the stall
// detector's idea of whether the valve is open cannot drift from the pin.
// =========================================================================
static void valve_write(bool open) {
  digitalWrite(PIN_VALVE, open ? RELAY_ON : RELAY_OFF);
  s_valve_open = open;
  flow_set_valve_open(open);
}

// Total delivered right now: what was banked before this leg, plus what the
// sensor has counted during it.
static volume_t delivered_now() {
  return s_banked + (s_valve_open || s_settling ? flow_ml() : 0);
}

// Close the valve and bank whatever the sensor counted during this leg.
static void bank_and_close() {
  const bool was_counting = s_valve_open || s_settling;
  // Output off FIRST, then bank. In timed mode the off-edge is what closes
  // the clock, so banking before it would drop the last few ms of the run.
  valve_write(false);
  if (was_counting) {
    flow_update();
    s_banked += flow_ml();
  }
  s_settling = false;
}

void dispense_begin() {
  pinMode(PIN_VALVE, OUTPUT);
  valve_write(false);
  s_status = DISPENSE_IDLE;
  s_target = 0;
  s_banked = 0;
  s_settling = false;
}

void dispense_update() {
  switch (s_status) {
    case DISPENSE_RUNNING: {
      // ---------------------------------------------------------------
      // Stall check comes FIRST.
      //
      // A stalled pour will never reach its target, so testing the target
      // first would just delay the detection by one pass. More importantly a
      // stall with the valve open is an active fault condition -- shut the
      // water off before doing anything else with it.
      // ---------------------------------------------------------------
      if (flow_is_stalled()) {
        bank_and_close();
        s_status = DISPENSE_STALLED;
        return;
      }

#ifdef DEBUG
      {
        const volume_t now_ml = delivered_now();
        if (now_ml >= s_logged_ml + 100) {
          s_logged_ml = (now_ml / 100) * 100;
          log_pour(F("pouring"));
        }
      }
#endif
      if (delivered_now() >= s_target) {
        // Target reached. Close the valve and start the settle window.
        //
        // The valve does not close instantly and a little water is still in
        // flight. Volume arriving during VALVE_CLOSE_SETTLE_MS is counted
        // toward what the user RECEIVED, so they are never charged for water
        // measured after their bottle was already full -- and never handed
        // uncounted water either.
        digitalWrite(PIN_VALVE, RELAY_OFF);
        s_valve_open = false;
        // Tell flow now, not after the settle window: in timed mode the clock
        // must stop with the pump, or the settle time is counted as water.
        // (With a sensor this only disarms stall detection early, which is
        // harmless -- the pulses of the tail are still counted.)
        flow_set_valve_open(false);
        s_settling = true;
        s_settle_started = millis();
        s_status = DISPENSE_COMPLETE;
#ifdef DEBUG
        log_pour(F("TARGET REACHED, pump OFF"));
#endif
      }
      return;
    }

    case DISPENSE_COMPLETE: {
      if (!s_settling) return;
      if ((uint32_t)(millis() - s_settle_started) < VALVE_CLOSE_SETTLE_MS) return;

      // Settle window over. Bank the tail and tell flow the valve is shut, so
      // the stall detector disarms.
      s_banked += flow_ml();
      s_settling = false;
      flow_set_valve_open(false);
      return;
    }

    case DISPENSE_PAUSED:
    case DISPENSE_STALLED:
    case DISPENSE_IDLE:
    default:
      return;
  }
}

void dispense_start(volume_t target_ml) {
  if (target_ml <= 0) return;
  if (s_status == DISPENSE_RUNNING) return;

  s_target = target_ml;
  s_banked = 0;
  s_settling = false;
  s_status = DISPENSE_RUNNING;

  flow_reset();
  valve_write(true);
#ifdef DEBUG
  s_logged_ml = 0;
  log_pour(F("START, pump ON"));
#endif
}

void dispense_pause() {
  if (s_status != DISPENSE_RUNNING) return;

  // Close immediately and hold the delivered count. The grace countdown is the
  // state machine's business, not this module's.
  bank_and_close();
  s_status = DISPENSE_PAUSED;
#ifdef DEBUG
  log_pour(F("PAUSED (no bottle), pump OFF"));
#endif
}

void dispense_resume() {
  if (s_status != DISPENSE_PAUSED) return;

  // Continue toward the ORIGINAL target from the volume already delivered.
  //
  // s_banked is kept and the sensor restarted from zero, so the pour finishes
  // at the target rather than at target + already_delivered. A resume that
  // reset the total would hand out a second full measure of free water on every
  // replaced bottle.
  flow_reset();
  valve_write(true);
  s_status = DISPENSE_RUNNING;
#ifdef DEBUG
  log_pour(F("RESUMED, pump ON"));
#endif
}

void dispense_abort() {
  if (s_status == DISPENSE_IDLE) return;

  bank_and_close();
  // Status is left at whatever it was if the pour already ended on its own --
  // a stall must stay visible as a stall so the caller raises the right fault.
  if (s_status == DISPENSE_RUNNING || s_status == DISPENSE_PAUSED) {
    s_status = DISPENSE_COMPLETE;
  }
}

dispense_result_t dispense_status() {
  return s_status;
}

volume_t dispense_delivered() {
  return delivered_now();
}

volume_t dispense_target() {
  return s_target;
}

bool dispense_valve_open() {
  return s_valve_open;
}

void dispense_clear() {
  // Called once the caller has settled the transaction against
  // dispense_delivered(). Close the valve defensively: clearing while it is
  // still open would leave water running with no state machine watching it.
  bank_and_close();
  s_status = DISPENSE_IDLE;
  s_target = 0;
  s_banked = 0;
}
