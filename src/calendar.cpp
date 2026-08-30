// Calendar arithmetic. No Arduino dependency by design -- see calendar.h.

#include "calendar.h"

bool calendar_is_leap(uint16_t year) {
  // Every fourth year, except centuries, except every fourth century.
  // 2000 was a leap year; 1900 and 2100 are not.
  return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

uint8_t calendar_days_in_month(uint16_t year, uint8_t month) {
  static const uint8_t DAYS[] = { 31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31 };
  if (month < 1 || month > 12) return 0;
  if (month == 2 && calendar_is_leap(year)) return 29;
  return DAYS[month - 1];
}

bool calendar_plausible(const datetime_t *dt) {
  if (!dt) return false;

  // Year window first. A clock reading before the firmware was written is a
  // dead battery, a corrupt read, or a counterfeit part -- never a real time.
  if (dt->year < RTC_MIN_YEAR || dt->year > RTC_MAX_YEAR) return false;

  if (dt->month < 1 || dt->month > 12) return false;

  const uint8_t dim = calendar_days_in_month(dt->year, dt->month);
  if (dt->day < 1 || dt->day > dim) return false;

  // 24-hour clock. An hour of 24 is the classic BCD misread and must not pass.
  if (dt->hour > 23) return false;
  if (dt->minute > 59) return false;
  // 60 is rejected: this machine never sees a leap second, and accepting one
  // would mean accepting a misread register with the same value.
  if (dt->second > 59) return false;

  return true;
}

// Days from the civil calendar to the days-since-1970 epoch.
//
// Howard Hinnant's algorithm. It is exact for every date in the supported range
// and uses only integer arithmetic -- no tables, no loop over years, nothing
// that drifts. Do not replace it with a "days so far this year plus 365 times
// the year" approximation; that is where off-by-one-in-a-leap-year bugs live.
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
  y -= (m <= 2) ? 1 : 0;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = (uint32_t)(y - era * 400);                    // [0, 399]
  const uint32_t doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
  const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;     // [0, 146096]
  return era * 146097 + (int32_t)doe - 719468;
}

int32_t calendar_day_number(const datetime_t *dt) {
  if (!calendar_plausible(dt)) return 0;
  return days_from_civil((int32_t)dt->year, dt->month, dt->day);
}

uint32_t calendar_timestamp(const datetime_t *dt) {
  if (!calendar_plausible(dt)) return RTC_TIMESTAMP_INVALID;

  const int32_t days = days_from_civil((int32_t)dt->year, dt->month, dt->day);
  return (uint32_t)days * 86400UL +
         (uint32_t)dt->hour * 3600UL +
         (uint32_t)dt->minute * 60UL +
         (uint32_t)dt->second;
}
