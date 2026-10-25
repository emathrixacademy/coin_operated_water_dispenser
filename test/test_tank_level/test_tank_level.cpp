// Cold tank level -- A02YYUW framing, the median filter, and the pump / serve
// decisions.
//
// Two things are being protected here.
//
// The PUMP decision. A pump that runs on a garbage level either overflows the
// tank or empties a gallon and runs dry, and dry running destroys it. Every
// "no valid level" path must come out as pump OFF.
//
// The SERVE decision. On a timed-dispense unit nothing detects that water
// stopped coming, so a machine that sells below the serve line charges a
// customer for air.

#include <unity.h>
#include "tank_level.h"

void setUp() {}
void tearDown() {}

static bool feed_frame(a02_parser_t *p, uint8_t hi, uint8_t lo, uint16_t *mm) {
  const uint8_t sum = (uint8_t)(0xFF + hi + lo);
  bool got = false;
  got |= a02_parser_feed(p, 0xFF, mm);
  got |= a02_parser_feed(p, hi, mm);
  got |= a02_parser_feed(p, lo, mm);
  got |= a02_parser_feed(p, sum, mm);
  return got;
}

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

static void test_valid_frame_decodes_mm() {
  a02_parser_t p; a02_parser_reset(&p);
  uint16_t mm = 0;
  TEST_ASSERT_TRUE(feed_frame(&p, 0x01, 0x2C, &mm));   // 300 mm
  TEST_ASSERT_EQUAL_UINT16(300, mm);
}

static void test_bad_checksum_is_rejected() {
  a02_parser_t p; a02_parser_reset(&p);
  uint16_t mm = 1234;
  a02_parser_feed(&p, 0xFF, &mm);
  a02_parser_feed(&p, 0x00, &mm);
  a02_parser_feed(&p, 0x64, &mm);
  TEST_ASSERT_FALSE(a02_parser_feed(&p, 0x00, &mm));   // should be 0x63
  TEST_ASSERT_EQUAL_UINT16(1234, mm);                  // untouched
}

static void test_garbage_before_header_is_skipped() {
  a02_parser_t p; a02_parser_reset(&p);
  uint16_t mm = 0;
  a02_parser_feed(&p, 0x12, &mm);
  a02_parser_feed(&p, 0x34, &mm);
  TEST_ASSERT_TRUE(feed_frame(&p, 0x00, 0x50, &mm));
  TEST_ASSERT_EQUAL_UINT16(80, mm);
}

static void test_resyncs_on_ff_inside_a_bad_frame() {
  // Joined mid-stream: a stray 0xFF looks like a header, the real frame starts
  // one byte later. The real frame must still decode.
  a02_parser_t p; a02_parser_reset(&p);
  uint16_t mm = 0;
  a02_parser_feed(&p, 0xFF, &mm);              // stray
  TEST_ASSERT_TRUE(feed_frame(&p, 0x00, 0x78, &mm));
  TEST_ASSERT_EQUAL_UINT16(120, mm);
}

static void test_back_to_back_frames() {
  a02_parser_t p; a02_parser_reset(&p);
  uint16_t mm = 0;
  TEST_ASSERT_TRUE(feed_frame(&p, 0x00, 0x64, &mm));
  TEST_ASSERT_EQUAL_UINT16(100, mm);
  TEST_ASSERT_TRUE(feed_frame(&p, 0x00, 0xC8, &mm));
  TEST_ASSERT_EQUAL_UINT16(200, mm);
}

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

static void test_filter_reports_nothing_until_full() {
  tank_filter_t f; tank_filter_reset(&f);
  uint16_t mm = 0;
  for (uint8_t i = 0; i < TANK_FILTER_SAMPLES - 1; i++) {
    tank_filter_add(&f, 100);
    TEST_ASSERT_FALSE(tank_filter_median(&f, &mm));
  }
  tank_filter_add(&f, 100);
  TEST_ASSERT_TRUE(tank_filter_median(&f, &mm));
  TEST_ASSERT_EQUAL_UINT16(100, mm);
}

static void test_median_ignores_one_wild_echo() {
  tank_filter_t f; tank_filter_reset(&f);
  const uint16_t in[] = { 150, 152, 290, 151, 149 };
  for (uint8_t i = 0; i < 5; i++) tank_filter_add(&f, in[i]);
  uint16_t mm = 0;
  TEST_ASSERT_TRUE(tank_filter_median(&f, &mm));
  TEST_ASSERT_EQUAL_UINT16(151, mm);
}

