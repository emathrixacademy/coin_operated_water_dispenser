// Billing -- coin-to-volume allocation and refund computation.
//
// COINS DETERMINE VOLUME. VOLUME NEVER DETERMINES COINS.
//
// Deliberately has NO Arduino dependency: this is the arithmetic that decides
// what a user is charged, so it is linked into the host-side unit tests and
// checked there rather than only on hardware. If you add an Arduino call to
// this file, the tests stop building and the money math stops being tested.

#include <string.h>
#include "billing.h"

static transaction_t s_txn;

money_t coin_value(coin_t coin) {
  switch (coin) {
    case COIN_P1:  return COIN_VALUE_P1;
    case COIN_P5:  return COIN_VALUE_P5;
    case COIN_P10: return COIN_VALUE_P10;
    case COIN_P20: return COIN_VALUE_P20;
    // SPEC 3.1: an in-range train that matched no denomination is a real coin
    // of unknown value. Credit the MINIMUM. Guessing high would hand out change
    // the user never paid for; guessing low costs them at most the difference
    // and only ever happens on a miscalibrated acceptor, which is a fault to
    // fix rather than a rate to tune.
    case COIN_UNKNOWN: return COIN_VALUE_P1;
    default:       return 0;  // COIN_NONE, COIN_INVALID
  }
}

void billing_begin() {
  billing_reset();
}

void billing_add_coin(coin_t coin) {
  const money_t v = coin_value(coin);
  if (v <= 0) return;

  // Refuse anything that would take the transaction past its ceiling. The
  // acceptor is inhibited at the ceiling too, so this should not be reachable
  // -- it is here because "should not be reachable" is not a guarantee, and
  // over-crediting is money the machine may not be able to refund.
  if (s_txn.inserted + v > MAX_TRANSACTION_CENTAVOS) return;

  s_txn.inserted += v;
  s_txn.credit += v;
}

money_t billing_credit() {
  return s_txn.credit;
}

money_t billing_inserted() {
  return s_txn.inserted;
}

bool billing_at_ceiling() {
  return s_txn.inserted >= MAX_TRANSACTION_CENTAVOS;
}

volume_t billing_max_selectable_ml() {
  // Centavos -> whole pesos -> millilitres. Integer throughout; the truncation
  // is deliberate, since a partial peso buys nothing.
  return (volume_t)((s_txn.credit / CENTAVOS_PER_PESO) * ML_PER_PESO);
}

money_t billing_price_of(volume_t target_ml) {
  // Volume in, price out, at the fixed rate. A price list lookup -- the
  // argument is a SELECTION, never a sensor reading.
  return (money_t)((target_ml / ML_PER_PESO) * CENTAVOS_PER_PESO);
}

bool billing_can_select(volume_t target_ml) {
  if (target_ml <= 0) return false;
  if (target_ml > MAX_TRANSACTION_ML) return false;
  // Only whole steps are sellable; a 150 mL target has no price at this rate.
  if (target_ml % ML_PER_PESO != 0) return false;
  return billing_price_of(target_ml) <= s_txn.credit;
}

volume_t billing_select(volume_t target_ml) {
  if (!billing_can_select(target_ml)) return 0;

  // The price is deducted NOW, before the valve opens. What the flow sensor
  // later reports cannot change it. That ordering is the whole design.
  s_txn.credit -= billing_price_of(target_ml);
  s_txn.target_ml = target_ml;
  s_txn.dispensed_ml = 0;
  return target_ml;
}

volume_t billing_round_down(volume_t measured_ml) {
  // ALWAYS DOWN. Never to nearest, never up. 305 -> 300.
  // Rounding always favours the machine, never the user. This is deliberate.
  if (measured_ml <= 0) return 0;
  return (measured_ml / REFUND_ROUND_ML) * REFUND_ROUND_ML;
}

void billing_settle_partial(volume_t delivered_ml) {
  // Round FIRST, then price the rounded figure. This is the only point where a
  // measured volume touches money, and rounding first is what keeps the
  // sensor's 2-5% tolerance out of the change computation: what gets charged is
  // a volume the machine chose to sell, not a volume the sensor happened to
  // read.
  volume_t charged_ml = billing_round_down(delivered_ml);

  // Never charge for more than was selected, even if the sensor over-reads.
  if (charged_ml > s_txn.target_ml) charged_ml = s_txn.target_ml;

  const money_t paid = billing_price_of(s_txn.target_ml);  // already deducted
  const money_t owed = billing_price_of(charged_ml);

  // Return the unused portion of the selection price to credit.
  s_txn.credit += (paid - owed);
  s_txn.total_ml += charged_ml;
  s_txn.dispensed_ml = 0;
  s_txn.target_ml = 0;
}

void billing_settle_complete(volume_t delivered_ml) {
  (void)delivered_ml;
  // The pour reached its target, so the price already deducted at selection
  // stands and no refund arises.
  //
  // The target is credited to the totals rather than the measured delivery:
  // the target is what the user paid for and what the machine undertook to
  // sell, and it does not drift with the sensor's tolerance. Using the measured
  // figure would make the daily volume total disagree with the daily profit
  // total by the sensor error, every single transaction.
  s_txn.total_ml += s_txn.target_ml;
  s_txn.dispensed_ml = 0;
  s_txn.target_ml = 0;
}

money_t billing_change_due() {
  return s_txn.credit;
}

volume_t billing_total_dispensed() {
  return s_txn.total_ml;
}

void billing_reset() {
  memset(&s_txn, 0, sizeof(s_txn));
}

money_t billing_worst_case_change() {
  // The most the machine could owe is the ENTIRE CEILING, not the ceiling less
  // one sellable step.
  //
  // This previously returned P19, on the reasoning that a user must buy at
  // least 100 mL. That reasoning is wrong: SPEC 2.2 reaches PAYING_CHANGE with
  // the full credit down two separate paths that involve no pour at all --
  //
  //   SELECTING       | finish without pour        | PAYING_CHANGE
  //   AWAITING_BOTTLE | BOTTLE_WAIT_CANCEL_MS      | PAYING_CHANGE (full credit)
  //
  // -- so a P20 refund is reachable, and a guard sized at P19 would let the
  // machine accept a transaction it cannot refund by exactly one peso. That is
  // the precise failure this guard exists to prevent.
  //
  // Checked against hopper stock BEFORE the first coin is accepted, not against
  // what has been inserted so far. By the time the money is in, refusing is no
  // longer an option.
  return MAX_TRANSACTION_CENTAVOS;
}

void billing_load(const transaction_t *txn) {
  if (txn) s_txn = *txn;
}

void billing_store(transaction_t *txn) {
  if (txn) *txn = s_txn;
}
