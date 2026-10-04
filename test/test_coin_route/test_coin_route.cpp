// Coin routing truth table -- fabrication drawing Rev B sheet 2, and WO-012.
//
// A wrong row here is only visible on hardware by counting coins by hand. The
// two failure directions are not symmetric:
//
//   A coin counted into a HOPPER that is really in the box -> the machine
//   promises change it does not hold. A jam under a paying user.
//
//   A coin counted into the BOX that is really in a hopper -> the machine locks
//   early. Annoying, recoverable.
//
// So the tests that matter most are the ones asserting what must NEVER reach a
// hopper counter.

#include <unity.h>
#include "coin_route.h"

void setUp() {}
void tearDown() {}

static const coin_t ALL_COINS[] = { COIN_NONE, COIN_P1, COIN_P5, COIN_P10,
                                    COIN_P20, COIN_UNKNOWN, COIN_INVALID };
#define N_COINS (sizeof(ALL_COINS) / sizeof(ALL_COINS[0]))

// ---------------------------------------------------------------------------
// RECIRCULATE -- the Rev B truth table
// ---------------------------------------------------------------------------

static void test_recirculate_p1_goes_to_the_p1_hopper() {
  TEST_ASSERT_EQUAL(DEST_P1_HOPPER, coin_route(COIN_ROUTING_RECIRCULATE, COIN_P1));
}

static void test_recirculate_p5_goes_to_the_p5_hopper() {
  TEST_ASSERT_EQUAL(DEST_P5_HOPPER, coin_route(COIN_ROUTING_RECIRCULATE, COIN_P5));
}

static void test_recirculate_p10_and_p20_go_to_the_box_by_denomination() {
  TEST_ASSERT_EQUAL(DEST_BOX_P10, coin_route(COIN_ROUTING_RECIRCULATE, COIN_P10));
  TEST_ASSERT_EQUAL(DEST_BOX_P20, coin_route(COIN_ROUTING_RECIRCULATE, COIN_P20));
}

static void test_recirculate_unknown_coin_never_reaches_a_hopper() {
  // The machine must not claim change stock it cannot verify.
  TEST_ASSERT_EQUAL(DEST_BOX_UNKNOWN, coin_route(COIN_ROUTING_RECIRCULATE, COIN_UNKNOWN));
  TEST_ASSERT_EQUAL(DEST_BOX_UNKNOWN, coin_route(COIN_ROUTING_RECIRCULATE, COIN_INVALID));
  TEST_ASSERT_EQUAL(DEST_BOX_UNKNOWN, coin_route(COIN_ROUTING_RECIRCULATE, COIN_NONE));
}

// ---------------------------------------------------------------------------
// COLLECT_ALL -- Gate A pinned toward the box, no servos fitted
// ---------------------------------------------------------------------------

static void test_collect_all_sends_every_coin_to_the_box() {
  for (size_t i = 0; i < N_COINS; i++) {
    const coin_dest_t d = coin_route(COIN_ROUTING_COLLECT_ALL, ALL_COINS[i]);
    TEST_ASSERT_FALSE_MESSAGE(coin_dest_is_hopper(d),
                              "a coin was counted into a hopper in COLLECT_ALL");
  }
}

static void test_collect_all_counts_each_denomination_separately() {
  // Five counters, so the box's peso value is derivable and an operator's
  // physical count can be reconciled against the record.
  TEST_ASSERT_EQUAL(DEST_BOX_P1,  coin_route(COIN_ROUTING_COLLECT_ALL, COIN_P1));
  TEST_ASSERT_EQUAL(DEST_BOX_P5,  coin_route(COIN_ROUTING_COLLECT_ALL, COIN_P5));
  TEST_ASSERT_EQUAL(DEST_BOX_P10, coin_route(COIN_ROUTING_COLLECT_ALL, COIN_P10));
  TEST_ASSERT_EQUAL(DEST_BOX_P20, coin_route(COIN_ROUTING_COLLECT_ALL, COIN_P20));
  TEST_ASSERT_EQUAL(DEST_BOX_UNKNOWN, coin_route(COIN_ROUTING_COLLECT_ALL, COIN_UNKNOWN));
}

