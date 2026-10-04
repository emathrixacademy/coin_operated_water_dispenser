#include <Arduino.h>
#include <Servo.h>
#include "coin_diverter.h"
#include "coin_acceptor.h"
#include "coin_route.h"
#include "persist.h"

// Coin diverter -- servo routing and the per-coin lockout window.
//
// The window is the whole point of this module in RECIRCULATE. route() asserts
// the acceptor inhibit BEFORE the servo is commanded, and holds it for
// COIN_LOCKOUT_MS after. A coin arriving in that window is rejected by the
// acceptor and any stray pulses are dropped by coin_acceptor_update() -- never
// queued, never credited late. See scenarios.md case 14.
//
// WHERE a coin goes is decided by coin_route(), which has no Arduino dependency
// and is unit tested. This file only moves the mechanism to match.
//
// TODO(Rev B): the mechanism below is still the Rev A single three-position
// servo. Rev B replaces it with two binary gates; that rewrite is its own work
// order and is deliberately not mixed into this one.

// A plain constant rather than #if around the code, so BOTH paths are compiled
// and type-checked in every build. The recirculation path must not rot while
// the machine runs without servos -- we are going back to it.
static const bool COLLECT_ALL = (COIN_ROUTING_MODE == COIN_ROUTING_COLLECT_ALL);

static Servo s_servo;

static enum : uint8_t {
  DIV_IDLE = 0,
  DIV_MOVING
} s_state = DIV_IDLE;

static uint32_t s_started_ms = 0;
static coin_t s_coin = COIN_NONE;

// Three physical positions only. Every box destination is one chamber and
// therefore one angle -- the denomination split is in the books, not in the
// mechanism.
static uint8_t angle_for(coin_dest_t dest) {
  switch (dest) {
    case DEST_P1_HOPPER: return DIVERTER_ANGLE_P1_HOPPER;
    case DEST_P5_HOPPER: return DIVERTER_ANGLE_P5_HOPPER;
    default:             return DIVERTER_ANGLE_PROFIT;
  }
}

void coin_diverter_begin() {
  s_state = DIV_IDLE;
  s_coin = COIN_NONE;

  // COLLECT_ALL: NO SERVO IS FITTED, so none is attached and none is ever
  // commanded. Both flaps are mechanically pinned, Gate A toward the coin box.
  // The firmware cannot tell an absent servo from a failed one; the mode flag
  // is the only thing that distinguishes them, which is why nothing here waits
  // for travel that will never happen.
  if (COLLECT_ALL) return;

  s_servo.attach(PIN_DIVERTER_SERVO);
  s_servo.write(DIVERTER_ANGLE_PROFIT);
}

void coin_diverter_update() {
  if (s_state != DIV_MOVING) return;

  if ((uint32_t)(millis() - s_started_ms) < COIN_LOCKOUT_MS) return;

  // ---------------------------------------------------------------------
  // The coin is physically committed to its chamber only now.
  //
  // The servo has no position feedback, so this timer is the only thing
  // standing between a coin and a jammed chute. DO NOT shorten
  // COIN_LOCKOUT_MS to make the machine feel faster.
  //
  // Inventory increments HERE and nowhere else -- after physical commit, never
  // at the moment the coin was credited. That ordering is what makes the case
  // 13 reconciliation possible: a power cut before this point leaves an
  // in-flight marker and no inventory change.
  // ---------------------------------------------------------------------
  persist_inventory_add(coin_destination(s_coin), +1);
  persist_clear_coin_in_flight();

  s_state = DIV_IDLE;
  s_coin = COIN_NONE;
  coin_acceptor_window_release();
}

void coin_diverter_route(coin_t coin) {
  // A route already in progress is never interrupted or queued behind. The
  // caller must not have credited a second coin while is_busy() was true.
  if (s_state != DIV_IDLE) return;
  if (coin == COIN_NONE || coin == COIN_INVALID) return;

  if (COLLECT_ALL) {
    // -------------------------------------------------------------------
    // Nothing moves, and three things follow from that. All three are
    // CONSEQUENCES of having no gate to move, not omissions:
    //
    //   1. NO LOCKOUT WINDOW. COIN_LOCKOUT_MS is the time a gate takes to
    //      travel. With the flaps pinned the chute is always in position, so
    //      the acceptor is not inhibited and coins can be fed as fast as the
    //      acceptor reads them.
    //
    //   2. NO ROUTING INTENT. The in-flight marker exists because a power cut
    //      mid-travel leaves a coin whose destination is unknown. A coin
    //      falling through a pinned chute has only one place to go, so there
    //      is nothing to reconcile and the in-flight ring is not written. The
    //      ring itself stays in the EEPROM layout for the return to
    //      RECIRCULATE.
    //
    //   3. THE COIN IS COUNTED AT ONCE, into the box, under its denomination.
    //
    // The caller writes the transaction record immediately after this returns.
    // A power cut between the two writes loses that one coin's credit while
    // the box count already includes it. RECIRCULATE has the same few
    // milliseconds of exposure between its intent write and the transaction
    // write. The hopper counts -- what change is promised against -- are not
    // touched by an inserted coin in this mode at all.
    // -------------------------------------------------------------------
    persist_inventory_add(coin_destination(coin), +1);
    return;
  }

  // Inhibit BEFORE the servo moves, not after. The gap between crediting a coin
  // and asserting the inhibit is exactly the window in which a second coin can
  // land mid-travel.
  coin_acceptor_window_inhibit();

  // Recorded before the move so a power cut mid-travel is recoverable. Cleared
  // on settle above.
  persist_mark_coin_in_flight(coin);

  s_coin = coin;
  s_servo.write(angle_for(coin_destination(coin)));
  s_started_ms = millis();
  s_state = DIV_MOVING;
}

bool coin_diverter_is_settled() {
  return s_state == DIV_IDLE;
}

bool coin_diverter_is_busy() {
  return s_state != DIV_IDLE;
}

coin_dest_t coin_destination(coin_t coin) {
  return coin_route(COIN_ROUTING_MODE, coin);
}
