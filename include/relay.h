#pragma once

#include <Arduino.h>

// The one way to drive a relay or SSR input (all active LOW).
//
// ON pulls the input LOW. OFF lets go of it (INPUT, no pull-up) instead of
// driving it HIGH. Driven HIGH, the module still lit whenever the Mega's 5 V
// sat below the module's own VCC -- the difference was enough to light the
// opto (bench 2026-10-30: all four relays on with the Mega running off its
// own supply). Let go, there is no path for that current at any voltage.
static inline void relay_write(uint8_t pin, bool on) {
  if (on) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  } else {
    pinMode(pin, INPUT);   // also clears the pull-up
  }
}
