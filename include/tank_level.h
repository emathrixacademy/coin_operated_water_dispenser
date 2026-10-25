#ifndef TANK_LEVEL_H
#define TANK_LEVEL_H

// Cold tank level from the A02YYUW ultrasonic sensor -- frame parsing,
// filtering and the pump / serve decisions.
//
// Deliberately NO Arduino dependency. This is the logic that decides whether
// the fill pump runs and whether the machine may take money for water, so it
// links into the host-side unit tests. water_level.cpp owns the UART and the
// pins; this file only does arithmetic on the numbers it is handed.
//
// Every distance here is millimetres from the sensor face DOWN to the water
// surface. A LARGER number means LESS water.

#include <stdint.h>
#include "config.h"

// ---------------------------------------------------------------------------
// A02YYUW UART frame: 0xFF, high byte, low byte, checksum. 9600 8N1.
// Checksum = (0xFF + high + low) & 0xFF. Distance in mm = high * 256 + low.
// ---------------------------------------------------------------------------

struct a02_parser_t {
  uint8_t buf[4];
  uint8_t len;
};

void a02_parser_reset(a02_parser_t *p);

// Feed one received byte. Returns true and writes *out_mm when this byte
// completes a frame whose checksum is valid. A bad checksum resynchronises on
// the next 0xFF already in the buffer rather than dropping three bytes, so one
// corrupted byte costs one frame, not two.
bool a02_parser_feed(a02_parser_t *p, uint8_t b, uint16_t *out_mm);

// ---------------------------------------------------------------------------
// Median filter over the last TANK_FILTER_SAMPLES readings.
//
// Median, not mean: one echo off a ripple or the pump's inlet stream is a wild
// value, and a mean drags the level with it while a median ignores it.
// ---------------------------------------------------------------------------

struct tank_filter_t {
  uint16_t s[TANK_FILTER_SAMPLES];
  uint8_t  count;   // valid samples held, up to TANK_FILTER_SAMPLES
  uint8_t  head;    // next slot to overwrite
};

void tank_filter_reset(tank_filter_t *f);
void tank_filter_add(tank_filter_t *f, uint16_t mm);

// Median of what is held. Returns false (and leaves *out_mm alone) until the
// filter is full -- a level decided from one or two echoes is a guess.
bool tank_filter_median(const tank_filter_t *f, uint16_t *out_mm);

// ---------------------------------------------------------------------------
// Decisions
// ---------------------------------------------------------------------------

// A reading inside the sensor's blind zone or beyond the tank floor is not a
// level. The caller drops it instead of filtering it.
bool tank_reading_plausible(uint16_t mm);

// Pump hysteresis: on at TANK_SONAR_REFILL_MM, off at TANK_SONAR_FULL_MM, hold
// in between. No valid level -> false, always: a pump that cannot see the tank
// can overflow it or run it dry.
bool tank_pump_want(uint16_t mm, bool valid, bool pump_on);

// True while the tank holds enough for a full P20 pour on top of the pump
// intake. No valid level -> false: the machine must not sell water it cannot
// see.
bool tank_can_serve(uint16_t mm, bool valid);

// 0-100 between TANK_SONAR_EMPTY_MM (0) and TANK_SONAR_FULL_MM (100), for the
// display. Integer only. 0 when there is no valid level.
uint8_t tank_percent(uint16_t mm, bool valid);

#endif  // TANK_LEVEL_H
