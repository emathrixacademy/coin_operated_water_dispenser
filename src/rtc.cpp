#include <Arduino.h>
#include <Wire.h>

#include "rtc.h"
#include "calendar.h"

// DS3231 real-time clock.
//
// A deliberately minimal direct-register driver rather than a library. The
// DS3231's time registers are seven consecutive BCD bytes and the only other
// register that matters is the status byte, so a dependency would be more code
// than this, not less -- and this way there is no third-party allocation
// behaviour to audit against the no-dynamic-allocation rule.

// Register map. Only the ones this firmware touches.
#define REG_SECONDS 0x00
#define REG_STATUS  0x0F

// Status register bit 7: Oscillator Stop Flag.
//
// The DS3231 sets this whenever the oscillator has stopped since the flag was
// last cleared -- a flat battery, a brownout, or a first power-up. It is the
// chip telling us the elapsed time is unknown, which is exactly the question
// this module has to answer, and it is more reliable than inferring it from the
// values. Cleared only by rtc_set(), so it survives power cycles until a human
// actually sets the clock.
#define STATUS_OSF 0x80

static datetime_t s_now;
static bool s_ok = false;

static uint32_t s_last_read_ms = 0;

// Day-boundary tracking. s_day is 0 until the first valid reading, which is
// what stops a rollover firing on the first read after boot: a machine that was
// switched off over midnight has no open day to close.
static int32_t s_day = 0;
static bool s_day_rolled = false;

static uint8_t bcd_to_bin(uint8_t v) {
  return (uint8_t)((v & 0x0F) + 10u * ((v >> 4) & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t v) {
  return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}

static bool read_regs(uint8_t start, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(start);
  if (Wire.endTransmission() != 0) return false;

  if (Wire.requestFrom((uint8_t)RTC_I2C_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

static bool write_regs(uint8_t start, const uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(RTC_I2C_ADDR);
  Wire.write(start);
  for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
  return Wire.endTransmission() == 0;
}

// Read the clock once. Leaves s_ok false and s_now untouched on any failure.
static void read_clock() {
  uint8_t status;
  if (!read_regs(REG_STATUS, &status, 1)) {
    s_ok = false;
    return;
  }

  // The chip itself says the time is unknown. Trust it over the register
  // values, which will still contain something that looks like a date.
  if (status & STATUS_OSF) {
    s_ok = false;
    return;
  }

  uint8_t r[7];
  if (!read_regs(REG_SECONDS, r, 7)) {
    s_ok = false;
    return;
  }

  datetime_t dt;
  dt.second = bcd_to_bin(r[0] & 0x7F);
  dt.minute = bcd_to_bin(r[1] & 0x7F);
  // Bit 6 set means 12-hour mode. This firmware only ever writes 24-hour, so a
  // device in 12-hour mode was set by something else and is not trusted --
  // misreading it would put every afternoon receipt twelve hours out.
  if (r[2] & 0x40) {
    s_ok = false;
    return;
  }
  dt.hour  = bcd_to_bin(r[2] & 0x3F);
  // r[3] is day-of-week, which this firmware does not use: it is redundant with
  // the date and is a field the chip will happily let a human set wrong.
  dt.day   = bcd_to_bin(r[4] & 0x3F);
  dt.month = bcd_to_bin(r[5] & 0x1F);
  // Bit 7 of the month register is the century flag. The year window in
  // calendar_plausible() already rejects anything outside 2026-2099, so a
  // century rollover cannot be silently misread -- it fails the plausibility
  // check instead.
  dt.year  = (uint16_t)(2000u + bcd_to_bin(r[6]));

  if (!calendar_plausible(&dt)) {
    s_ok = false;
    return;
  }

  s_now = dt;
  s_ok = true;

  // Day boundary. Compared against the last VALID reading, so a spell of failed
  // reads across midnight still produces exactly one rollover when the clock
  // comes back.
  const int32_t today = calendar_day_number(&dt);
  if (s_day != 0 && today != s_day) {
    s_day_rolled = true;
  }
  s_day = today;
}

void rtc_begin() {
  Wire.begin();

  // ---------------------------------------------------------------------
  // BOUND THE BUS BEFORE THE FIRST TRANSFER.
  //
  // Wire spins on the TWI flag with no timeout by default, and rtc_update()
  // runs in loop(). A DS3231 that fails with SDA held low would otherwise stop
  // the entire machine -- no coin pulses, no flow counting, no hopper polling
  // -- with a user's money inside it. This one line is what makes the clock a
  // component that can fail rather than a component that can kill the machine.
  //
  // The `true` resets the TWI hardware on expiry. Without it the peripheral
  // stays wedged and a momentary glitch becomes a permanently dead clock.
  // ---------------------------------------------------------------------
  Wire.setWireTimeout(RTC_I2C_TIMEOUT_US, true);

  s_ok = false;
  s_day = 0;
  s_day_rolled = false;
  read_clock();
  s_last_read_ms = millis();
}

void rtc_update() {
  const uint32_t now = millis();
  if ((uint32_t)(now - s_last_read_ms) < RTC_READ_INTERVAL_MS) return;
  s_last_read_ms = now;
  read_clock();
}

bool rtc_ok() {
  return s_ok;
}

const datetime_t *rtc_now() {
  return s_ok ? &s_now : nullptr;
}

uint32_t rtc_timestamp() {
  if (!s_ok) return RTC_TIMESTAMP_INVALID;
  return calendar_timestamp(&s_now);
}

bool rtc_day_rolled() {
  const bool r = s_day_rolled;
  s_day_rolled = false;
  return r;
}

int32_t rtc_day_number() {
  return s_ok ? s_day : 0;
}

bool rtc_set(const datetime_t *dt) {
  // Refuse an implausible datetime rather than writing it. A mistyped Admin
  // entry must not be able to put the machine into a state where it believes a
  // wrong date -- that is the failure this whole module exists to prevent.
  if (!calendar_plausible(dt)) return false;

  uint8_t r[7];
  r[0] = bin_to_bcd(dt->second);
  r[1] = bin_to_bcd(dt->minute);
  r[2] = bin_to_bcd(dt->hour);          // bit 6 clear = 24-hour mode
  r[3] = 1;                              // day-of-week, unused but must be 1-7
  r[4] = bin_to_bcd(dt->day);
  r[5] = bin_to_bcd(dt->month);          // century bit clear
  r[6] = bin_to_bcd((uint8_t)(dt->year - 2000u));

  if (!write_regs(REG_SECONDS, r, 7)) return false;

  // Clear the oscillator-stop flag LAST, and only after the time landed. If the
  // write above failed, the flag stays set and the clock keeps reporting itself
  // as untrustworthy, which is the correct outcome.
  uint8_t status;
  if (!read_regs(REG_STATUS, &status, 1)) return false;
  status &= (uint8_t)~STATUS_OSF;
  if (!write_regs(REG_STATUS, &status, 1)) return false;

  // Re-read so s_now and the day number reflect what was actually stored rather
  // than what was requested.
  read_clock();

  // Setting the clock is not a midnight rollover. Suppress any edge the jump
  // produced -- closing a "day" here would write a spurious history entry and
  // zero totals the operator was mid-way through reading.
  s_day_rolled = false;

  return s_ok;
}
