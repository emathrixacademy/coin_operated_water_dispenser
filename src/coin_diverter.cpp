#include <Arduino.h>
#include <Servo.h>
#include "coin_diverter.h"
#include "coin_acceptor.h"

// Milestone 2: contract only. Implementation lands in Milestone 3.
//
// Implementation notes:
//   - route() asserts the acceptor inhibit BEFORE commanding the servo.
//   - is_settled() goes true only after COIN_LOCKOUT_MS from the command, not
//     from the servo reporting position. The servo has no feedback; the timer
//     is the only thing standing between a coin and a jammed chute.
//   - persist_mark_coin_in_flight() is called before the move and cleared on
//     settle, which is what makes case 13 reconciliation possible.

static Servo s_servo;

void coin_diverter_begin() {
  s_servo.attach(PIN_DIVERTER_SERVO);
  s_servo.write(DIVERTER_ANGLE_PROFIT);
}

void coin_diverter_update() {
  // TODO(M3): drive the lockout window and settle detection.
}

void coin_diverter_route(coin_t coin) {
  (void)coin;
  // TODO(M3): inhibit, mark in-flight, move servo, start lockout timer.
}

bool coin_diverter_is_settled() {
  return true;
}

bool coin_diverter_is_busy() {
  return false;
}

coin_dest_t coin_destination(coin_t coin) {
  switch (coin) {
    case COIN_P1: return DEST_P1_HOPPER;
    case COIN_P5: return DEST_P5_HOPPER;
    // P10, P20 and anything unrecognised go to the locked chamber. Defaulting
    // to profit rather than to a hopper keeps an odd coin out of the change
    // float -- fail toward understating hopper stock.
    default:      return DEST_PROFIT;
  }
}
