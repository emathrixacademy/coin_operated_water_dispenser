#pragma once

#include <Arduino.h>

// Two forward/reverse DC motors, each with a HOME (back) and FRONT limit.
// Polled; never blocks. Motor index 0 = Motor 1, 1 = Motor 2.

enum slide_cmd_t : uint8_t {
  SLIDE_STOP = 0,
  SLIDE_FWD,     // run forward until the FRONT limit
  SLIDE_REV,     // run back until the HOME limit
  SLIDE_CYCLE,   // forward to FRONT, pause, back to HOME
  SLIDE_AUTO,    // repeat the cycle forever
};

void slide_begin();
void slide_update();
void slide_command(uint8_t m, slide_cmd_t c);
// Run both motors while `want` is true; when it goes false (after
// SLIDE_CHANGE_HOLD_MS) each motor returns to HOME and stops.
void slide_follow(bool want);
// Full HOME->FRONT->HOME strokes since boot. With HOPPER_BY_SLIDE each stroke
// pushes one coin, so coin_hopper counts the payout from this.
uint16_t slide_strokes(uint8_t m);
bool slide_busy(uint8_t m);
bool slide_fault(uint8_t m);
bool slide_at_home(uint8_t m);
bool slide_at_front(uint8_t m);
void slide_print_status();
