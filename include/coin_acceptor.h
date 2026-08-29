#ifndef COIN_ACCEPTOR_H
#define COIN_ACCEPTOR_H

// Coin acceptor -- pulse capture and denomination resolution.
//
// One acceptor identifies all four denominations by pulse count. The ISR on
// PIN_COIN_PULSE increments a volatile counter and returns; nothing is
// interpreted there. update() watches for the pulse train to go idle for
// COIN_PULSE_GAP_MS and only then resolves the count to a denomination.
//
// Ownership: this module owns the acceptor inhibit line (PIN_COIN_INHIBIT).
// Nothing else may drive it. Both the per-coin lockout window and every fault
// lockout route through inhibit()/uninhibit() here.

#include "types.h"

void coin_acceptor_begin();
void coin_acceptor_update();

// True for exactly one update() cycle after a coin is fully resolved. The
// caller must consume it with take_coin() in the same cycle.
bool coin_acceptor_available();

// Returns the resolved denomination and clears the pending flag. Returns
// COIN_NONE if nothing is pending.
//
// COIN_INVALID is returned for a pulse train that matched no denomination -- a
// slug, a foreign coin, or noise. The caller credits nothing. Motor noise from
// the pump or a hopper coupling into D2 shows up here, which is why trains
// longer than COIN_PULSE_MAX are discarded rather than credited.
coin_t coin_acceptor_take_coin();

// Inhibit control. Asserted during COIN_LOCKOUT_MS while the diverter travels,
// and asserted FIRST in every fault lockout -- never accept money the machine
// cannot honour.
//
// Inhibits nest by intent, not by count: a lockout asserted while the per-coin
// window is open stays asserted when that window expires. Releasing the coin
// window does not release a fault inhibit.
void coin_acceptor_inhibit();
void coin_acceptor_uninhibit();
bool coin_acceptor_is_inhibited();

// Value of a denomination in centavos. COIN_NONE and COIN_INVALID are worth 0.
money_t coin_value(coin_t coin);

#ifdef DEBUG
// Count of pulse trains discarded as invalid since boot. A number that climbs
// while the pump runs means noise on D2 -- check the star ground before
// suspecting the acceptor.
uint16_t coin_acceptor_invalid_count();
#endif

#endif  // COIN_ACCEPTOR_H
