#ifndef BILLING_H
#define BILLING_H

// Billing -- coin-to-volume allocation and refund computation.
//
// ===========================================================================
// COINS DETERMINE VOLUME. VOLUME NEVER DETERMINES COINS.
// ===========================================================================
//
// Coins set the credit. Credit sets the volume target. The valve opens. The
// flow sensor closes it. That is the whole direction of causation and it does
// not reverse anywhere in this module.
//
// There is deliberately NO function in this header that takes a millilitre
// reading and returns an amount owed for it. If you find yourself needing one,
// stop and re-read CLAUDE.md. Billing from a measured volume makes the change
// computation drift against the flow sensor's 2-5% tolerance, and the drift
// comes out of the hoppers on every transaction.
//
// The one place a measured volume touches money is settling a partial pour,
// and even there the measurement is first rounded DOWN to a whole
// REFUND_ROUND_ML step -- so what is charged is a volume the machine chose to
// sell, not a volume the sensor happened to read.
//
// ---------------------------------------------------------------------------
// The volume selector
// ---------------------------------------------------------------------------
//
// The client's mockup shows a volume selection screen after coins go in. This
// is compatible with coins-first billing, implemented as follows:
//
//   - Inserted credit sets the CEILING of what can be selected.
//   - The user picks a target within that ceiling.
//   - The price of that target is computed FROM THE SELECTION, not from what
//     the flow sensor later reports.
//
// Select 500 mL from P20 inserted: costs P5, leaves P15 credit.
//
// The selector is a way of spending credit. It is never a way of measuring
// water into a price.

#include "types.h"

void billing_begin();

// --- Credit -------------------------------------------------------------

// Add an accepted coin's value to the credit. Refuses anything that would take
// the credit past MAX_TRANSACTION_CENTAVOS.
void billing_add_coin(coin_t coin);

money_t billing_credit();     // centavos still available to spend
money_t billing_inserted();   // centavos inserted this transaction, for the summary

// True once credit has reached the per-transaction ceiling. The acceptor is
// inhibited at this point -- the machine stops taking money it has capped.
bool billing_at_ceiling();

// --- Selection ----------------------------------------------------------

// Largest volume the current credit can buy, in millilitres. This is the
// ceiling the Select Volume screen greys out above.
volume_t billing_max_selectable_ml();

// Price of a target volume, in centavos. A pure function of the selection.
// Note the direction: volume in, price out, at the fixed rate. This is a
// price list lookup, not a measurement being valued.
money_t billing_price_of(volume_t target_ml);

// Can the current credit afford this target?
bool billing_can_select(volume_t target_ml);

// Commit a selection. Deducts its price from credit and returns the target to
// pour. Must be called before the valve opens -- the target is fixed first.
// Returns 0 and changes nothing if the selection is unaffordable.
volume_t billing_select(volume_t target_ml);

// --- Settlement ---------------------------------------------------------

// Round a measured volume DOWN to the nearest REFUND_ROUND_ML.
//
// 305 mL -> 300 mL. Always down. Never to nearest, never up. Rounding always
// favours the machine, never the user, and this is deliberate.
volume_t billing_round_down(volume_t measured_ml);

// Settle a pour that ended early -- bottle abandoned, or a flow stall.
//
// The delivered volume is rounded down, charged at the fixed rate, and the
// unused portion of the already-deducted selection price is returned to credit.
// Stop at a measured 305 mL of a 2000 mL selection and the machine keeps P3 and
// returns P17 to credit.
//
// The rounding happens FIRST and the price comes from the rounded figure, which
// is what keeps a sensor reading out of the money path.
void billing_settle_partial(volume_t delivered_ml);

// Cancel a selection that never poured -- the back arrow from AWAITING_BOTTLE.
//
// Returns the whole selection price to credit. Nothing was dispensed, so no
// rounding applies and the user loses nothing. See decisions.md, "Back arrow".
void billing_cancel_selection();

// Credit a completed pour toward the transaction totals.
void billing_settle_complete(volume_t delivered_ml);

// --- Finishing ----------------------------------------------------------

// Change owed if the user finishes now. Equal to the remaining credit.
money_t billing_change_due();

// Total water delivered across every pour this transaction.
volume_t billing_total_dispensed();

// Clear down for the next user. Called only after change has been physically
// counted out of the hoppers and the transaction is committed to EEPROM.
void billing_reset();

// --- Guards -------------------------------------------------------------

// Worst-case change the machine could owe for a full-ceiling transaction.
// Checked against hopper stock BEFORE the first coin is accepted. If the
// hoppers cannot cover it the machine locks with LOW CHANGE rather than taking
// money it cannot honour.
money_t billing_worst_case_change();

// --- Persistence --------------------------------------------------------

void billing_load(const transaction_t *txn);
void billing_store(transaction_t *txn);

#endif  // BILLING_H
