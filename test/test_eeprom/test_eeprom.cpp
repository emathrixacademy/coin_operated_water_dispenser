// Host-side tests for the EEPROM record framing.
//
// The case that matters most here is the first boot on a virgin chip. Every
// AVR EEPROM cell reads 0xFF from the factory. Without framing, an inventory
// record read from a fresh chip is 65535 coins in each hopper, and the machine
// confidently believes it can make change it does not physically have -- which
// is a jam under the first paying user.
//
// These run off-target so that path is actually exercised, rather than being
// something we assert about hardware we cannot easily put into that state twice.

#include <unity.h>
#include <string.h>
#include "eeprom_record.h"
#include "types.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// The virgin-cell case
// ---------------------------------------------------------------------------

static void test_virgin_eeprom_is_rejected() {
  // A factory-fresh chip: every byte 0xFF.
  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  memset(buf, 0xFF, sizeof(buf));

  inventory_t inv;
  memset(&inv, 0, sizeof(inv));

  TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&inv, sizeof(inv)));

  // ...and the payload must be untouched, NOT filled with 0xFF. This is the
  // whole point: a rejected record must not leave 65535 coins in the mirror.
  TEST_ASSERT_EQUAL_UINT16(0, inv.p1_count);
  TEST_ASSERT_EQUAL_UINT16(0, inv.p5_count);
}

static void test_erased_eeprom_is_rejected() {
  // An all-zeros region, e.g. after a bulk erase tool.
  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  memset(buf, 0x00, sizeof(buf));

  inventory_t inv;
  inv.p1_count = 1234;
  TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&inv, sizeof(inv)));
  TEST_ASSERT_EQUAL_UINT16(1234, inv.p1_count);  // untouched
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

static void test_round_trip_preserves_payload() {
  inventory_t src;
  src.p1_count = 100;
  src.p5_count = 42;
  src.profit_p10 = 7;
  src.profit_p20 = 3;

  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));

  inventory_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_TRUE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));

  TEST_ASSERT_EQUAL_UINT16(100, dst.p1_count);
  TEST_ASSERT_EQUAL_UINT16(42, dst.p5_count);
  TEST_ASSERT_EQUAL_UINT16(7, dst.profit_p10);
  TEST_ASSERT_EQUAL_UINT16(3, dst.profit_p20);
}

static void test_round_trip_of_an_all_zero_payload() {
  // A legitimately zeroed inventory must survive, and must be distinguishable
  // from an erased chip. This is why the magic word is framing rather than a
  // property of the payload.
  inventory_t src;
  memset(&src, 0, sizeof(src));

  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));

  inventory_t dst;
  memset(&dst, 0xAB, sizeof(dst));
  TEST_ASSERT_TRUE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));
  TEST_ASSERT_EQUAL_UINT16(0, dst.p1_count);
}

// ---------------------------------------------------------------------------
// Corruption detection
// ---------------------------------------------------------------------------

static void test_corrupt_payload_is_rejected() {
  inventory_t src;
  src.p1_count = 100;
  src.p5_count = 100;
  src.profit_p10 = 0;
  src.profit_p20 = 0;

  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));

  // A degraded cell flips a bit in the payload.
  buf[RECORD_OVERHEAD] ^= 0x01;

  inventory_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));
}

static void test_every_single_bit_flip_in_payload_is_caught() {
  // CRC-8 catches all single-bit errors. Verify that across the whole payload
  // rather than trusting the property -- this is the inventory record.
  inventory_t src;
  src.p1_count = 100;
  src.p5_count = 55;
  src.profit_p10 = 12;
  src.profit_p20 = 9;

  uint8_t good[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(good, (const uint8_t *)&src, sizeof(src));

  for (unsigned i = 0; i < sizeof(inventory_t); i++) {
    for (unsigned b = 0; b < 8; b++) {
      uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
      memcpy(buf, good, sizeof(buf));
      buf[RECORD_OVERHEAD + i] ^= (uint8_t)(1u << b);

      inventory_t dst;
      memset(&dst, 0, sizeof(dst));
      TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));
    }
  }
}

