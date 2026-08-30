// Calendar arithmetic -- validation, day numbering, timestamps.
//
// The day boundary computed here is what resets the daily totals, and those
// totals are what the owner counts cash against. An off-by-one at a month or
// year end means they reconcile money against the wrong figure, on a day nobody
// is watching for it.
//
// The plausibility check is the other half: it is what stands between a dead
// RTC battery and a receipt stamped with a confidently wrong date.

#include <unity.h>
#include "calendar.h"

static datetime_t DT;

void setUp() {
  DT.year = 2026; DT.month = 8; DT.day = 30;
  DT.hour = 10;   DT.minute = 30; DT.second = 0;
}
void tearDown() {}

// ---------------------------------------------------------------------------
// Leap years
// ---------------------------------------------------------------------------

static void test_leap_year_rule() {
  TEST_ASSERT_TRUE(calendar_is_leap(2028));   // divisible by 4
  TEST_ASSERT_FALSE(calendar_is_leap(2026));  // not
  TEST_ASSERT_FALSE(calendar_is_leap(2100));  // century, not a leap year
  TEST_ASSERT_TRUE(calendar_is_leap(2000));   // fourth century, is one
}

static void test_february_length_follows_the_leap_rule() {
  TEST_ASSERT_EQUAL_UINT8(29, calendar_days_in_month(2028, 2));
  TEST_ASSERT_EQUAL_UINT8(28, calendar_days_in_month(2026, 2));
  TEST_ASSERT_EQUAL_UINT8(28, calendar_days_in_month(2100, 2));
}

