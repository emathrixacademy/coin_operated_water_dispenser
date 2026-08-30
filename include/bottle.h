#ifndef BOTTLE_H
#define BOTTLE_H

// Bottle detection -- proximity sensor debounce.
//
// Debounced over BOTTLE_DEBOUNCE_MS. A momentary flicker -- a hand passing the
// sensor, splash-back off the bottle neck, a reflection off moving water --
// must not register as the bottle being removed and interrupt a good pour.
//
// The sensor is wired active-low with a pull-up, so a broken wire reads as
// "no bottle" rather than as a phantom bottle. The machine refusing to pour is
// recoverable; pouring onto the drip tray is not.

#include "types.h"

void bottle_begin();
void bottle_update();

// Debounced state. Never returns the raw pin.
bool bottle_present();

// One-shot edge flags. CONSUMED BY READING -- each returns true once per
// debounced edge and clears itself.
//
// Consumed rather than "true for one update() cycle" so an edge cannot be
// missed if update() happens to run twice before the state machine looks. A
// missed removal edge leaves the valve open over an absent bottle.
bool bottle_just_placed();
bool bottle_just_removed();

#endif  // BOTTLE_H
