#include <Arduino.h>
#include "config.h"
#include "coin_level.h"

// See coin_level.h. Same debounce shape as the float switches in
// water_level.cpp: a raw change restarts the window, and the state only moves
// once the raw reading has held for the whole window.

struct beam_t {
  uint8_t  pin;
  bool     state;       // debounced: true = full
  bool     candidate;   // last raw reading
  uint32_t since_ms;    // when candidate last changed
};

static beam_t s_p1  = { PIN_HOPPER_P1_FULL, false, false, 0 };
static beam_t s_p5  = { PIN_HOPPER_P5_FULL, false, false, 0 };
static beam_t s_box = { PIN_CHAMBER_FULL,   false, false, 0 };

static bool raw_full(const beam_t *b) {
  return digitalRead(b->pin) == LOW;
}

static void beam_begin(beam_t *b) {
  pinMode(b->pin, INPUT_PULLUP);
  // Start from the raw reading: a hopper that is already full at power-up must
  // not take coins during the first debounce window.
  b->state = raw_full(b);
  b->candidate = b->state;
  b->since_ms = millis();
}

static void beam_update(beam_t *b) {
  const bool raw = raw_full(b);
  const uint32_t now = millis();

  if (raw != b->candidate) {
    b->candidate = raw;
    b->since_ms = now;
    return;
  }
  if (raw == b->state) return;
  if ((uint32_t)(now - b->since_ms) < COIN_LEVEL_DEBOUNCE_MS) return;

  b->state = raw;
}

void coin_level_begin() {
  beam_begin(&s_p1);
  beam_begin(&s_p5);
  beam_begin(&s_box);
}

void coin_level_update() {
  beam_update(&s_p1);
  beam_update(&s_p5);
  beam_update(&s_box);
}

bool coin_level_hopper_full(hopper_id_t hopper) {
  switch (hopper) {
    case HOPPER_P1: return s_p1.state;
    case HOPPER_P5: return s_p5.state;
    default:        return true;
  }
}

bool coin_level_box_full() {
  return s_box.state;
}
