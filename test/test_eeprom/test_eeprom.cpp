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
  src.box_p10 = 7;
  src.box_p20 = 3;

  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));

  inventory_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_TRUE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));

  TEST_ASSERT_EQUAL_UINT16(100, dst.p1_count);
  TEST_ASSERT_EQUAL_UINT16(42, dst.p5_count);
  TEST_ASSERT_EQUAL_UINT16(7, dst.box_p10);
  TEST_ASSERT_EQUAL_UINT16(3, dst.box_p20);
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
  src.box_p10 = 0;
  src.box_p20 = 0;

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
  src.box_p10 = 12;
  src.box_p20 = 9;

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
  src.banked_ml = 205;
  src.segment_ml = 100;
  src.total_ml = 300;
  src.phase = TXN_PHASE_POUR;
  src.leg_hopper = 1;
  src.leg_count = 3;

  uint8_t buf[RECORD_OVERHEAD + sizeof(transaction_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));

  transaction_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_TRUE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));

  TEST_ASSERT_EQUAL_INT32(1500, dst.credit);
  TEST_ASSERT_EQUAL_INT32(2000, dst.inserted);
  TEST_ASSERT_EQUAL_UINT16(2000, dst.target_ml);
  TEST_ASSERT_EQUAL_UINT16(205, dst.banked_ml);
  TEST_ASSERT_EQUAL_UINT16(100, dst.segment_ml);
  TEST_ASSERT_EQUAL_UINT16(300, dst.total_ml);
  TEST_ASSERT_EQUAL_UINT8(TXN_PHASE_POUR, dst.phase);
  TEST_ASSERT_EQUAL_UINT8(1, dst.leg_hopper);
  TEST_ASSERT_EQUAL_UINT8(3, dst.leg_count);
}

static void test_inventory_record_is_sixteen_bytes() {
  // 16 payload + 4 framing = 20, at address 16, so the fault flags start at 40.
  TEST_ASSERT_EQUAL_size_t(16, sizeof(inventory_t));
}

static void test_inventory_carries_all_five_box_counters_and_the_mode() {
  inventory_t src;
  memset(&src, 0, sizeof(src));
  src.p1_count = 115; src.p5_count = 34;
  src.box_p1 = 1; src.box_p5 = 2; src.box_p10 = 3; src.box_p20 = 4; src.box_unknown = 5;
  src.routing_mode = COIN_ROUTING_COLLECT_ALL;

  uint8_t buf[RECORD_OVERHEAD + sizeof(inventory_t)];
  record_pack(buf, (const uint8_t *)&src, sizeof(src));
  inventory_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_TRUE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));

  TEST_ASSERT_EQUAL_UINT16(1, dst.box_p1);
  TEST_ASSERT_EQUAL_UINT16(2, dst.box_p5);
  TEST_ASSERT_EQUAL_UINT16(3, dst.box_p10);
  TEST_ASSERT_EQUAL_UINT16(4, dst.box_p20);
  TEST_ASSERT_EQUAL_UINT16(5, dst.box_unknown);
  TEST_ASSERT_EQUAL_UINT8(COIN_ROUTING_COLLECT_ALL, dst.routing_mode);
}

static void test_routing_mode_values_are_frozen_and_neither_is_zero() {
  // Stored in EEPROM, so never renumbered. And neither may be zero: a zeroed
  // inventory record must not look like it was written in a valid mode.
  TEST_ASSERT_EQUAL_INT(1, COIN_ROUTING_RECIRCULATE);
  TEST_ASSERT_EQUAL_INT(2, COIN_ROUTING_COLLECT_ALL);
}

static void test_transaction_record_is_twenty_bytes() {
  // The ring arithmetic in config.h and decisions.md is built on this number:
  // 20 payload + 4 sequence + 4 framing = 28 per slot, 64 slots. A field added
  // casually shortens the ring's life without anyone deciding to.
  TEST_ASSERT_EQUAL_size_t(20, sizeof(transaction_t));
}

static void test_transaction_volume_fields_hold_the_ceiling() {
  // The volume fields are uint16_t to save EEPROM. That is only sound while
  // the per-transaction ceiling fits, with room for the sensor to over-read.
  TEST_ASSERT_TRUE(MAX_TRANSACTION_ML * 2 <= 65535L);
}

static void test_zeroed_transaction_means_no_transaction() {
  // billing_reset() and a fresh EEPROM region both produce an all-zero record.
  // That MUST read as "nothing to resume": if phase zero meant anything else,
  // every cold boot would resume a transaction that never happened.
  transaction_t t;
  memset(&t, 0, sizeof(t));
  TEST_ASSERT_EQUAL_UINT8(TXN_PHASE_NONE, t.phase);
}

static void test_phase_codes_are_frozen() {
  // These numbers are IN THE EEPROM. Renumbering them reinterprets every
  // stored transaction, so a change here must come with a layout version bump.
  TEST_ASSERT_EQUAL_UINT8(0, TXN_PHASE_NONE);
  TEST_ASSERT_EQUAL_UINT8(1, TXN_PHASE_CREDIT);
  TEST_ASSERT_EQUAL_UINT8(2, TXN_PHASE_POUR);
  TEST_ASSERT_EQUAL_UINT8(3, TXN_PHASE_PAYING);
}

// ---------------------------------------------------------------------------
// Slot classification and the R-8 rule
// ---------------------------------------------------------------------------
//
// Blank and corrupt are different facts. Blank: never written, the ring has not
// wrapped. Corrupt: written and unreadable, so something NEWER than the newest
// readable record may have existed -- and that something may have been the
// write that paid a balance out.

#define TXN_SLOT_BYTES (RECORD_OVERHEAD + sizeof(transaction_t))

