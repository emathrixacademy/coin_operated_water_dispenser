#ifndef WATER_LEVEL_H
#define WATER_LEVEL_H

// Water level -- three floats, pump control, lockout.
//
// Two floats in the cold tank drive the pump: mid turns it on, high turns it
// off. The third float sits in the gallon bay.
//
// THE GALLON BAY FLOAT IS A SAFETY LOCKOUT, NOT PUMP CONTROL.
//
// If the gallon float reads empty the pump must not run, no matter what the
// cold tank floats say. Running the pump dry destroys it, and this is the
// single most likely way to kill the machine in service. The inhibit is
// unconditional and is checked at the point the pump output is written, not
// only at the point the decision is made -- see scenarios.md case 15.
//
// All three floats are debounced over FLOAT_DEBOUNCE_MS. Water sloshes,
// especially during a pour, and an undebounced float chatters the pump relay.

#include "types.h"

// Cold tank level for the System Status screen and the three level LEDs.
enum water_level_t : uint8_t {
  WATER_LEVEL_LOW = 0,   // below the mid float
  WATER_LEVEL_MID,       // at or above mid, not yet at high
  WATER_LEVEL_HIGH       // at the high float -- tank full
};

void water_level_begin();
void water_level_update();

// Debounced float states.
bool water_tank_below_mid();
bool water_tank_at_high();
bool water_gallon_empty();

// Cold tank level as a single value, for the status screen.
water_level_t water_tank_level();

// True when the gallon bay is empty. The machine locks with OUT OF WATER and
// the acceptor is disabled -- it cannot guarantee it can serve the next user.
bool water_is_locked_out();

bool water_pump_running();

// Absolute run ceiling. A pump that has run PUMP_MAX_RUN_MS without reaching
// the high float is pumping air, pumping against a blockage, or looking at a
// failed float. All three destroy it. Raises FAULT_PUMP_RUNTIME.
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
