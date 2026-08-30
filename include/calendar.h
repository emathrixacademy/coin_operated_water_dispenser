#ifndef CALENDAR_H
#define CALENDAR_H

// Calendar arithmetic -- validation, day numbering, timestamps.
//
// NO Arduino dependency, so it links into the host-side unit tests. Leap years,
// month lengths and day-boundary arithmetic are exactly the kind of logic that
// is quietly wrong at the edges and impossible to notice until a February or a
// year end, by which time the machine has been in service for months.
//
// The day boundary computed here is what resets the daily totals, and those
// totals are what the owner counts money against. Getting it wrong by a day
// means the owner reconciles cash against the wrong figure.

#include "types.h"

bool calendar_is_leap(uint16_t year);

// Days in a month, 1-12. Returns 0 for an invalid month.
uint8_t calendar_days_in_month(uint16_t year, uint8_t month);

// Is this a real date and time the machine is willing to trust?
//
// Rejects impossible field values (month 13, 31 February, hour 24) and dates
// outside [RTC_MIN_YEAR, RTC_MAX_YEAR]. A clock reading earlier than the
// firmware that reads it is a dead battery or a bad part, not a valid time.
//
// A false return means SHOW A CLOCK-NOT-SET STATE. It never means "use it
// anyway" -- a receipt stamped with a confidently wrong date is worse than one
// that admits it does not know.
bool calendar_plausible(const datetime_t *dt);

// Days since 1970-01-01. Monotonic across months and years, so a change in this
// value IS the day boundary. Returns 0 for an implausible date.
int32_t calendar_day_number(const datetime_t *dt);

// Seconds since 1970-01-01 00:00:00, local time, no timezone handling.
// Returns RTC_TIMESTAMP_INVALID for an implausible date so a history entry
// written with a failed clock is distinguishable from a real one.
uint32_t calendar_timestamp(const datetime_t *dt);

#endif  // CALENDAR_H
