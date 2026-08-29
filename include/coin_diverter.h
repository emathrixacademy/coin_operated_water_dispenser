#ifndef COIN_DIVERTER_H
#define COIN_DIVERTER_H

// Coin diverter -- servo routing and the per-coin lockout window.
//
// A servo routes each identified coin: P1 and P5 to their hoppers, P10 and P20
// to the locked profit chamber.
//
// The diverter must be in position BEFORE the coin arrives. route() asserts the
// acceptor inhibit, moves the servo, and holds the inhibit for COIN_LOCKOUT_MS
// while the servo settles. Do not shorten that constant to make the machine
// feel faster -- a coin landing mid-travel jams the chute.
//
// The window is also the reason case 13 exists: power lost between crediting a
// coin and confirming its routing leaves a coin the machine cannot see. The
// reconciliation policy is in persist.h -- credit the user, assume the profit
// chamber, tag the event. Fail toward understating hopper stock.

#include "types.h"

void coin_diverter_begin();
void coin_diverter_update();

// Begin routing a coin. Asserts the acceptor inhibit and starts the servo move.
// Non-blocking -- completion is reported by is_settled().
// Ignored if a route is already in progress.
void coin_diverter_route(coin_t coin);

// True once the servo has reached position and COIN_LOCKOUT_MS has elapsed. The
// coin is considered physically committed to its chamber at this point, and
// only then may the inventory be incremented.
bool coin_diverter_is_settled();

// True while a route is in progress. The caller must not credit a further coin
// while this is true -- see scenarios.md case 14.
bool coin_diverter_is_busy();

// Which chamber a denomination routes to.
coin_dest_t coin_destination(coin_t coin);

#endif  // COIN_DIVERTER_H
