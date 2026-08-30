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
// COIN_UNKNOWN is returned for a pulse train that is inside COIN_PULSE_MAX but
// matched no denomination. SPEC 3.1: that is still a real coin, so the caller
// credits it at the MINIMUM denomination and the diverter routes it to profit.
// Crediting nothing would take the user's money.
//
// COIN_INVALID is never returned by this path -- it means "not a coin".
//
// Motor noise from the pump or a hopper coupling into D2 shows up as an
// over-length train, which is why trains longer than COIN_PULSE_MAX are
// discarded rather than credited, and why a run of them raises FAULT_ACCEPTOR.
coin_t coin_acceptor_take_coin();

// --- Inhibit ------------------------------------------------------------
//
// There are TWO INDEPENDENT inhibit sources and the physical line is asserted
// if either is set. They are tracked separately, not counted, so releasing one
// can never release the other. Releasing the diverter window while a fault is
// active must not re-enable the acceptor.
//
// Fault inhibit -- asserted FIRST in every lockout, before the screen changes.
// Never accept money the machine cannot honour.
void coin_acceptor_inhibit();
void coin_acceptor_uninhibit();

// Diverter window inhibit -- asserted for COIN_LOCKOUT_MS while the servo
// travels. Owned by coin_diverter; nothing else may call these.
void coin_acceptor_window_inhibit();
void coin_acceptor_window_release();

bool coin_acceptor_is_inhibited();

// Consecutive over-COIN_PULSE_MAX trains seen. Resets on any train that
// resolves to a real denomination.
//
// A stuck acceptor output that silently swallows coins looks exactly like a
// dead acceptor from the user's side, so this is not discarded quietly forever:
// at COIN_OVERMAX_FAULT_MAX the caller raises FAULT_ACCEPTOR and the machine
// shows a service message instead of eating money in silence.
uint8_t coin_acceptor_overmax_streak();

// Count of pulse trains discarded since boot -- over-max plus unrecognised.
// A number that climbs while the pump or a hopper runs means noise on D2;
// check the star ground before suspecting the acceptor.
uint16_t coin_acceptor_discarded_count();

#endif  // COIN_ACCEPTOR_H