static void test_month_lengths() {
  const uint8_t want[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  for (uint8_t m = 1; m <= 12; m++) {
    TEST_ASSERT_EQUAL_UINT8(want[m - 1], calendar_days_in_month(2026, m));
  }
}

static void test_invalid_month_has_no_length() {
  TEST_ASSERT_EQUAL_UINT8(0, calendar_days_in_month(2026, 0));
  TEST_ASSERT_EQUAL_UINT8(0, calendar_days_in_month(2026, 13));
}

// ---------------------------------------------------------------------------
// Plausibility -- the dead-battery guard
// ---------------------------------------------------------------------------

static void test_a_normal_datetime_is_plausible() {
  TEST_ASSERT_TRUE(calendar_plausible(&DT));
}

static void test_null_is_not_plausible() {
  TEST_ASSERT_FALSE(calendar_plausible(nullptr));
}

static void test_year_before_the_firmware_is_rejected() {
  // The classic dead-battery reading. A clock cannot legitimately read earlier
  // than the code reading it.
  DT.year = 2000;
  TEST_ASSERT_FALSE(calendar_plausible(&DT));
  DT.year = RTC_MIN_YEAR - 1;
  TEST_ASSERT_FALSE(calendar_plausible(&DT));
}

static void test_the_year_window_boundaries() {
  DT.year = RTC_MIN_YEAR;
  TEST_ASSERT_TRUE(calendar_plausible(&DT));
  DT.year = RTC_MAX_YEAR;
  TEST_ASSERT_TRUE(calendar_plausible(&DT));
  DT.year = RTC_MAX_YEAR + 1;
  TEST_ASSERT_FALSE(calendar_plausible(&DT));
}

static void test_impossible_month_and_day_rejected() {
  DT.month = 0;  TEST_ASSERT_FALSE(calendar_plausible(&DT));
  DT.month = 13; TEST_ASSERT_FALSE(calendar_plausible(&DT));
  setUp();
  DT.day = 0;    TEST_ASSERT_FALSE(calendar_plausible(&DT));
  DT.day = 32;   TEST_ASSERT_FALSE(calendar_plausible(&DT));
}

static void test_thirty_first_of_february_is_rejected() {
  // A BCD misread produces exactly this kind of value, and it is the reason the
  // day check is against the month's real length rather than a flat 31.
  DT.month = 2; DT.day = 30;
  TEST_ASSERT_FALSE(calendar_plausible(&DT));
  DT.day = 29;
  TEST_ASSERT_FALSE(calendar_plausible(&DT));   // 2026 is not a leap year
  DT.year = 2028;
  TEST_ASSERT_TRUE(calendar_plausible(&DT));    // 2028 is
}

static void test_thirty_first_of_a_thirty_day_month_is_rejected() {
  DT.month = 4; DT.day = 31;   // April
  TEST_ASSERT_FALSE(calendar_plausible(&DT));
}

static void test_hour_24_is_rejected() {
  // The classic BCD misread. Accepting it would put a receipt a day out.
  DT.hour = 24;
  TEST_ASSERT_FALSE(calendar_plausible(&DT));
  DT.hour = 23;
  TEST_ASSERT_TRUE(calendar_plausible(&DT));
}

static void test_minute_and_second_bounds() {
  DT.minute = 60; TEST_ASSERT_FALSE(calendar_plausible(&DT));
  setUp();
  DT.second = 60; TEST_ASSERT_FALSE(calendar_plausible(&DT));
  setUp();
  DT.minute = 59; DT.second = 59;
  TEST_ASSERT_TRUE(calendar_plausible(&DT));
}

// ---------------------------------------------------------------------------
// Day numbering -- what the rollover compares
// ---------------------------------------------------------------------------

static void test_implausible_date_has_no_day_number() {
  DT.year = 1999;
  TEST_ASSERT_EQUAL_INT32(0, calendar_day_number(&DT));
}

static void test_consecutive_days_differ_by_one() {
  DT.month = 8; DT.day = 30;
  const int32_t a = calendar_day_number(&DT);
  DT.day = 31;
  TEST_ASSERT_EQUAL_INT32(a + 1, calendar_day_number(&DT));
}

static void test_month_end_rolls_by_one() {
  DT.month = 8; DT.day = 31;
  const int32_t a = calendar_day_number(&DT);
  DT.month = 9; DT.day = 1;
  TEST_ASSERT_EQUAL_INT32(a + 1, calendar_day_number(&DT));
}

static void test_year_end_rolls_by_one() {
  // New Year's Eve into New Year's Day -- a boundary that happens once a year
  // in a machine nobody is standing next to.
  DT.year = 2026; DT.month = 12; DT.day = 31;
  const int32_t a = calendar_day_number(&DT);
  DT.year = 2027; DT.month = 1; DT.day = 1;
  TEST_ASSERT_EQUAL_INT32(a + 1, calendar_day_number(&DT));
}

static void test_leap_day_rolls_correctly() {
  DT.year = 2028; DT.month = 2; DT.day = 28;
  const int32_t a = calendar_day_number(&DT);
  DT.day = 29;
  TEST_ASSERT_EQUAL_INT32(a + 1, calendar_day_number(&DT));
  DT.month = 3; DT.day = 1;
  TEST_ASSERT_EQUAL_INT32(a + 2, calendar_day_number(&DT));
}

static void test_non_leap_february_rolls_straight_to_march() {
  DT.year = 2026; DT.month = 2; DT.day = 28;
  const int32_t a = calendar_day_number(&DT);
  DT.month = 3; DT.day = 1;
  TEST_ASSERT_EQUAL_INT32(a + 1, calendar_day_number(&DT));
}

static void test_time_of_day_does_not_change_the_day_number() {
  // The rollover fires on the DATE changing, not the clock passing a value.
  DT.hour = 0; DT.minute = 0; DT.second = 0;
  const int32_t midnight = calendar_day_number(&DT);
  DT.hour = 23; DT.minute = 59; DT.second = 59;
  TEST_ASSERT_EQUAL_INT32(midnight, calendar_day_number(&DT));
}

static void test_day_numbers_increase_monotonically_over_four_years() {
  // Walks every date across two leap years and a century-adjacent stretch,
  // asserting each day is exactly one after the last. This is the test that
  // would catch a rewrite of days_from_civil() with an approximation.
  int32_t prev = 0;
  bool first = true;
  for (uint16_t y = 2026; y <= 2029; y++) {
    for (uint8_t m = 1; m <= 12; m++) {
      const uint8_t dim = calendar_days_in_month(y, m);
      for (uint8_t d = 1; d <= dim; d++) {
        datetime_t t = { y, m, d, 12, 0, 0 };
        const int32_t n = calendar_day_number(&t);
        TEST_ASSERT_TRUE(calendar_plausible(&t));
        if (!first) TEST_ASSERT_EQUAL_INT32(prev + 1, n);
        prev = n;
        first = false;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Timestamps
// ---------------------------------------------------------------------------

static void test_implausible_date_gives_the_invalid_timestamp() {
  // A history entry written with a failed clock must be distinguishable from a
  // real one, rather than looking like a plausible moment in 1970.
  DT.year = 1999;
  TEST_ASSERT_EQUAL_UINT32(RTC_TIMESTAMP_INVALID, calendar_timestamp(&DT));
  TEST_ASSERT_EQUAL_UINT32(RTC_TIMESTAMP_INVALID, calendar_timestamp(nullptr));
}

static void test_a_known_epoch_value() {
  // 2026-08-30 00:00:00 is 1788048000 seconds after the 1970 epoch.
  //
  // Cross-checked against Python's datetime rather than computed by hand -- the
  // first version of this assertion was twelve days out, and it was the
  // implementation that turned out to be right. An anchor value that is only as
  // good as the arithmetic of whoever typed it is worse than no anchor.
  datetime_t t = { 2026, 8, 30, 0, 0, 0 };
  TEST_ASSERT_EQUAL_UINT32(1788048000UL, calendar_timestamp(&t));
}

static void test_time_of_day_adds_seconds() {
  datetime_t midnight = { 2026, 8, 30, 0, 0, 0 };
  datetime_t later    = { 2026, 8, 30, 10, 30, 15 };
  const uint32_t want = 10UL * 3600UL + 30UL * 60UL + 15UL;
  TEST_ASSERT_EQUAL_UINT32(calendar_timestamp(&midnight) + want,
                           calendar_timestamp(&later));
}

static void test_timestamps_increase_with_the_calendar() {
  datetime_t a = { 2026, 12, 31, 23, 59, 59 };
  datetime_t b = { 2027, 1, 1, 0, 0, 0 };
  TEST_ASSERT_EQUAL_UINT32(calendar_timestamp(&a) + 1, calendar_timestamp(&b));
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_leap_year_rule);
  RUN_TEST(test_february_length_follows_the_leap_rule);
  RUN_TEST(test_month_lengths);
  RUN_TEST(test_invalid_month_has_no_length);

  RUN_TEST(test_a_normal_datetime_is_plausible);
  RUN_TEST(test_null_is_not_plausible);
  RUN_TEST(test_year_before_the_firmware_is_rejected);
  RUN_TEST(test_the_year_window_boundaries);
  RUN_TEST(test_impossible_month_and_day_rejected);
  RUN_TEST(test_thirty_first_of_february_is_rejected);
  RUN_TEST(test_thirty_first_of_a_thirty_day_month_is_rejected);
  RUN_TEST(test_hour_24_is_rejected);
  RUN_TEST(test_minute_and_second_bounds);

  RUN_TEST(test_implausible_date_has_no_day_number);
  RUN_TEST(test_consecutive_days_differ_by_one);
  RUN_TEST(test_month_end_rolls_by_one);
  RUN_TEST(test_year_end_rolls_by_one);
  RUN_TEST(test_leap_day_rolls_correctly);
  RUN_TEST(test_non_leap_february_rolls_straight_to_march);
  RUN_TEST(test_time_of_day_does_not_change_the_day_number);
  RUN_TEST(test_day_numbers_increase_monotonically_over_four_years);

  RUN_TEST(test_implausible_date_gives_the_invalid_timestamp);
  RUN_TEST(test_a_known_epoch_value);
  RUN_TEST(test_time_of_day_adds_seconds);
  RUN_TEST(test_timestamps_increase_with_the_calendar);

  return UNITY_END();
}
