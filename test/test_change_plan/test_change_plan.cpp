// Change payout planning -- SPEC 3.4.
//
// This function decides what money a person is handed. It was the largest
// untested surface in the codebase until it was extracted out of the hardware
// wrapper, which is the only reason these tests can exist.
//
// The two failure directions are not symmetric and the tests are weighted
// accordingly:
//
//   Planning MORE than stock  -> the machine promises change it cannot pay,
//                                which is a jam under a paying user.
//   Planning LESS than stock  -> an unnecessary lockout. Annoying, recoverable.
//
// So every "can it fail closed" case matters more than every "is it optimal"
// case, and there are more of the former here.

#include <unity.h>
#include "change_plan.h"

static change_plan_t P;

void setUp() {
  P.p1 = 0;
  P.p5 = 0;
}
void tearDown() {}

// Convenience: plan N pesos against stock, assert success, return via P.
static bool plan_pesos(int32_t pesos, uint16_t p1, uint16_t p5) {
  return change_plan(pesos * CENTAVOS_PER_PESO, p1, p5, &P);
}

// ---------------------------------------------------------------------------
// Degenerate and malformed input
// ---------------------------------------------------------------------------

static void test_zero_is_coverable_and_pays_nothing() {
  TEST_ASSERT_TRUE(change_plan(0, 100, 100, &P));
  TEST_ASSERT_EQUAL_UINT16(0, P.p1);
  TEST_ASSERT_EQUAL_UINT16(0, P.p5);
}

static void test_zero_is_coverable_even_with_empty_hoppers() {
  // Nothing owed cannot fail, whatever the stock. If this returned false the
  // machine would lock at the end of every exact-change transaction.
  TEST_ASSERT_TRUE(change_plan(0, 0, 0, &P));
}

static void test_negative_amount_fails() {
  TEST_ASSERT_FALSE(change_plan(-100, 100, 100, &P));
  TEST_ASSERT_EQUAL_UINT16(0, P.p1);
  TEST_ASSERT_EQUAL_UINT16(0, P.p5);
}

static void test_non_whole_peso_amount_fails() {
  // No sub-peso coin exists to pay 50 centavos. Rounding here would create or
  // destroy money at the moment of payout.
  TEST_ASSERT_FALSE(change_plan(50, 100, 100, &P));
  TEST_ASSERT_FALSE(change_plan(150, 100, 100, &P));
  TEST_ASSERT_FALSE(change_plan(1999, 100, 100, &P));
}

static void test_failure_always_zeroes_the_plan() {
  // A caller that ignores the return value must not find a usable-looking plan
  // sitting in the out parameter.
  P.p1 = 99;
  P.p5 = 99;
  TEST_ASSERT_FALSE(change_plan(-1, 100, 100, &P));
  TEST_ASSERT_EQUAL_UINT16(0, P.p1);
  TEST_ASSERT_EQUAL_UINT16(0, P.p5);
}

static void test_null_out_is_tolerated() {
  // can_cover() asks only the question, not for the plan.
  TEST_ASSERT_TRUE(change_plan(500, 100, 100, nullptr));
  TEST_ASSERT_FALSE(change_plan(500, 0, 0, nullptr));
}

// ---------------------------------------------------------------------------
// Largest-coin-first
// ---------------------------------------------------------------------------

static void test_prefers_p5_when_well_stocked() {
  // P15 from healthy stock: three P5, no P1.
  TEST_ASSERT_TRUE(plan_pesos(15, 100, 100));
  TEST_ASSERT_EQUAL_UINT16(3, P.p5);
  TEST_ASSERT_EQUAL_UINT16(0, P.p1);
}

static void test_remainder_goes_to_p1() {
  // P17 = three P5 + two P1.
  TEST_ASSERT_TRUE(plan_pesos(17, 100, 100));
  TEST_ASSERT_EQUAL_UINT16(3, P.p5);
  TEST_ASSERT_EQUAL_UINT16(2, P.p1);
}

static void test_amounts_under_five_are_all_p1() {
  TEST_ASSERT_TRUE(plan_pesos(4, 100, 100));
  TEST_ASSERT_EQUAL_UINT16(0, P.p5);
  TEST_ASSERT_EQUAL_UINT16(4, P.p1);
}

static void test_the_ceiling_case() {
  // P20, the worst case the guard is sized against.
  TEST_ASSERT_TRUE(plan_pesos(20, 100, 100));
  TEST_ASSERT_EQUAL_UINT16(4, P.p5);
  TEST_ASSERT_EQUAL_UINT16(0, P.p1);
}

// ---------------------------------------------------------------------------
// The P5 reserve -- the part that exists to keep the machine in service
// ---------------------------------------------------------------------------

static void test_reserve_is_untouchable() {
  // Exactly at the reserve: no P5 is spendable, so P15 must come out as P1.
  TEST_ASSERT_TRUE(plan_pesos(15, 100, HOPPER_RESERVE_P5));
  TEST_ASSERT_EQUAL_UINT16(0, P.p5);
  TEST_ASSERT_EQUAL_UINT16(15, P.p1);
}

