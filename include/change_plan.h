#ifndef CHANGE_PLAN_H
#define CHANGE_PLAN_H

// Change payout planning -- the arithmetic only.
//
// ===========================================================================
// THIS FUNCTION DECIDES WHAT MONEY A PERSON IS HANDED.
// ===========================================================================
//
// It is deliberately a PURE FUNCTION OF THREE INTEGERS with NO Arduino
// dependency, so it links into the host-side unit tests and is checked there
// rather than only being observable by standing in front of a machine feeding
// it coins.
//
// The hardware wrapper is coin_hopper_plan(), which does nothing but read the
// two stock counts out of the inventory and call this. Keep it that way. If
// stock lookup, EEPROM access or fault raising creeps in here, the tests stop
// building and the change arithmetic stops being tested.
//
// SPEC 3.4 -- largest-coin-first with a hard P5 reserve.

#include "types.h"

struct change_plan_t {
  uint16_t p1;   // P1 coins to pay out
  uint16_t p5;   // P5 coins to pay out
};

// Plan a payout of `centavos` from the given stock.
//
// Returns false -- and zeroes *out -- if the amount cannot be made. A false
// return means the caller MUST NOT PROMISE THE CHANGE. It is not a suggestion
// to pay what it can: a short payout is a jam under a paying user.
//
// Fails on: a negative amount, an amount that is not a whole number of pesos
// (no sub-peso coin exists to pay it, and rounding here would create or destroy
// money at the payout), and insufficient P1 to cover what P5 cannot.
//
// Succeeds with {0, 0} for an amount of zero: nothing owed is trivially
// coverable. Callers must not command a zero-coin payout.
bool change_plan(money_t centavos, uint16_t p1_stock, uint16_t p5_stock,
                 change_plan_t *out);

#endif  // CHANGE_PLAN_H
