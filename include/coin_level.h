#ifndef COIN_LEVEL_H
#define COIN_LEVEL_H

// Coin level sensors -- three IR beams, polled and debounced.
//
//   P1 hopper full   D41  IR at the top of the P1 change hopper
//   P5 hopper full   D42  IR at the top of the P5 change hopper
//   Coin box full    D29  IR at the fill line of the large coin box
//
// All three read LOW when the beam is broken (break-beam) or an object is
// detected (reflective module). A coin falling past a beam breaks it for a few
// milliseconds; only a stack that SITS at the beam for COIN_LEVEL_DEBOUNCE_MS
// counts as full, and only a beam that stays clear that long counts as not
// full again after a payout.
//
// What the flags drive:
//   - A full hopper sends that denomination to the coin box instead, so the
//     machine keeps selling instead of overflowing the hopper chute. Routing is
//     decided in coin_route.cpp; this module only reports the beams.
//   - A full coin box is FAULT_STORAGE_FULL. The machine stops taking money,
//     because there is nowhere left for a P10 or P20 to go.
//
// FAILURE DIRECTION: a broken wire reads HIGH (INPUT_PULLUP) = "not full". For
// a hopper that means coins keep going to it as before this sensor existed;
// for the box it means no lockout. Check the beams at commissioning.

#include "types.h"
#include "coin_hopper.h"   // hopper_id_t

void coin_level_begin();
void coin_level_update();

// Debounced. hopper must be HOPPER_P1 or HOPPER_P5; anything else is "full",
// so an unexpected value never routes a coin into a hopper.
bool coin_level_hopper_full(hopper_id_t hopper);

bool coin_level_box_full();

#endif  // COIN_LEVEL_H
