// What the Mega last told us, decoded from its PG / VL / FT / LV lines.
//
// Both the LCD and the web page draw from the Mega's own numbers; neither
// computes money or volume. The enum values these fields carry are defined in
// the main project (state_t, fault_t in types.h; hmi_event_t in hmi.h).

#pragma once
#include <stdint.h>

// state_t values
enum : int32_t {
  ST_BOOT = 0, ST_STANDBY, ST_ACCEPTING, ST_SELECTING, ST_AWAITING, ST_DISPENSING,
  ST_PAUSED, ST_SETTLING, ST_COMPLETE, ST_PAYING, ST_THANKYOU, ST_FAULT, ST_ADMIN
};

// hmi_event_t values the screen sends
enum : int {
  EV_SELECT_VOLUME = 1, EV_DISPENSE_AGAIN = 2, EV_FINISH = 3, EV_BACK = 4, EV_CONFIRM = 15
};

struct machine_t {
  int32_t state;
  int32_t credit;     // centavos
  int32_t inserted;   // centavos
  int32_t maxml;
  int32_t target;
  int32_t poured;
  int32_t total;
  int32_t fault;      // fault_t, 0 = none
  int32_t tank;       // 0-100
  int32_t tank_ok;
  int32_t gal_a;      // 1 = gallon A low
  int32_t gal_b;
  uint32_t seen_ms;   // millis() of the last line from the Mega
};

extern machine_t g_m;

// Apply one line from the Mega. Returns true if anything changed.
bool model_apply(const char *line);

// Send a touch to the Mega: "EV <ev>" or "EV <ev> <payload>".
void mega_send_event(int ev, long payload);
