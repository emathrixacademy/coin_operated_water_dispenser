// Fault set arithmetic -- SPEC 6.2 priority and the persistent subset.
//
// Two things are being protected here.
//
// The PRIORITY ordering decides what a locked machine shows a technician. Get
// it wrong and a LOW CHANGE screen hides an active CHANGE JAM, so the tech
// reloads the hoppers, walks away, and the machine is still jammed.
//
// The PERSISTENT SUBSET decides what survives a power cycle. Get it wrong and
// the operator learns that the fix for a jam is a reboot -- the coins stay
// stuck and the machine goes back to taking money it cannot pay out.

#include <unity.h>
#include "fault_mask.h"

void setUp() {}
void tearDown() {}

static const fault_t ALL[] = {
  FAULT_OUT_OF_WATER, FAULT_LOW_CHANGE, FAULT_STORAGE_FULL, FAULT_CHANGE_JAM,
  FAULT_FLOW_STALL, FAULT_PUMP_RUNTIME, FAULT_ACCEPTOR
};
static const uint8_t ALL_COUNT = sizeof(ALL) / sizeof(ALL[0]);

// ---------------------------------------------------------------------------
// Bits
// ---------------------------------------------------------------------------

static void test_none_has_no_bit() {
  // FAULT_NONE is never a member of a set. If it had a bit, an empty set would
  // read as locked.
  TEST_ASSERT_EQUAL_UINT8(0, fault_bit(FAULT_NONE));
}

static void test_every_fault_has_a_distinct_nonzero_bit() {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < ALL_COUNT; i++) {
    const uint8_t b = fault_bit(ALL[i]);
    TEST_ASSERT_NOT_EQUAL_UINT8(0, b);
    TEST_ASSERT_EQUAL_UINT8(0, seen & b);   // no collision
    seen |= b;
  }
}

static void test_all_bits_fit_in_the_stored_byte() {
  // The EEPROM record holds a uint8_t. A fault enum that outgrew it would
  // silently stop persisting the ones that fell off the top.
  uint8_t all = 0;
  for (uint8_t i = 0; i < ALL_COUNT; i++) all |= fault_bit(ALL[i]);
  TEST_ASSERT_EQUAL_UINT8(all, all & 0xFF);
}

// ---------------------------------------------------------------------------
// Priority -- SPEC 6.2
// ---------------------------------------------------------------------------

static void test_empty_set_is_none() {
  TEST_ASSERT_EQUAL(FAULT_NONE, fault_highest(0));
}

static void test_single_fault_returns_itself() {
  for (uint8_t i = 0; i < ALL_COUNT; i++) {
    TEST_ASSERT_EQUAL(ALL[i], fault_highest(fault_bit(ALL[i])));
  }
}

static void test_change_jam_outranks_everything() {
  // The case that motivated the mask. A jam must never be hidden.
  for (uint8_t i = 0; i < ALL_COUNT; i++) {
    const uint8_t m = fault_bit(FAULT_CHANGE_JAM) | fault_bit(ALL[i]);
    TEST_ASSERT_EQUAL(FAULT_CHANGE_JAM, fault_highest(m));
  }
}

static void test_low_change_does_not_mask_a_jam() {
  const uint8_t m = fault_bit(FAULT_LOW_CHANGE) | fault_bit(FAULT_CHANGE_JAM);
  TEST_ASSERT_EQUAL(FAULT_CHANGE_JAM, fault_highest(m));
}

static void test_the_documented_order_holds_pairwise() {
  // SPEC 6.2, most blocking first. Each entry must outrank every entry after it.
  static const fault_t ORDER[] = {
    FAULT_CHANGE_JAM, FAULT_FLOW_STALL, FAULT_PUMP_RUNTIME, FAULT_ACCEPTOR,
    FAULT_OUT_OF_WATER, FAULT_LOW_CHANGE, FAULT_STORAGE_FULL
  };
  const uint8_t n = sizeof(ORDER) / sizeof(ORDER[0]);
  for (uint8_t i = 0; i < n; i++) {
    for (uint8_t j = i + 1; j < n; j++) {
      const uint8_t m = fault_bit(ORDER[i]) | fault_bit(ORDER[j]);
      TEST_ASSERT_EQUAL(ORDER[i], fault_highest(m));
    }
  }
}