static void test_corrupt_magic_is_rejected() {
  inventory_t src;
  memset(&src, 0, sizeof(src));
  src.p1_count = 100;

  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));
  buf[0] ^= 0xFF;

  inventory_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));
}

static void test_wrong_layout_version_is_rejected() {
  // A record written by firmware with a different struct layout. The magic
  // matches, so only the version check stands between us and misreading every
  // field in the inventory.
  inventory_t src;
  memset(&src, 0, sizeof(src));
  src.p1_count = 100;

  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));
  buf[2] = (uint8_t)(EEPROM_LAYOUT_VERSION + 1);

  inventory_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));
}

// ---------------------------------------------------------------------------
// CRC basics
// ---------------------------------------------------------------------------

static void test_crc_is_deterministic() {
  const uint8_t data[] = {1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL_UINT8(record_crc8(data, sizeof(data)),
                          record_crc8(data, sizeof(data)));
}

static void test_crc_differs_for_different_data() {
  const uint8_t a[] = {1, 2, 3};
  const uint8_t b[] = {1, 2, 4};
  TEST_ASSERT_NOT_EQUAL(record_crc8(a, sizeof(a)), record_crc8(b, sizeof(b)));
}

static void test_crc_detects_transposition() {
  // A plain checksum would miss this; the CRC must not.
  const uint8_t a[] = {0x12, 0x34};
  const uint8_t b[] = {0x34, 0x12};
  TEST_ASSERT_NOT_EQUAL(record_crc8(a, sizeof(a)), record_crc8(b, sizeof(b)));
}

// ---------------------------------------------------------------------------
// The transaction record, since it carries the user's money across a power cut
// ---------------------------------------------------------------------------

static void test_transaction_round_trip() {
  transaction_t src;
  memset(&src, 0, sizeof(src));
  src.credit = 1500;
  src.inserted = 2000;
  src.target_ml = 2000;
  src.dispensed_ml = 305;
  src.total_ml = 0;
  src.open = true;

  uint8_t buf[RECORD_OVERHEAD + sizeof(transaction_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));

  transaction_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_TRUE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));

  TEST_ASSERT_EQUAL_INT32(1500, dst.credit);
  TEST_ASSERT_EQUAL_INT32(2000, dst.inserted);
  TEST_ASSERT_EQUAL_INT32(2000, dst.target_ml);
  TEST_ASSERT_EQUAL_INT32(305, dst.dispensed_ml);
  TEST_ASSERT_TRUE(dst.open);
}

static void test_virgin_transaction_does_not_resume() {
  // A virgin chip must not look like an open transaction with a huge balance.
  uint8_t buf[RECORD_OVERHEAD + sizeof(transaction_t)];
  memset(buf, 0xFF, sizeof(buf));

  transaction_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));
  TEST_ASSERT_FALSE(dst.open);
  TEST_ASSERT_EQUAL_INT32(0, dst.credit);
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_virgin_eeprom_is_rejected);
  RUN_TEST(test_erased_eeprom_is_rejected);

  RUN_TEST(test_round_trip_preserves_payload);
  RUN_TEST(test_round_trip_of_an_all_zero_payload);

  RUN_TEST(test_corrupt_payload_is_rejected);
  RUN_TEST(test_every_single_bit_flip_in_payload_is_caught);
  RUN_TEST(test_corrupt_magic_is_rejected);
  RUN_TEST(test_wrong_layout_version_is_rejected);

  RUN_TEST(test_crc_is_deterministic);
  RUN_TEST(test_crc_differs_for_different_data);
  RUN_TEST(test_crc_detects_transposition);

  RUN_TEST(test_transaction_round_trip);
  RUN_TEST(test_virgin_transaction_does_not_resume);

  return UNITY_END();
}
