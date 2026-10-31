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
  EV_SELECT_VOLUME = 1, EV_DISPENSE_AGAIN = 2, EV_FINISH = 3, EV_BACK = 4,
  EV_ADMIN_SET_P1 = 10, EV_ADMIN_SET_P5 = 11, EV_CONFIRM = 15,
  EV_ADMIN_CLEAR = 16, EV_ADMIN_TEST = 17, EV_ADMIN_UNLOCK = 18
};

struct machine_t {
  int32_t state;
  int32_t credit;     // centavos
  int32_t inserted;   // centavos
  int32_t maxml;
  int32_t target;
  int32_t poured;
  int32_t total;
  int32_t unpaid;     // centavos of change the last payout could not give
  int32_t p1;         // coins in the P1 hopper (Mega's count)
  int32_t p5;         // coins in the P5 hopper
  int32_t b10;        // P10 coins in the coin box
  int32_t b20;        // P20 coins in the coin box
  int32_t fill;       // 1 = refill pump running
  int32_t au;         // last admin PIN reply: 1 accepted, 0 refused
  int32_t au_seq;     // bumps on every AU line, so the same answer twice is seen
  int32_t fault;      // fault_t, 0 = none
  int32_t tank;       // 0-100
  int32_t tank_ok;
  int32_t gal_a;      // 1 = gallon A low
  int32_t gal_b;
  uint32_t seen_ms;   // millis() of the last line from the Mega
};

extern machine_t g_m;

// Sales screen data, from the Mega's SD / DT / HS / HE reply (EEPROM-backed
// there, so it survives a power cut). Requested with EV 6.
#define EV_STATS 6
#define STATS_HISTORY 10

struct sale_t {
  uint32_t ts;     // Unix seconds, 0 = no clock when it was recorded
  int32_t in;      // centavos inserted
  int32_t ml;      // dispensed
  int32_t change;  // centavos returned
  int32_t owed;    // centavos of change NOT paid (0 = paid in full)
};

struct stats_t {
  int32_t income;  // centavos, today
  int32_t volume;  // mL, today
  int32_t count;   // purchases, today
  uint16_t year;   // 0 = the Mega has no clock
  uint8_t month, day;
  sale_t sale[STATS_HISTORY];
  uint8_t n;       // rows in sale[]
  bool ready;      // HE received since the last request
};

extern stats_t g_stats;

// Apply one line from the Mega. Returns true if anything changed.
bool model_apply(const char *line);

// Send a touch to the Mega: "EV <ev>" or "EV <ev> <payload>".
void mega_send_event(int ev, long payload);

// Admin PIN, shared by the LCD and the phone page so one lockout covers both.
// Kept in this board's NVS (main.cpp); checked here, never in the browser.
#define ADM_MAX_TRIES 3
int admin_try(uint16_t pin);          // 1 right, 0 wrong, -1 locked out
bool admin_locked();
uint32_t admin_lock_left_s();
uint8_t admin_fails();
void admin_set_pin(uint16_t pin);
// Unlock the Mega's own admin gate, then send the action.
void admin_mega(int ev, long payload);