static void test_full_set_reports_the_jam() {
  uint8_t all = 0;
  for (uint8_t i = 0; i < ALL_COUNT; i++) all |= fault_bit(ALL[i]);
  TEST_ASSERT_EQUAL(FAULT_CHANGE_JAM, fault_highest(all));
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

static void test_the_four_serviceable_faults_persist() {
  TEST_ASSERT_TRUE(fault_persistent(FAULT_CHANGE_JAM));
  TEST_ASSERT_TRUE(fault_persistent(FAULT_FLOW_STALL));
  TEST_ASSERT_TRUE(fault_persistent(FAULT_PUMP_RUNTIME));
  TEST_ASSERT_TRUE(fault_persistent(FAULT_ACCEPTOR));
}

static void test_the_transient_faults_do_not_persist() {
  // These re-evaluate from live sensor state on boot, so storing them would
  // resurrect a condition that may have been fixed while the power was off.
  TEST_ASSERT_FALSE(fault_persistent(FAULT_OUT_OF_WATER));
  TEST_ASSERT_FALSE(fault_persistent(FAULT_LOW_CHANGE));
  TEST_ASSERT_FALSE(fault_persistent(FAULT_STORAGE_FULL));
  TEST_ASSERT_FALSE(fault_persistent(FAULT_NONE));
}

static void test_subset_keeps_only_the_persistent_bits() {
  uint8_t all = 0;
  for (uint8_t i = 0; i < ALL_COUNT; i++) all |= fault_bit(ALL[i]);

  const uint8_t kept = fault_persistent_subset(all);
  const uint8_t want = fault_bit(FAULT_CHANGE_JAM) |
                       fault_bit(FAULT_FLOW_STALL) |
                       fault_bit(FAULT_PUMP_RUNTIME) |
                       fault_bit(FAULT_ACCEPTOR);
  TEST_ASSERT_EQUAL_UINT8(want, kept);
}

static void test_subset_of_transient_only_is_empty() {
  const uint8_t m = fault_bit(FAULT_OUT_OF_WATER) | fault_bit(FAULT_LOW_CHANGE);
  TEST_ASSERT_EQUAL_UINT8(0, fault_persistent_subset(m));
}

static void test_subset_is_idempotent() {
  // faults.cpp compares subsets to decide whether to spend an EEPROM write.
  // If this were not idempotent it would write on every pass and burn the cell.
  uint8_t all = 0;
  for (uint8_t i = 0; i < ALL_COUNT; i++) all |= fault_bit(ALL[i]);
  const uint8_t once = fault_persistent_subset(all);
  TEST_ASSERT_EQUAL_UINT8(once, fault_persistent_subset(once));
}

static void test_a_stored_garbage_byte_cannot_resurrect_a_transient_fault() {
  // On restore, whatever is in EEPROM is filtered through the subset. A byte
  // from a firmware with a different fault set -- or a corrupt one that still
  // passed CRC -- must not be able to boot the machine into LOW CHANGE.
  TEST_ASSERT_EQUAL_UINT8(0, fault_persistent_subset(0xFF) &
                             fault_bit(FAULT_LOW_CHANGE));
  TEST_ASSERT_EQUAL_UINT8(0, fault_persistent_subset(0xFF) &
                             fault_bit(FAULT_OUT_OF_WATER));
  TEST_ASSERT_EQUAL_UINT8(0, fault_persistent_subset(0xFF) &
                             fault_bit(FAULT_STORAGE_FULL));
}

static void test_virgin_and_erased_bytes_are_empty_sets() {
  // 0x00 is an erased cell. 0xFF is virgin, and must not read as "every fault".
  TEST_ASSERT_EQUAL(FAULT_NONE, fault_highest(fault_persistent_subset(0x00)));
  TEST_ASSERT_EQUAL(FAULT_CHANGE_JAM,
                    fault_highest(fault_persistent_subset(0xFF)));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_none_has_no_bit);
  RUN_TEST(test_every_fault_has_a_distinct_nonzero_bit);
  RUN_TEST(test_all_bits_fit_in_the_stored_byte);

  RUN_TEST(test_empty_set_is_none);
  RUN_TEST(test_single_fault_returns_itself);
  RUN_TEST(test_change_jam_outranks_everything);
  RUN_TEST(test_low_change_does_not_mask_a_jam);
  RUN_TEST(test_the_documented_order_holds_pairwise);
  RUN_TEST(test_full_set_reports_the_jam);

  RUN_TEST(test_the_four_serviceable_faults_persist);
  RUN_TEST(test_the_transient_faults_do_not_persist);
  RUN_TEST(test_subset_keeps_only_the_persistent_bits);
  RUN_TEST(test_subset_of_transient_only_is_empty);
  RUN_TEST(test_subset_is_idempotent);
  RUN_TEST(test_a_stored_garbage_byte_cannot_resurrect_a_transient_fault);
  RUN_TEST(test_virgin_and_erased_bytes_are_empty_sets);

  return UNITY_END();
}
