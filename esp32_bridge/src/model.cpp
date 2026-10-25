#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"

machine_t g_m = {};

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
      { "total", &g_m.total }
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

  return false;   // PG and anything else: the state field already says it
}
