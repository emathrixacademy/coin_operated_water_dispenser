#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"

machine_t g_m = {};
stats_t g_stats = {};

static bool set(int32_t *field, long v) {
  if (*field == (int32_t)v) return false;
  *field = (int32_t)v;
  return true;
}

bool model_apply(const char *line) {
  g_m.seen_ms = millis();

  if (!strncmp(line, "VL ", 3)) {
    const char *name = line + 3;
    const char *sp = strchr(name, ' ');
    if (!sp) return false;
    const size_t n = (size_t)(sp - name);
    const long v = strtol(sp + 1, NULL, 10);
    struct { const char *k; int32_t *f; } map[] = {
      { "state", &g_m.state }, { "credit", &g_m.credit }, { "inserted", &g_m.inserted },
      { "maxml", &g_m.maxml }, { "target", &g_m.target }, { "poured", &g_m.poured },
      { "total", &g_m.total }, { "unpaid", &g_m.unpaid }
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
      if (strlen(map[i].k) == n && !strncmp(map[i].k, name, n)) return set(map[i].f, v);
    }
    return false;
  }

  if (!strncmp(line, "FT ", 3)) return set(&g_m.fault, strtol(line + 3, NULL, 10));

  if (!strncmp(line, "LV ", 3)) {
    char *p = (char *)line + 3;
    bool ch = false;
    ch |= set(&g_m.tank, strtol(p, &p, 10));
    ch |= set(&g_m.tank_ok, strtol(p, &p, 10));
    ch |= set(&g_m.gal_a, strtol(p, &p, 10));
    ch |= set(&g_m.gal_b, strtol(p, &p, 10));
    return ch;
  }

  if (!strncmp(line, "SD ", 3)) {
    char *p = (char *)line + 3;
    g_stats.income = strtol(p, &p, 10);
    g_stats.volume = strtol(p, &p, 10);
    g_stats.count = strtol(p, &p, 10);
    g_stats.n = 0;
    g_stats.ready = false;
    return true;
  }

  if (!strncmp(line, "DT ", 3)) {
    char *p = (char *)line + 3;
    g_stats.year = (uint16_t)strtol(p, &p, 10);
    g_stats.month = (uint8_t)strtol(p, &p, 10);
    g_stats.day = (uint8_t)strtol(p, &p, 10);
    return true;
  }

  if (!strncmp(line, "HS ", 3)) {
    char *p = (char *)line + 3;
    const long i = strtol(p, &p, 10);
    if (i < 0 || i >= STATS_HISTORY) return false;
    sale_t &s = g_stats.sale[i];
    s.ts = (uint32_t)strtoul(p, &p, 10);
    s.in = strtol(p, &p, 10);
    s.ml = strtol(p, &p, 10);
    s.change = strtol(p, &p, 10);
    s.owed = strtol(p, &p, 10);
    if (g_stats.n < i + 1) g_stats.n = (uint8_t)(i + 1);
    return true;
  }

  if (!strncmp(line, "HE", 2)) {
    g_stats.ready = true;
    return true;
  }

  return false;   // PG and anything else: the state field already says it
}
