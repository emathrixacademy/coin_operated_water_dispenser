// Cold tank level arithmetic. No Arduino dependency by design -- see
// tank_level.h.

#include <string.h>
#include "tank_level.h"

void a02_parser_reset(a02_parser_t *p) {
  p->len = 0;
}

bool a02_parser_feed(a02_parser_t *p, uint8_t b, uint16_t *out_mm) {
  // Wait for a header. Anything before it is the tail of a frame we joined
  // half way through.
  if (p->len == 0 && b != 0xFF) return false;

  p->buf[p->len++] = b;
  if (p->len < 4) return false;

  const uint8_t sum = (uint8_t)(p->buf[0] + p->buf[1] + p->buf[2]);
  if (sum == p->buf[3]) {
    if (out_mm) *out_mm = (uint16_t)(((uint16_t)p->buf[1] << 8) | p->buf[2]);
    p->len = 0;
    return true;
  }

  // Bad checksum. A data byte can itself be 0xFF, so the real header may
  // already be inside the buffer -- slide to it instead of discarding.
  uint8_t i = 1;
  while (i < 4 && p->buf[i] != 0xFF) i++;
  p->len = (uint8_t)(4 - i);
  if (p->len) memmove(p->buf, p->buf + i, p->len);
  return false;
}

void tank_filter_reset(tank_filter_t *f) {
  f->count = 0;
  f->head = 0;
}

void tank_filter_add(tank_filter_t *f, uint16_t mm) {
  f->s[f->head] = mm;
  f->head = (uint8_t)((f->head + 1) % TANK_FILTER_SAMPLES);
  if (f->count < TANK_FILTER_SAMPLES) f->count++;
}

bool tank_filter_median(const tank_filter_t *f, uint16_t *out_mm) {
  if (f->count < TANK_FILTER_SAMPLES) return false;

  // Insertion sort on a copy. Five elements; nothing cleverer is warranted.
  uint16_t v[TANK_FILTER_SAMPLES];
  memcpy(v, f->s, sizeof(v));
  for (uint8_t i = 1; i < TANK_FILTER_SAMPLES; i++) {
    const uint16_t x = v[i];
    uint8_t j = i;
    while (j > 0 && v[j - 1] > x) {
      v[j] = v[j - 1];
      j--;
    }
    v[j] = x;
  }
  *out_mm = v[TANK_FILTER_SAMPLES / 2];
  return true;
}

bool tank_reading_plausible(uint16_t mm) {
  return mm >= TANK_SONAR_BLIND_MM && mm <= TANK_SONAR_EMPTY_MM;
}

bool tank_pump_want(uint16_t mm, bool valid, bool pump_on) {
  if (!valid) return false;
  if (mm <= TANK_SONAR_FULL_MM) return false;     // full
  if (mm >= TANK_SONAR_REFILL_MM) return true;    // dropped past the band
  return pump_on;                                 // inside the band: hold
}

bool tank_can_serve(uint16_t mm, bool valid) {
  if (!valid) return false;
  return mm < TANK_SONAR_MIN_SERVE_MM;
}

uint8_t tank_percent(uint16_t mm, bool valid) {
  if (!valid) return 0;
  if (mm <= TANK_SONAR_FULL_MM) return 100;
  if (mm >= TANK_SONAR_EMPTY_MM) return 0;
  return (uint8_t)(((uint32_t)(TANK_SONAR_EMPTY_MM - mm) * 100u) /
                   (TANK_SONAR_EMPTY_MM - TANK_SONAR_FULL_MM));
}
