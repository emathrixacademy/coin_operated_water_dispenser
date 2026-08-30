#include <Arduino.h>
#include <Servo.h>
#include "coin_diverter.h"
#include "coin_acceptor.h"
#include "persist.h"

// Coin diverter -- servo routing and the per-coin lockout window.
//
// The window is the whole point of this module. route() asserts the acceptor
// inhibit BEFORE the servo is commanded, and holds it for COIN_LOCKOUT_MS after.
// A coin arriving in that window is rejected by the acceptor and any stray
// pulses are dropped by coin_acceptor_update() -- never queued, never credited
// late. See scenarios.md case 14.

static Servo s_servo;

static enum : uint8_t {
  DIV_IDLE = 0,
  DIV_MOVING
} s_state = DIV_IDLE;

static uint32_t s_started_ms = 0;
static coin_t s_coin = COIN_NONE;

// Three physical positions only. DEST_PROFIT_P10, DEST_PROFIT_P20 and
// DEST_PROFIT_UNKNOWN are one chamber and therefore one angle -- the
// denomination split is in the books, not in the mechanism.
static uint8_t angle_for(coin_dest_t dest) {
  switch (dest) {
    case DEST_P1_HOPPER: return DIVERTER_ANGLE_P1_HOPPER;
    case DEST_P5_HOPPER: return DIVERTER_ANGLE_P5_HOPPER;
    default:             return DIVERTER_ANGLE_PROFIT;
  }
}

void coin_diverter_begin() {
  s_servo.attach(PIN_DIVERTER_SERVO);
  s_servo.write(DIVERTER_ANGLE_PROFIT);
  s_state = DIV_IDLE;
  s_coin = COIN_NONE;
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
  switch (coin) {
    case COIN_P1:  return DEST_P1_HOPPER;
    case COIN_P5:  return DEST_P5_HOPPER;
    // SPEC 7.1: the two profit denominations are counted separately so the
    // chamber's peso value can be derived from its counts and reconciled
    // against a physical collection. They share one servo angle.
    case COIN_P10: return DEST_PROFIT_P10;
    case COIN_P20: return DEST_PROFIT_P20;
    // Anything unrecognised goes to the locked chamber and is counted in
    // neither profit counter -- see the DEST_PROFIT_UNKNOWN note in types.h.
    // Defaulting to profit rather than to a hopper keeps an odd coin out of the
    // change float: fail toward understating hopper stock.
    default:       return DEST_PROFIT_UNKNOWN;
  }
}
