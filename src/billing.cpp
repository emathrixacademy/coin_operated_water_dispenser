#include <Arduino.h>
#include "billing.h"
#include "coin_acceptor.h"

// Milestone 2: contract only. Implementation lands in Milestone 5.
//
// COINS DETERMINE VOLUME. VOLUME NEVER DETERMINES COINS.
//
// Two functions are implemented here already because they are pure integer
// arithmetic, they are what the Milestone 3 unit tests exercise, and getting
// the rounding direction wrong is the expensive mistake in this file.

static transaction_t s_txn;

void billing_begin() {
  billing_reset();
}

void billing_add_coin(coin_t coin) {
  (void)coin;
  // TODO(M5): add value, refuse past MAX_TRANSACTION_CENTAVOS.
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
  // Credit in centavos -> pesos -> millilitres, integer throughout.
  return (s_txn.credit / CENTAVOS_PER_PESO) * ML_PER_PESO;
}

money_t billing_price_of(volume_t target_ml) {
  // Volume in, price out, at the fixed rate. A price list lookup -- not a
  // measurement being valued. The argument is a SELECTION, never a reading.
  return (target_ml / ML_PER_PESO) * CENTAVOS_PER_PESO;
}

bool billing_can_select(volume_t target_ml) {
  return target_ml > 0 &&
         target_ml <= MAX_TRANSACTION_ML &&
         billing_price_of(target_ml) <= s_txn.credit;
}

volume_t billing_select(volume_t target_ml) {
  (void)target_ml;
  // TODO(M5): deduct the price, fix the target before the valve opens.
  return 0;
}

volume_t billing_round_down(volume_t measured_ml) {
  // ALWAYS DOWN. Never to nearest, never up. 305 -> 300.
  // Rounding always favours the machine, never the user. This is deliberate.
  if (measured_ml <= 0) return 0;
  return (measured_ml / REFUND_ROUND_ML) * REFUND_ROUND_ML;
}

void billing_settle_partial(volume_t delivered_ml) {
  (void)delivered_ml;
  // TODO(M5): round down FIRST, charge the rounded figure, return the unused
  // portion of the selection price to credit.
}

void billing_settle_complete(volume_t delivered_ml) {
  (void)delivered_ml;
  // TODO(M5)
}

money_t billing_change_due() {
  return s_txn.credit;
}

volume_t billing_total_dispensed() {
  return s_txn.total_ml;
}

void billing_reset() {
  s_txn.credit = 0;
  s_txn.inserted = 0;
  s_txn.target_ml = 0;
  s_txn.dispensed_ml = 0;
  s_txn.total_ml = 0;
  s_txn.open = false;
}

money_t billing_worst_case_change() {
  // The most the machine could owe on a full-ceiling transaction: the user
  // inserts the ceiling and buys the smallest sellable volume.
  return MAX_TRANSACTION_CENTAVOS - billing_price_of(REFUND_ROUND_ML);
}

void billing_load(const transaction_t *txn) {
  if (txn) s_txn = *txn;
}

void billing_store(transaction_t *txn) {
  if (txn) *txn = s_txn;
}