static void test_below_reserve_pays_entirely_in_p1() {
  TEST_ASSERT_TRUE(plan_pesos(15, 100, HOPPER_RESERVE_P5 - 1));
  TEST_ASSERT_EQUAL_UINT16(0, P.p5);
  TEST_ASSERT_EQUAL_UINT16(15, P.p1);
}

static void test_one_above_reserve_spends_exactly_one_p5() {
  // The boundary. One coin above the reserve means one coin is spendable.
  TEST_ASSERT_TRUE(plan_pesos(15, 100, HOPPER_RESERVE_P5 + 1));
  TEST_ASSERT_EQUAL_UINT16(1, P.p5);
  TEST_ASSERT_EQUAL_UINT16(10, P.p1);
}

static void test_reserve_is_never_eaten_to_avoid_a_failure() {
  // P15 owed, plenty of P5 sitting in the reserve, but only 10 P1.
  // The reserve must NOT be raided to make this work -- it must fail.
  TEST_ASSERT_FALSE(plan_pesos(15, 10, HOPPER_RESERVE_P5));
}

static void test_partial_reserve_spend_still_respects_the_floor() {
  // Two coins above the reserve, P20 owed. Only two P5 may be spent, and the
  // remaining P10 must come from P1.
  TEST_ASSERT_TRUE(plan_pesos(20, 100, HOPPER_RESERVE_P5 + 2));
  TEST_ASSERT_EQUAL_UINT16(2, P.p5);
  TEST_ASSERT_EQUAL_UINT16(10, P.p1);
}

// ---------------------------------------------------------------------------
// Failing closed
// ---------------------------------------------------------------------------

static void test_fails_when_p1_cannot_cover_the_remainder() {
  // P17 with three P5 spendable leaves P2, and only one P1 in stock.
  TEST_ASSERT_FALSE(plan_pesos(17, 1, 100));
}

static void test_fails_with_both_hoppers_empty() {
  TEST_ASSERT_FALSE(plan_pesos(1, 0, 0));
}

static void test_exact_p1_stock_is_enough_but_one_less_is_not() {
  // The precise boundary of the P1 check. Below the P5 reserve so the whole
  // amount falls on P1.
  TEST_ASSERT_TRUE(plan_pesos(15, 15, 0));
  TEST_ASSERT_EQUAL_UINT16(15, P.p1);
  TEST_ASSERT_FALSE(plan_pesos(15, 14, 0));
}

static void test_never_plans_more_than_stock_across_the_whole_range() {
  // The invariant that matters: for every amount up to the ceiling and a spread
  // of stock levels, a successful plan is payable from what is actually there,
  // pays exactly what was owed, and never touches the P5 reserve.
  for (int32_t pesos = 0; pesos <= 20; pesos++) {
    for (uint16_t p1 = 0; p1 <= 25; p1 += 5) {
      for (uint16_t p5 = 0; p5 <= 20; p5 += 2) {
        if (!change_plan(pesos * CENTAVOS_PER_PESO, p1, p5, &P)) continue;

        TEST_ASSERT_TRUE(P.p1 <= p1);
        TEST_ASSERT_TRUE(P.p5 <= p5);
        TEST_ASSERT_EQUAL_INT32(pesos, (int32_t)P.p1 + 5 * (int32_t)P.p5);
        TEST_ASSERT_TRUE((int32_t)(p5 - P.p5) >= HOPPER_RESERVE_P5 ||
                         P.p5 == 0);
      }
    }
  }
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_zero_is_coverable_and_pays_nothing);
  RUN_TEST(test_zero_is_coverable_even_with_empty_hoppers);
  RUN_TEST(test_negative_amount_fails);
  RUN_TEST(test_non_whole_peso_amount_fails);
  RUN_TEST(test_failure_always_zeroes_the_plan);
  RUN_TEST(test_null_out_is_tolerated);

  RUN_TEST(test_prefers_p5_when_well_stocked);
  RUN_TEST(test_remainder_goes_to_p1);
  RUN_TEST(test_amounts_under_five_are_all_p1);
  RUN_TEST(test_the_ceiling_case);

  RUN_TEST(test_reserve_is_untouchable);
  RUN_TEST(test_below_reserve_pays_entirely_in_p1);
  RUN_TEST(test_one_above_reserve_spends_exactly_one_p5);
  RUN_TEST(test_reserve_is_never_eaten_to_avoid_a_failure);
  RUN_TEST(test_partial_reserve_spend_still_respects_the_floor);

  RUN_TEST(test_fails_when_p1_cannot_cover_the_remainder);
  RUN_TEST(test_fails_with_both_hoppers_empty);
  RUN_TEST(test_exact_p1_stock_is_enough_but_one_less_is_not);
  RUN_TEST(test_never_plans_more_than_stock_across_the_whole_range);

  return UNITY_END();
}
