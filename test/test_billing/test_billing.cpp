// Host-side tests for the billing arithmetic and the refund rounding.
//
// These are NOT a release gate. docs/scenarios.md is, and it requires real
// coins on real hardware. What is checked here is the integer math that sits
// behind the coin path -- the part where a wrong sign or a wrong rounding
// direction quietly costs money on every transaction.

#include <unity.h>
#include "billing.h"

void setUp() { billing_begin(); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Refund rounding -- ALWAYS DOWN
// ---------------------------------------------------------------------------

static void test_round_down_exact_multiples() {
  TEST_ASSERT_EQUAL_INT32(0, billing_round_down(0));
  TEST_ASSERT_EQUAL_INT32(100, billing_round_down(100));
  TEST_ASSERT_EQUAL_INT32(2000, billing_round_down(2000));
}

static void test_round_down_the_documented_case() {
  // CLAUDE.md and scenarios.md case 8 both name this one explicitly.
  TEST_ASSERT_EQUAL_INT32(300, billing_round_down(305));
}

static void test_round_down_never_rounds_to_nearest() {
  // 399 is nearer to 400 than to 300. Rounding to nearest here would hand the
  // user 100 mL they did not pay for. It must still go down.
  TEST_ASSERT_EQUAL_INT32(300, billing_round_down(399));
  TEST_ASSERT_EQUAL_INT32(300, billing_round_down(350));
  TEST_ASSERT_EQUAL_INT32(1900, billing_round_down(1999));
}

static void test_round_down_boundaries() {
  TEST_ASSERT_EQUAL_INT32(200, billing_round_down(299));
  TEST_ASSERT_EQUAL_INT32(300, billing_round_down(300));
  TEST_ASSERT_EQUAL_INT32(300, billing_round_down(301));
  TEST_ASSERT_EQUAL_INT32(300, billing_round_down(399));
  TEST_ASSERT_EQUAL_INT32(400, billing_round_down(400));
}

static void test_round_down_below_one_step_is_zero() {
  // Under 100 mL the user gets nothing charged. Rounding up here would charge
  // a full peso for a dribble.
  TEST_ASSERT_EQUAL_INT32(0, billing_round_down(1));
  TEST_ASSERT_EQUAL_INT32(0, billing_round_down(99));
}

static void test_round_down_rejects_negative() {
  TEST_ASSERT_EQUAL_INT32(0, billing_round_down(-50));
}

// ---------------------------------------------------------------------------
// Credit accumulation and the ceiling
// ---------------------------------------------------------------------------

static void test_coin_values() {
  TEST_ASSERT_EQUAL_INT32(100, coin_value(COIN_P1));
  TEST_ASSERT_EQUAL_INT32(500, coin_value(COIN_P5));
  TEST_ASSERT_EQUAL_INT32(1000, coin_value(COIN_P10));
  TEST_ASSERT_EQUAL_INT32(2000, coin_value(COIN_P20));
}

static void test_invalid_coins_are_worth_nothing() {
  // A slug must not become credit.
  TEST_ASSERT_EQUAL_INT32(0, coin_value(COIN_INVALID));
  TEST_ASSERT_EQUAL_INT32(0, coin_value(COIN_NONE));

  billing_add_coin(COIN_INVALID);
  billing_add_coin(COIN_NONE);
  TEST_ASSERT_EQUAL_INT32(0, billing_credit());
  TEST_ASSERT_EQUAL_INT32(0, billing_inserted());
}

static void test_credit_accumulates() {
  billing_add_coin(COIN_P5);
  billing_add_coin(COIN_P1);
  TEST_ASSERT_EQUAL_INT32(600, billing_credit());
  TEST_ASSERT_EQUAL_INT32(600, billing_inserted());
}

static void test_ceiling_is_enforced() {
  billing_add_coin(COIN_P20);
  TEST_ASSERT_TRUE(billing_at_ceiling());
  TEST_ASSERT_EQUAL_INT32(2000, billing_credit());

  // One more coin must not be credited -- that is money the machine may not be
  // able to refund.
  billing_add_coin(COIN_P1);
  TEST_ASSERT_EQUAL_INT32(2000, billing_credit());
  TEST_ASSERT_EQUAL_INT32(2000, billing_inserted());
}

static void test_ceiling_blocks_a_coin_that_would_overshoot() {
  billing_add_coin(COIN_P10);
  billing_add_coin(COIN_P5);   // 1500
  billing_add_coin(COIN_P10);  // would be 2500, refused
  TEST_ASSERT_EQUAL_INT32(1500, billing_credit());

  // ...but a coin that fits exactly is still accepted.
  billing_add_coin(COIN_P5);
  TEST_ASSERT_EQUAL_INT32(2000, billing_credit());
}

// ---------------------------------------------------------------------------
// Selection: credit sets the ceiling, the selection sets the price
// ---------------------------------------------------------------------------

static void test_max_selectable_tracks_credit() {
  TEST_ASSERT_EQUAL_INT32(0, billing_max_selectable_ml());
  billing_add_coin(COIN_P5);
  TEST_ASSERT_EQUAL_INT32(500, billing_max_selectable_ml());
  billing_add_coin(COIN_P5);
  TEST_ASSERT_EQUAL_INT32(1000, billing_max_selectable_ml());
}

static void test_price_is_a_lookup_not_a_measurement() {
  TEST_ASSERT_EQUAL_INT32(100, billing_price_of(100));
  TEST_ASSERT_EQUAL_INT32(500, billing_price_of(500));
  TEST_ASSERT_EQUAL_INT32(2000, billing_price_of(2000));
}

static void test_cannot_select_beyond_credit() {
  billing_add_coin(COIN_P5);
  TEST_ASSERT_TRUE(billing_can_select(500));
  TEST_ASSERT_FALSE(billing_can_select(600));
  TEST_ASSERT_EQUAL_INT32(0, billing_select(600));
  // A refused selection must not have moved the credit.
  TEST_ASSERT_EQUAL_INT32(500, billing_credit());
}

static void test_cannot_select_a_partial_step() {
  billing_add_coin(COIN_P20);
  TEST_ASSERT_FALSE(billing_can_select(150));
  TEST_ASSERT_FALSE(billing_can_select(1));
}

static void test_selection_deducts_before_the_valve_opens() {
  // The scenario from the build prompt: P20 in, 500 mL out, P15 left.
  billing_add_coin(COIN_P20);
  TEST_ASSERT_EQUAL_INT32(500, billing_select(500));
  TEST_ASSERT_EQUAL_INT32(1500, billing_credit());
  TEST_ASSERT_EQUAL_INT32(1500, billing_change_due());
}

// ---------------------------------------------------------------------------
// Settlement
// ---------------------------------------------------------------------------

static void test_complete_pour_leaves_credit_alone() {
  billing_add_coin(COIN_P20);
  billing_select(2000);
  TEST_ASSERT_EQUAL_INT32(0, billing_credit());
  billing_settle_complete(2000);
  TEST_ASSERT_EQUAL_INT32(0, billing_change_due());
  TEST_ASSERT_EQUAL_INT32(2000, billing_total_dispensed());
}

static void test_partial_pour_the_documented_case() {
  // scenarios.md case 8: P20 in, 2000 mL selected, stopped at a measured
  // 305 mL. Charge 300 mL = P3, refund P17.
  billing_add_coin(COIN_P20);
  billing_select(2000);
  billing_settle_partial(305);
  TEST_ASSERT_EQUAL_INT32(1700, billing_change_due());
  TEST_ASSERT_EQUAL_INT32(300, billing_total_dispensed());
}

static void test_partial_rounding_favours_the_machine() {
  // 399 mL measured is charged as 300, not 400. The user loses the 99 mL.
  billing_add_coin(COIN_P20);
  billing_select(2000);
  billing_settle_partial(399);
  TEST_ASSERT_EQUAL_INT32(1700, billing_change_due());
}

static void test_partial_below_one_step_refunds_everything() {
  // Nothing usable delivered -- the user gets it all back.
  billing_add_coin(COIN_P5);
  billing_select(500);
  billing_settle_partial(80);
  TEST_ASSERT_EQUAL_INT32(500, billing_change_due());
  TEST_ASSERT_EQUAL_INT32(0, billing_total_dispensed());
}

static void test_partial_never_charges_beyond_the_selection() {
  // If the sensor over-reads past the target, the user is still only charged
  // for what they selected. An over-reading sensor must not be able to invent
  // a charge.
  billing_add_coin(COIN_P20);
  billing_select(500);
  billing_settle_partial(900);
  TEST_ASSERT_EQUAL_INT32(1500, billing_change_due());
  TEST_ASSERT_EQUAL_INT32(500, billing_total_dispensed());
}

static void test_multiple_pours_accumulate() {
  billing_add_coin(COIN_P20);
  billing_select(500);
  billing_settle_complete(500);
  TEST_ASSERT_EQUAL_INT32(1500, billing_change_due());

  billing_select(1000);
  billing_settle_complete(1000);
  TEST_ASSERT_EQUAL_INT32(500, billing_change_due());
  TEST_ASSERT_EQUAL_INT32(1500, billing_total_dispensed());
}

// ---------------------------------------------------------------------------
// Guards
// ---------------------------------------------------------------------------

static void test_worst_case_change() {
  // The whole ceiling, not the ceiling less one sellable step.
  //
  // This asserted P19 until the SPEC 2.2 transition table was reconciled
  // against it. Two paths reach PAYING_CHANGE with the full credit and no pour
  // at all -- "finish without pour" from SELECTING, and the bottle-wait timeout
  // from AWAITING_BOTTLE -- so a P20 refund is reachable and a guard sized at
  // P19 lets the machine accept a transaction it cannot refund by one peso.
  TEST_ASSERT_EQUAL_INT32(2000, billing_worst_case_change());
}

static void test_worst_case_change_covers_a_no_pour_refund() {
  // The path that proves the point: ceiling in, nothing selected, user walks.
  billing_add_coin(COIN_P20);
  TEST_ASSERT_EQUAL_INT32(2000, billing_change_due());
  TEST_ASSERT_TRUE(billing_change_due() <= billing_worst_case_change());
}

static void test_unknown_coin_credits_the_minimum() {
  // SPEC 3.1: an in-range pulse train that matched no denomination is a real
  // coin. Credit the minimum rather than nothing -- crediting nothing takes the
  // user's money. Routing to profit is the diverter's half of the same rule.
  TEST_ASSERT_EQUAL_INT32(COIN_VALUE_P1, coin_value(COIN_UNKNOWN));

  billing_add_coin(COIN_UNKNOWN);
  TEST_ASSERT_EQUAL_INT32(100, billing_credit());
  TEST_ASSERT_EQUAL_INT32(100, billing_inserted());
}

static void test_reset_clears_everything() {
  billing_add_coin(COIN_P20);
  billing_select(500);
  billing_settle_complete(500);
  billing_reset();
  TEST_ASSERT_EQUAL_INT32(0, billing_credit());
  TEST_ASSERT_EQUAL_INT32(0, billing_inserted());
  TEST_ASSERT_EQUAL_INT32(0, billing_total_dispensed());
  TEST_ASSERT_EQUAL_INT32(0, billing_change_due());
}

static void test_no_money_is_created_or_destroyed() {
  // The invariant that matters: across any sequence of pours, what the user
  // was charged plus what they get back equals what they put in. If this ever
  // fails, the difference is coming out of the hoppers.
  billing_add_coin(COIN_P20);
  billing_select(1000);
  billing_settle_partial(305);   // charged 300 mL = P3
  billing_select(200);
  billing_settle_complete(200);  // charged P2

  const money_t charged = billing_price_of(billing_total_dispensed());
  TEST_ASSERT_EQUAL_INT32(500, charged);
  TEST_ASSERT_EQUAL_INT32(2000, charged + billing_change_due());
  TEST_ASSERT_EQUAL_INT32(billing_inserted(), charged + billing_change_due());
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_round_down_exact_multiples);
  RUN_TEST(test_round_down_the_documented_case);
  RUN_TEST(test_round_down_never_rounds_to_nearest);
  RUN_TEST(test_round_down_boundaries);
  RUN_TEST(test_round_down_below_one_step_is_zero);
  RUN_TEST(test_round_down_rejects_negative);

  RUN_TEST(test_coin_values);
  RUN_TEST(test_invalid_coins_are_worth_nothing);
  RUN_TEST(test_credit_accumulates);
  RUN_TEST(test_ceiling_is_enforced);
  RUN_TEST(test_ceiling_blocks_a_coin_that_would_overshoot);

  RUN_TEST(test_max_selectable_tracks_credit);
  RUN_TEST(test_price_is_a_lookup_not_a_measurement);
  RUN_TEST(test_cannot_select_beyond_credit);
  RUN_TEST(test_cannot_select_a_partial_step);
  RUN_TEST(test_selection_deducts_before_the_valve_opens);

  RUN_TEST(test_complete_pour_leaves_credit_alone);
  RUN_TEST(test_partial_pour_the_documented_case);
  RUN_TEST(test_partial_rounding_favours_the_machine);
  RUN_TEST(test_partial_below_one_step_refunds_everything);
  RUN_TEST(test_partial_never_charges_beyond_the_selection);
  RUN_TEST(test_multiple_pours_accumulate);

  RUN_TEST(test_worst_case_change);
  RUN_TEST(test_worst_case_change_covers_a_no_pour_refund);
  RUN_TEST(test_unknown_coin_credits_the_minimum);
  RUN_TEST(test_reset_clears_everything);
  RUN_TEST(test_no_money_is_created_or_destroyed);

  return UNITY_END();
}