static void make_valid_slot(uint8_t *buf, money_t credit, uint8_t phase) {
  transaction_t t;
  memset(&t, 0, sizeof(t));
  t.credit = credit;
  t.phase = phase;
  record_pack(buf, (const uint8_t *)&t, sizeof(t));
}

static void test_virgin_slot_classifies_blank() {
  uint8_t buf[TXN_SLOT_BYTES];
  memset(buf, 0xFF, sizeof(buf));
  TEST_ASSERT_EQUAL(SLOT_BLANK, record_classify(buf, sizeof(transaction_t)));
}

static void test_good_slot_classifies_valid() {
  uint8_t buf[TXN_SLOT_BYTES];
  make_valid_slot(buf, 1500, TXN_PHASE_CREDIT);
  TEST_ASSERT_EQUAL(SLOT_VALID, record_classify(buf, sizeof(transaction_t)));
}

static void test_classify_agrees_with_unpack_on_every_bit_flip() {
  // record_classify() repeats record_unpack()'s checks without the copy. If
  // the two ever disagree, boot would trust a slot it cannot actually read.
  uint8_t good[TXN_SLOT_BYTES];
  make_valid_slot(good, 1900, TXN_PHASE_PAYING);

  for (size_t byte = 0; byte < sizeof(good); byte++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t buf[TXN_SLOT_BYTES];
      memcpy(buf, good, sizeof(buf));
      buf[byte] ^= (uint8_t)(1u << bit);

      transaction_t out;
      const bool readable = record_unpack(buf, (uint8_t *)&out, sizeof(out));
      const slot_kind_t kind = record_classify(buf, sizeof(transaction_t));
      TEST_ASSERT_EQUAL(readable ? SLOT_VALID : SLOT_CORRUPT, kind);
    }
  }
}

static void test_torn_write_classifies_corrupt_not_blank() {
  // A write interrupted by the power cut: the first bytes are new, the rest
  // are still whatever the slot held. Here the slot was virgin, so the tail is
  // 0xFF -- and it must still NOT read as blank, or the lost write is missed.
  uint8_t buf[TXN_SLOT_BYTES];
  make_valid_slot(buf, 1500, TXN_PHASE_NONE);
  for (size_t i = 10; i < sizeof(buf); i++) buf[i] = 0xFF;
  TEST_ASSERT_EQUAL(SLOT_CORRUPT, record_classify(buf, sizeof(transaction_t)));
}

static void test_erased_slot_classifies_corrupt() {
  // All zeros is not a virgin AVR cell. Something wrote it.
  uint8_t buf[TXN_SLOT_BYTES];
  memset(buf, 0x00, sizeof(buf));
  TEST_ASSERT_EQUAL(SLOT_CORRUPT, record_classify(buf, sizeof(transaction_t)));
}

static void test_other_layout_version_classifies_corrupt() {
  uint8_t buf[TXN_SLOT_BYTES];
  make_valid_slot(buf, 1500, TXN_PHASE_CREDIT);
  buf[2] = (uint8_t)(EEPROM_LAYOUT_VERSION - 1);
  TEST_ASSERT_EQUAL(SLOT_CORRUPT, record_classify(buf, sizeof(transaction_t)));
}

static void test_newest_is_trusted_when_ring_has_not_wrapped() {
  TEST_ASSERT_TRUE(ring_newest_trusted(SLOT_BLANK));
}

static void test_newest_is_trusted_when_successor_is_an_older_record() {
  TEST_ASSERT_TRUE(ring_newest_trusted(SLOT_VALID));
}

static void test_newest_is_NOT_trusted_when_successor_is_corrupt() {
  // The theft route R-8 closes: the write that closed a paid-out transaction
  // was torn, the slot before it still says "open, with credit", and the
  // machine would offer that credit to whoever is standing there.
  TEST_ASSERT_FALSE(ring_newest_trusted(SLOT_CORRUPT));
}

static void test_virgin_transaction_does_not_resume() {
  // A virgin chip must not look like an open transaction with a huge balance.
  uint8_t buf[RECORD_OVERHEAD + sizeof(transaction_t)];
  memset(buf, 0xFF, sizeof(buf));

  transaction_t dst;
  memset(&dst, 0, sizeof(dst));
  TEST_ASSERT_FALSE(record_unpack(buf, (uint8_t *)&dst, sizeof(dst)));
  TEST_ASSERT_EQUAL_UINT8(TXN_PHASE_NONE, dst.phase);
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
  RUN_TEST(test_inventory_record_is_sixteen_bytes);
  RUN_TEST(test_inventory_carries_all_five_box_counters_and_the_mode);
  RUN_TEST(test_routing_mode_values_are_frozen_and_neither_is_zero);
  RUN_TEST(test_transaction_record_is_twenty_bytes);
  RUN_TEST(test_transaction_volume_fields_hold_the_ceiling);
  RUN_TEST(test_zeroed_transaction_means_no_transaction);
  RUN_TEST(test_phase_codes_are_frozen);

  RUN_TEST(test_virgin_slot_classifies_blank);
  RUN_TEST(test_good_slot_classifies_valid);
  RUN_TEST(test_classify_agrees_with_unpack_on_every_bit_flip);
  RUN_TEST(test_torn_write_classifies_corrupt_not_blank);
  RUN_TEST(test_erased_slot_classifies_corrupt);
  RUN_TEST(test_other_layout_version_classifies_corrupt);
  RUN_TEST(test_newest_is_trusted_when_ring_has_not_wrapped);
  RUN_TEST(test_newest_is_trusted_when_successor_is_an_older_record);
  RUN_TEST(test_newest_is_NOT_trusted_when_successor_is_corrupt);

  return UNITY_END();
}
