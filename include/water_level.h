#ifndef WATER_LEVEL_H
#define WATER_LEVEL_H

// Water level -- cold tank ultrasonic, two supply gallons, pump control,
// lockout.
//
// COLD TANK. An A02YYUW ultrasonic on Serial3 measures the distance down to the
// water. The fill pump keeps the tank topped up between TANK_SONAR_REFILL_MM
// and TANK_SONAR_FULL_MM. Below TANK_SONAR_MIN_SERVE_MM there is not enough for
// a full P20 pour and the machine locks with OUT OF WATER at the next STANDBY.
//
// SUPPLY GALLONS. One capacitive sensor per gallon at the reserve line. One low
// is a display warning only. BOTH LOW STOPS THE PUMP: running it dry destroys
// it, and this is the single most likely way to kill the machine in service.
// A lost tank sensor stops it too. Both interlocks are checked at the point the
// pump output is written, not only where the decision is made -- see
// scenarios.md case 15.

#include "types.h"

// Cold tank level for the System Status screen and the three level LEDs.
enum water_level_t : uint8_t {
  WATER_LEVEL_LOW = 0,   // below MIN_SERVE, or no sensor -- machine will lock
  WATER_LEVEL_MID,       // serving, pump refilling
  WATER_LEVEL_HIGH       // inside the refill band -- topped up
};

void water_level_begin();
void water_level_update();

// Cold tank.
bool water_tank_sensor_ok();          // false = no plausible frame lately
uint16_t water_tank_distance_mm();    // filtered; 0 when the sensor is not ok
uint8_t water_tank_percent();         // 0-100 for the display; 0 when not ok
water_level_t water_tank_level();

// Bench diagnostics: the last checksummed frame even when it was discarded as
// implausible, and running counts of frames and raw bytes from the sensor.
// bytes == 0 means nothing is arriving on RX3 at all (wiring or power).
uint16_t water_tank_raw_mm();
uint32_t water_tank_raw_frames();
uint32_t water_tank_raw_bytes();

// PWM variant bench diagnostics: last HIGH width, last rise-to-rise period,
// and pulses seen. pulses == 0 means nothing on PIN_TANK_SONAR_PWM.
void water_tank_pwm_diag(uint32_t *width_us, uint32_t *period_us, uint32_t *pulses);

// Times the PWM line was muted for an interrupt storm since boot. Climbing =
// the sensor wire is picking up noise; fit the RC filter.
uint16_t water_tank_storm_count();

// Supply gallons. which: 0 = A, 1 = B. "Low" = dry at the reserve line.
bool water_gallon_low(uint8_t which);
uint8_t water_gallons_low_count();    // 0, 1 or 2 -- drives the display warning

// True when the cold tank cannot cover a full P20 pour, or its sensor is lost.
// The machine locks with OUT OF WATER -- it cannot guarantee it can serve the
// next user. Checked in STANDBY only, so a paid pour always finishes.
bool water_is_locked_out();

bool water_pump_running();

// Test console only: run the fill pump for `seconds` (capped at
// MANUAL_FILL_MAX_S) regardless of the tank and gallon sensors. 0 stops it.
void water_manual_fill(uint32_t seconds);

// Pump back into the tank what a pour took out: adds ml x FILL_MS_PER_100ML of
// fill-pump run time, worked off while the pump runs. Called after every pour.
void water_refill_add(volume_t ml);
uint32_t water_refill_owed_ms();

// Test console only: tank ultrasonic trigger mode, 0-2 (see sonar_trigger()).
void water_tank_trigger_mode(uint8_t mode);

// Absolute run ceiling. A pump that has run PUMP_MAX_RUN_MS without filling the
// tank is pumping air, pumping against a blockage, or looking at a sensor that
// is lying. All three destroy it. Raises FAULT_PUMP_RUNTIME.
bool water_pump_overrun();

// Cold tank temperature in TENTHS of a degree Celsius, for the Status screen
// only. 48 means 4.8 degC. Returns TEMP_INVALID_TENTHS if the probe is
// disconnected or has not completed its first conversion.
//
// Tenths because the client mockup shows one decimal place. Integer only --
// the display formats it as value/10 and value%10, with no float in the path.
//
// NEVER gates billing or dispensing. A failed temperature probe must not be
// able to stop the machine selling water.
int16_t water_temperature_tenths_c();

// Whole degrees, for callers that do not want the decimal. Returns
// TEMP_INVALID_C when the probe has no reading.
int8_t water_temperature_c();

bool water_cooling_active();

#endif  // WATER_LEVEL_H