static void test_filter_reset_forgets_old_level() {
  tank_filter_t f; tank_filter_reset(&f);
  for (uint8_t i = 0; i < TANK_FILTER_SAMPLES; i++) tank_filter_add(&f, 100);
  tank_filter_reset(&f);
  uint16_t mm = 0;
  TEST_ASSERT_FALSE(tank_filter_median(&f, &mm));
}

// ---------------------------------------------------------------------------
// Decisions
// ---------------------------------------------------------------------------

static void test_threshold_order_holds() {
  // If calibration ever breaks this order the hysteresis inverts.
  TEST_ASSERT_TRUE(TANK_SONAR_BLIND_MM < TANK_SONAR_FULL_MM);
  TEST_ASSERT_TRUE(TANK_SONAR_FULL_MM < TANK_SONAR_REFILL_MM);
  TEST_ASSERT_TRUE(TANK_SONAR_REFILL_MM < TANK_SONAR_MIN_SERVE_MM);
  TEST_ASSERT_TRUE(TANK_SONAR_MIN_SERVE_MM < TANK_SONAR_EMPTY_MM);
}

static void test_plausible_range() {
  TEST_ASSERT_FALSE(tank_reading_plausible(TANK_SONAR_BLIND_MM - 1));
  TEST_ASSERT_TRUE(tank_reading_plausible(TANK_SONAR_BLIND_MM));
  TEST_ASSERT_TRUE(tank_reading_plausible(TANK_SONAR_EMPTY_MM));
  TEST_ASSERT_FALSE(tank_reading_plausible(TANK_SONAR_EMPTY_MM + 1));
  TEST_ASSERT_FALSE(tank_reading_plausible(0));
}

static void test_pump_never_runs_without_a_level() {
  TEST_ASSERT_FALSE(tank_pump_want(TANK_SONAR_EMPTY_MM, false, false));
  TEST_ASSERT_FALSE(tank_pump_want(TANK_SONAR_EMPTY_MM, false, true));
}

static void test_pump_hysteresis() {
  // Below the refill line: on.
  TEST_ASSERT_TRUE(tank_pump_want(TANK_SONAR_REFILL_MM, true, false));
  // Inside the band: hold whatever it is doing.
  const uint16_t mid = (TANK_SONAR_FULL_MM + TANK_SONAR_REFILL_MM) / 2;
  TEST_ASSERT_TRUE(tank_pump_want(mid, true, true));
  TEST_ASSERT_FALSE(tank_pump_want(mid, true, false));
  // At full: off, even if running.
  TEST_ASSERT_FALSE(tank_pump_want(TANK_SONAR_FULL_MM, true, true));
}

static void test_serve_line() {
  TEST_ASSERT_TRUE(tank_can_serve(TANK_SONAR_MIN_SERVE_MM - 1, true));
  TEST_ASSERT_FALSE(tank_can_serve(TANK_SONAR_MIN_SERVE_MM, true));
  TEST_ASSERT_FALSE(tank_can_serve(TANK_SONAR_FULL_MM, false));   // blind = no sale
}

static void test_percent() {
  TEST_ASSERT_EQUAL_UINT8(100, tank_percent(TANK_SONAR_FULL_MM, true));
  TEST_ASSERT_EQUAL_UINT8(100, tank_percent(TANK_SONAR_BLIND_MM, true));
  TEST_ASSERT_EQUAL_UINT8(0, tank_percent(TANK_SONAR_EMPTY_MM, true));
  TEST_ASSERT_EQUAL_UINT8(0, tank_percent(TANK_SONAR_FULL_MM, false));
  const uint16_t half = (TANK_SONAR_FULL_MM + TANK_SONAR_EMPTY_MM) / 2;
  TEST_ASSERT_UINT8_WITHIN(1, 50, tank_percent(half, true));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_valid_frame_decodes_mm);
  RUN_TEST(test_bad_checksum_is_rejected);
  RUN_TEST(test_garbage_before_header_is_skipped);
  RUN_TEST(test_resyncs_on_ff_inside_a_bad_frame);
  RUN_TEST(test_back_to_back_frames);

  RUN_TEST(test_filter_reports_nothing_until_full);
  RUN_TEST(test_median_ignores_one_wild_echo);
  RUN_TEST(test_filter_reset_forgets_old_level);

  RUN_TEST(test_threshold_order_holds);
  RUN_TEST(test_plausible_range);
  RUN_TEST(test_pump_never_runs_without_a_level);
  RUN_TEST(test_pump_hysteresis);
  RUN_TEST(test_serve_line);
  RUN_TEST(test_percent);

  return UNITY_END();
}
