#include <Arduino.h>
#include "bottle.h"

// Milestone 2: contract only. Implementation lands in Milestone 4.
//
// Implementation note: debounce both directions over BOTTLE_DEBOUNCE_MS. The
// removal edge is the one that matters -- a flicker read as a removal shuts the
// valve mid-pour and drops the user onto the Waiting screen for no reason.

void bottle_begin() {
  pinMode(PIN_BOTTLE_PROX, INPUT_PULLUP);
}

void bottle_update() {
  // TODO(M4): debounce and edge detection.
}

bool bottle_present() {
  return false;
}

bool bottle_just_placed() {
  return false;
}

bool bottle_just_removed() {
  return false;
}