static void test_the_two_modes_agree_on_everything_except_p1_and_p5() {
  // The only difference between the builds is whether small coins recirculate.
  TEST_ASSERT_EQUAL(coin_route(COIN_ROUTING_RECIRCULATE, COIN_P10),
                    coin_route(COIN_ROUTING_COLLECT_ALL, COIN_P10));
  TEST_ASSERT_EQUAL(coin_route(COIN_ROUTING_RECIRCULATE, COIN_P20),
                    coin_route(COIN_ROUTING_COLLECT_ALL, COIN_P20));
  TEST_ASSERT_EQUAL(coin_route(COIN_ROUTING_RECIRCULATE, COIN_UNKNOWN),
                    coin_route(COIN_ROUTING_COLLECT_ALL, COIN_UNKNOWN));
}

static void test_an_unknown_mode_value_routes_as_recirculate() {
  // A mode byte this firmware does not recognise must not send small coins
  // somewhere the full truth table would not.
  for (size_t i = 0; i < N_COINS; i++) {
    TEST_ASSERT_EQUAL(coin_route(COIN_ROUTING_RECIRCULATE, ALL_COINS[i]),
                      coin_route(0xEE, ALL_COINS[i]));
  }
}

// ---------------------------------------------------------------------------
// Power-loss reconciliation -- SPEC 3.3
// ---------------------------------------------------------------------------

static void test_an_unrouted_coin_is_never_counted_into_a_hopper() {
  // The coin was HEADED for a hopper and may well be in it. It is still counted
  // in the box: understate hopper stock, never overstate it.
  for (size_t i = 0; i < N_COINS; i++) {
    TEST_ASSERT_FALSE(coin_dest_is_hopper(coin_route_unrouted(ALL_COINS[i])));
  }
}

static void test_an_unrouted_coin_keeps_its_denomination() {
  // Location assumed, value known. "One P5, assumed in the box" is a legible
  // reconciliation entry; "one coin of unknown value" is a mystery.
  TEST_ASSERT_EQUAL(DEST_BOX_P1,  coin_route_unrouted(COIN_P1));
  TEST_ASSERT_EQUAL(DEST_BOX_P5,  coin_route_unrouted(COIN_P5));
  TEST_ASSERT_EQUAL(DEST_BOX_P10, coin_route_unrouted(COIN_P10));
  TEST_ASSERT_EQUAL(DEST_BOX_P20, coin_route_unrouted(COIN_P20));
  TEST_ASSERT_EQUAL(DEST_BOX_UNKNOWN, coin_route_unrouted(COIN_UNKNOWN));
}

static void test_only_the_two_hoppers_are_hoppers() {
  TEST_ASSERT_TRUE(coin_dest_is_hopper(DEST_P1_HOPPER));
  TEST_ASSERT_TRUE(coin_dest_is_hopper(DEST_P5_HOPPER));
  TEST_ASSERT_FALSE(coin_dest_is_hopper(DEST_BOX_P1));
  TEST_ASSERT_FALSE(coin_dest_is_hopper(DEST_BOX_P5));
  TEST_ASSERT_FALSE(coin_dest_is_hopper(DEST_BOX_P10));
  TEST_ASSERT_FALSE(coin_dest_is_hopper(DEST_BOX_P20));
  TEST_ASSERT_FALSE(coin_dest_is_hopper(DEST_BOX_UNKNOWN));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_recirculate_p1_goes_to_the_p1_hopper);
  RUN_TEST(test_recirculate_p5_goes_to_the_p5_hopper);
  RUN_TEST(test_recirculate_p10_and_p20_go_to_the_box_by_denomination);
  RUN_TEST(test_recirculate_unknown_coin_never_reaches_a_hopper);

  RUN_TEST(test_collect_all_sends_every_coin_to_the_box);
  RUN_TEST(test_collect_all_counts_each_denomination_separately);
  RUN_TEST(test_the_two_modes_agree_on_everything_except_p1_and_p5);
  RUN_TEST(test_an_unknown_mode_value_routes_as_recirculate);

  RUN_TEST(test_an_unrouted_coin_is_never_counted_into_a_hopper);
  RUN_TEST(test_an_unrouted_coin_keeps_its_denomination);
  RUN_TEST(test_only_the_two_hoppers_are_hoppers);

  return UNITY_END();
}
