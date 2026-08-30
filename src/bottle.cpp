#include <Arduino.h>
#include "bottle.h"

// Bottle detection -- proximity sensor debounce. SPEC 5.5.
//
// Wired active-low with a pull-up, so a broken wire reads as "no bottle" rather
// than as a phantom bottle. The machine refusing to pour is recoverable;
// pouring onto the drip tray is not.

// Debounced state, and the raw candidate being timed.
static bool s_present = false;
static bool s_candidate = false;
static uint32_t s_candidate_since = 0;

// One-shot edge flags, cleared by their reader.
static bool s_just_placed = false;
static bool s_just_removed = false;

static bool read_raw() {
  return digitalRead(PIN_BOTTLE_PROX) == LOW;   // LOW = bottle present
}

void bottle_begin() {
  pinMode(PIN_BOTTLE_PROX, INPUT_PULLUP);

  // Seed from the pin rather than assuming absent. A bottle already sitting on
  // the platform at power-up is a real state, and the alternative is a spurious
  // "just placed" edge on the first pass.
  s_present = read_raw();
  s_candidate = s_present;
  s_candidate_since = millis();
  s_just_placed = false;
  s_just_removed = false;
}

void bottle_update() {
  const bool raw = read_raw();
  const uint32_t now = millis();

  if (raw != s_candidate) {
    // A new candidate level. Restart its clock; nothing changes yet.
    s_candidate = raw;
    s_candidate_since = now;
    return;
  }

  if (raw == s_present) return;   // candidate agrees with the settled state

  // ---------------------------------------------------------------------
  // Debounce BOTH directions over BOTTLE_DEBOUNCE_MS.
  //
  // The removal edge is the one that matters: a flicker read as a removal shuts
  // the valve mid-pour and drops the user onto the waiting screen for no
  // reason. A hand passing the sensor, splash-back off the bottle neck, or a
  // reflection off moving water all produce exactly that flicker.
  //
  // It is deliberately the SHORTEST debounce on the machine anyway, because the
  // removal grace period only starts once removal is detected -- see 5.5.
  // ---------------------------------------------------------------------
  if ((uint32_t)(now - s_candidate_since) < BOTTLE_DEBOUNCE_MS) return;

  s_present = raw;
  if (s_present) {
    s_just_placed = true;
  } else {
    s_just_removed = true;
  }
}

bool bottle_present() {
  return s_present;
}

bool bottle_just_placed() {
  const bool e = s_just_placed;
  s_just_placed = false;
  return e;
}

bool bottle_just_removed() {
  const bool e = s_just_removed;
  s_just_removed = false;
  return e;
}
