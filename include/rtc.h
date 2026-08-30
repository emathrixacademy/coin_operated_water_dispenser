#ifndef RTC_H
#define RTC_H

// Real-time clock -- DS3231, battery-backed, temperature-compensated.
//
// ===========================================================================
// A FAILED CLOCK REPORTS FAILURE. IT NEVER GUESSES.
// ===========================================================================
//
// If the clock is not running, has lost its oscillator, or reads back an
// implausible date, rtc_ok() is false and every timestamp is
// RTC_TIMESTAMP_INVALID. The machine shows a clock-not-set state instead of
// stamping receipts and daily totals with a date that is confidently wrong.
//
// This matters because the daily profit total is a number the owner counts
// money against. A wrong date on it is worse than no date, because there is
// nothing to tell them it is wrong.
//
// The calendar arithmetic lives in calendar.*, which has no Arduino dependency
// and is unit tested. This module is the I2C transport and the state around it.

#include "types.h"

void rtc_begin();
void rtc_update();

// False when the clock cannot be trusted: no device, oscillator stopped since
// the last time it was set, or an implausible reading. Drives the
// clock-not-set state on the display and in Admin.
bool rtc_ok();

// Current time, or nullptr when !rtc_ok(). Points at module-owned storage; the
// caller does not own it and must not modify it.
const datetime_t *rtc_now();

// Seconds since 1970-01-01 local, or RTC_TIMESTAMP_INVALID when the clock has
// failed. This is what goes into history entries.
uint32_t rtc_timestamp();

// --- Day boundary -------------------------------------------------------
//
// The midnight rollover is what resets the daily totals. It is explicit rather
// than implicit because the closing totals MUST be written to the history ring
// before they are zeroed -- otherwise the operator loses the day they were
// reading, and there is no way to recover it.
//
// One-shot, consumed by reading. Never fires while the clock is failed, and
// never fires on the first valid reading after boot -- a machine that was off
// over midnight has no day to close.
bool rtc_day_rolled();

// Days since 1970-01-01, or 0 when the clock has failed.
int32_t rtc_day_number();

// --- Admin --------------------------------------------------------------

// Set the clock. Rejects an implausible datetime rather than writing it, so a
// mistyped Admin entry cannot put the machine into a state where it believes a
// wrong date. Clears the oscillator-stop flag on success.
bool rtc_set(const datetime_t *dt);

#endif  // RTC_H
