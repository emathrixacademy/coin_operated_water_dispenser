#ifndef COIN_ROUTE_H
#define COIN_ROUTE_H

// Where a coin goes -- the routing truth table, and nothing else.
//
// ===========================================================================
// THIS TABLE DECIDES WHICH COINS THE MACHINE BELIEVES IT CAN GIVE AS CHANGE.
// ===========================================================================
//
// No Arduino dependency by design, for the same reason as change_plan.h: a
// wrong row here puts a coin in a hopper that the books say is in the box, or
// the reverse, and that is only observable on hardware by counting coins by
// hand. Here it is a host-side unit test.
//
// The servo side lives in coin_diverter.cpp, which asks this file where a coin
// goes and then moves (or does not move) the gates to match.

#include "types.h"

// Destination of an identified coin in the given routing mode.
//
//   COIN_ROUTING_RECIRCULATE  P1 -> P1 hopper, P5 -> P5 hopper,
//                             P10, P20, unknown -> coin box
//   COIN_ROUTING_COLLECT_ALL  every coin -> coin box, counted by denomination
//
// An unknown coin goes to the box in BOTH modes and is never counted into a
// hopper: the machine must not claim change stock it cannot verify.
coin_dest_t coin_route(uint8_t mode, coin_t coin);

// Where to COUNT a coin whose routing was interrupted by a power cut -- SPEC
// 3.3. Always the coin box, never a hopper, whatever it was headed for:
// overstating hopper stock makes the machine promise change it does not hold.
//
// The denomination is known even though the location is assumed, so the coin
// is counted under its own denomination. That keeps the box's peso value
// derivable and makes the reconciliation entry legible: "one P5, assumed in the
// box" rather than "one coin of unknown value".
coin_dest_t coin_route_unrouted(coin_t coin);

// True if `dest` is one of the two change hoppers.
bool coin_dest_is_hopper(coin_dest_t dest);

#endif  // COIN_ROUTE_H
