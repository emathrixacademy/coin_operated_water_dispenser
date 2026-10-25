#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hmi.h"
#include "water_level.h"
#include "billing.h"
#include "faults.h"
#include "dispense.h"
#ifdef SERIAL_CONSOLE
#include "persist.h"
#include "coin_level.h"
#include "bottle.h"
#endif

// Display link to the ESP32 LCD. Protocol in hmi.h.
//
// Implementation notes:
//   - Fixed char[HMI_TX_BUFFER] / char[HMI_RX_BUFFER]. No String, no dynamic
//     allocation.
//   - Amounts format from integer centavos with snprintf("%ld.%02ld") using
//     integer division and modulo. No float in the display path -- the number
//     shown is the number charged.
//   - RX is parsed incrementally in update(). Never block waiting for a byte.
//   - Refresh during a pour is rate-limited to HMI_DISPENSE_REFRESH_MS so the
//     serial line is not saturated while flow pulses arrive on an interrupt.

static page_t s_page = PAGE_STANDBY;

// One pending event. A second event arriving before the first is taken is
// dropped, so a double tap cannot turn into two actions.
static hmi_event_t s_evt = HMI_EVENT_NONE;
static int32_t s_evt_payload = 0;

static void post_event(hmi_event_t e, int32_t payload) {
  if (s_evt != HMI_EVENT_NONE) return;
  s_evt = e;
  s_evt_payload = payload;
}

// ---------------------------------------------------------------------------
// TX
// ---------------------------------------------------------------------------

static char s_tx[HMI_TX_BUFFER];

// Write one formatted line. Page, text and fault commands must arrive, so they
// are written even if the TX buffer is full -- at HMI_BAUD that is a stall of a
// few ms at worst, never the ~67 ms it was at 9600.
static void send_line() {
  s_tx[sizeof(s_tx) - 1] = '\0';
  HMI_SERIAL.write((const uint8_t *)s_tx, strlen(s_tx));
  HMI_SERIAL.write('\n');
}

// ---------------------------------------------------------------------------
// Level status -- the LV line
// ---------------------------------------------------------------------------

static uint8_t s_lv_last[4] = { 0xFF, 0xFF, 0xFF, 0xFF };  // forces the first send
static uint32_t s_lv_sent_ms = 0;

static void status_update() {
  const uint8_t now_lv[4] = {
    water_tank_percent(),
    (uint8_t)(water_tank_sensor_ok() ? 1 : 0),
    (uint8_t)(water_gallon_low(0) ? 1 : 0),
    (uint8_t)(water_gallon_low(1) ? 1 : 0)
  };
  const uint32_t now = millis();
  const bool changed = memcmp(now_lv, s_lv_last, sizeof(now_lv)) != 0;
  if (!changed && (uint32_t)(now - s_lv_sent_ms) < HMI_STATUS_RESEND_MS) return;

  snprintf(s_tx, sizeof(s_tx), "LV %u %u %u %u",
           now_lv[0], now_lv[1], now_lv[2], now_lv[3]);

  // Unlike page commands this one repeats on its own, so it is skipped rather
  // than allowed to block when the TX buffer is busy.
  if ((size_t)HMI_SERIAL.availableForWrite() < strlen(s_tx) + 1) return;

  send_line();
  memcpy(s_lv_last, now_lv, sizeof(now_lv));
  s_lv_sent_ms = now;
}

// ---------------------------------------------------------------------------
// RX -- "EV <event> [payload]"
// ---------------------------------------------------------------------------

static char s_rx[HMI_RX_BUFFER];
static uint8_t s_rx_len = 0;
static bool s_rx_overflow = false;   // discard until the next newline

static bool s_confirm = false;

static void rx_line(char *line) {
  if (strncmp(line, "EV ", 3) != 0) return;

  char *end = NULL;
  const long ev = strtol(line + 3, &end, 10);
  if (end == line + 3) return;                         // no number
  if (ev <= (long)HMI_EVENT_NONE || ev > (long)HMI_EVENT_CONFIRM) return;

  if (ev == (long)HMI_EVENT_CONFIRM) {
    s_confirm = true;
    return;
  }
  const long payload = (*end == ' ') ? strtol(end + 1, NULL, 10) : 0;
  post_event((hmi_event_t)ev, (int32_t)payload);
}

static void rx_update() {
  while (HMI_SERIAL.available()) {
    const char ch = (char)HMI_SERIAL.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (!s_rx_overflow && s_rx_len) {
        s_rx[s_rx_len] = '\0';
        rx_line(s_rx);
      }
      s_rx_len = 0;
      s_rx_overflow = false;
    } else if (s_rx_len < sizeof(s_rx) - 1) {
      s_rx[s_rx_len++] = ch;
    } else {
      // Longer than any valid line: noise, or a baud mismatch. Never act on a
      // truncated line -- "EV 3 2000" cut short is a different selection.
      s_rx_overflow = true;
    }
  }
}

void hmi_begin() {
  HMI_SERIAL.begin(HMI_BAUD);
  // A newline closes any partial line left in the ESP32's parser by a reset.
  HMI_SERIAL.write('\n');
}

#ifdef SERIAL_CONSOLE
// ===========================================================================
// TEST CONSOLE -- types the same events into the USB Serial Monitor.
//
// Commands typed into the Serial Monitor (115200, newline) become the same
// touch events the panel sends, so the real state machine runs end to end
// with or without the ESP32 attached. DEBUG builds only (env:test); never in a
// build that takes money.
// ===========================================================================
static char s_line[24];
static uint8_t s_len = 0;

static void console_help() {
  Serial.println(F("[console] c=CONFIRM  s <mL>=select (e.g. s 500)  f=finish/refund"));
  Serial.println(F("[console] b=back  a=dispense again  st=status"));
  Serial.println(F("[console] p1 <n> / p5 <n>=load hopper count  clr=clear faults  h=help"));
}

static void console_status() {
  const inventory_t *inv = persist_inventory();
  Serial.print(F("[status] inserted="));  Serial.print(billing_inserted() / 100);
  Serial.print(F(" credit="));            Serial.print(billing_credit() / 100);
  Serial.print(F(" poured="));            Serial.print(dispense_delivered());
  Serial.print(F("mL bottle="));          Serial.println(bottle_present() ? F("YES") : F("no"));
  Serial.print(F("[status] P1="));        Serial.print(inv->p1_count);
  Serial.print(F(" P5="));                Serial.print(inv->p5_count);
  Serial.print(F(" P1full="));            Serial.print(coin_level_hopper_full(HOPPER_P1) ? 1 : 0);
  Serial.print(F(" P5full="));            Serial.print(coin_level_hopper_full(HOPPER_P5) ? 1 : 0);
  Serial.print(F(" boxfull="));           Serial.println(coin_level_box_full() ? 1 : 0);
  // tank_mm is what calibration reads: fill the tank to each level and copy
  // the number into the TANK_SONAR_*_MM constants.
  Serial.print(F("[status] tank_mm="));   Serial.print(water_tank_distance_mm());
  Serial.print(F(" tank="));              Serial.print(water_tank_percent());
  Serial.print(F("% sensor="));           Serial.print(water_tank_sensor_ok() ? F("ok") : F("LOST"));
  Serial.print(F(" galA_low="));          Serial.print(water_gallon_low(0) ? 1 : 0);
  Serial.print(F(" galB_low="));          Serial.println(water_gallon_low(1) ? 1 : 0);
  Serial.print(F("[status] fill_pump=")); Serial.print(water_pump_running() ? F("ON") : F("off"));
  Serial.print(F(" fault="));             Serial.print((int)faults_active());
  Serial.print(F(" today=P"));            Serial.print(persist_daily_profit() / 100);
  Serial.print(F(" "));                   Serial.print(persist_daily_volume());
  Serial.println(F("mL"));
}

static void console_exec(char *cmd) {
  char *arg = strchr(cmd, ' ');
  int32_t n = 0;
  if (arg) { *arg++ = '\0'; n = atol(arg); }

  if      (!strcmp(cmd, "c"))  { s_confirm = true; }
  else if (!strcmp(cmd, "s"))  { post_event(HMI_EVENT_SELECT_VOLUME, n); }
  else if (!strcmp(cmd, "f"))  { post_event(HMI_EVENT_FINISH, 0); }
  else if (!strcmp(cmd, "b"))  { post_event(HMI_EVENT_BACK, 0); }
  else if (!strcmp(cmd, "a"))  { post_event(HMI_EVENT_DISPENSE_AGAIN, 0); }
  else if (!strcmp(cmd, "st")) { console_status(); return; }
  else if (!strcmp(cmd, "p1") || !strcmp(cmd, "p5")) {
    if (n < 0) n = 0;
    persist_inventory_set(cmd[1] == '1' ? DEST_P1_HOPPER : DEST_P5_HOPPER, (uint16_t)n);
    // Stand-in for the Admin load: LOW CHANGE clears here and is re-checked
    // at the STANDBY gate against the new counts.
    faults_clear(FAULT_LOW_CHANGE);
  }
  else if (!strcmp(cmd, "clr")) {
    for (uint8_t f = 1; f <= (uint8_t)FAULT_ACCEPTOR; f++) faults_clear((fault_t)f);
  }
  else { console_help(); return; }
  Serial.print(F("[console] ok: ")); Serial.println(cmd);
}

#endif

bool hmi_confirm_take() {
  const bool c = s_confirm;
  s_confirm = false;
  return c;
}

// ---------------------------------------------------------------------------
// Screen sync -- PG / VL / FT from the machine's own state
// ---------------------------------------------------------------------------

static page_t page_for(state_t s) {
  switch (s) {
    case STATE_SELECTING:       return PAGE_SELECT_VOLUME;
    case STATE_AWAITING_BOTTLE: return PAGE_INSERT_BOTTLE;
    case STATE_DISPENSING:
    case STATE_SETTLING:
    case STATE_COMPLETE:        return PAGE_DISPENSING;
    case STATE_PAUSED:          return PAGE_WAITING;
    case STATE_PAYING_CHANGE:
    case STATE_THANK_YOU:       return PAGE_THANK_YOU;
    case STATE_FAULT:           return PAGE_FAULT;
    case STATE_ADMIN:           return PAGE_ADMIN;
    case STATE_BOOT:
    case STATE_STANDBY:
    case STATE_ACCEPTING:
    default:                    return PAGE_STANDBY;
  }
}

enum sync_field_t : uint8_t {
  SF_STATE = 0, SF_CREDIT, SF_INSERTED, SF_MAXML, SF_TARGET, SF_POURED, SF_TOTAL,
  SF_COUNT
};
static const char *const SF_NAME[SF_COUNT] = {
  "state", "credit", "inserted", "maxml", "target", "poured", "total"
};

static int32_t s_sf_sent[SF_COUNT];
static bool s_sf_valid[SF_COUNT];      // false = must (re)send
static bool s_pg_valid = false;
static bool s_ft_valid = false;
static uint8_t s_ft_sent = 0;
static uint32_t s_sync_full_ms = 0;
static uint32_t s_poured_ms = 0;

// Sends s_tx only if it fits in the TX buffer right now. Anything not sent
// stays invalid and goes out on a later pass -- the loop never waits on it.
static bool try_send_line() {
  if ((size_t)HMI_SERIAL.availableForWrite() < strlen(s_tx) + 1) return false;
  send_line();
  return true;
}

void hmi_sync(state_t s) {
  const uint32_t now = millis();

  if ((uint32_t)(now - s_sync_full_ms) >= HMI_STATUS_RESEND_MS) {
    s_sync_full_ms = now;
    for (uint8_t i = 0; i < SF_COUNT; i++) s_sf_valid[i] = false;
    s_pg_valid = false;
    s_ft_valid = false;
  }

  const page_t pg = page_for(s);
  if (!s_pg_valid || pg != s_page) {
    snprintf(s_tx, sizeof(s_tx), "PG %u", (unsigned)pg);
    if (!try_send_line()) return;      // page first; values follow next pass
    s_page = pg;
    s_pg_valid = true;
  }

  const uint8_t ft = (uint8_t)faults_active();
  if (!s_ft_valid || ft != s_ft_sent) {
    snprintf(s_tx, sizeof(s_tx), "FT %u", (unsigned)ft);
    if (try_send_line()) {
      s_ft_sent = ft;
      s_ft_valid = true;
    }
  }

  const int32_t now_v[SF_COUNT] = {
    (int32_t)s,
    (int32_t)billing_credit(),
    (int32_t)billing_inserted(),
    (int32_t)billing_max_selectable_ml(),
    (int32_t)dispense_target(),
    (int32_t)dispense_delivered(),
    (int32_t)billing_total_dispensed()
  };

  for (uint8_t i = 0; i < SF_COUNT; i++) {
    if (s_sf_valid[i] && now_v[i] == s_sf_sent[i]) continue;
    // The pour counter changes on every flow pulse; HMI_DISPENSE_REFRESH_MS
    // keeps it from flooding the line.
    if (i == SF_POURED && s_sf_valid[i] &&
        (uint32_t)(now - s_poured_ms) < HMI_DISPENSE_REFRESH_MS) continue;

    snprintf(s_tx, sizeof(s_tx), "VL %s %ld", SF_NAME[i], (long)now_v[i]);
    if (!try_send_line()) return;
    s_sf_sent[i] = now_v[i];
    s_sf_valid[i] = true;
    if (i == SF_POURED) s_poured_ms = now;
  }
}

void hmi_update() {
#ifdef SERIAL_CONSOLE
  // Non-blocking: take what has arrived, act on a full line.
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      s_line[s_len] = '\0';
      if (s_len) console_exec(s_line);
      s_len = 0;
    } else if (s_len < sizeof(s_line) - 1) {
      s_line[s_len++] = ch;
    }
  }
#endif
  rx_update();
  status_update();
  // TODO(M6): rate-limited refresh of the pour screen.
}

void hmi_setPage(page_t page) {
  s_page = page;
  snprintf(s_tx, sizeof(s_tx), "PG %u", (unsigned)page);
  send_line();
}

page_t hmi_currentPage() {
  return s_page;
}

void hmi_setText(const char *component, const char *value) {
  snprintf(s_tx, sizeof(s_tx), "TX %s %s", component, value);
  send_line();
}

void hmi_setValue(const char *component, int32_t value) {
  snprintf(s_tx, sizeof(s_tx), "VL %s %ld", component, (long)value);
  send_line();
}

void hmi_setAmount(const char *component, money_t centavos) {
  // Integer only: 2000 -> "20.00". No float between the till and the screen.
  char amount[16];
  const long c = (long)centavos;
  const long whole = c / CENTAVOS_PER_PESO;
  const long frac = (c < 0 ? -c : c) % CENTAVOS_PER_PESO;
  snprintf(amount, sizeof(amount), "%ld.%02ld", whole, frac);
  hmi_setText(component, amount);
}

void hmi_setVolume(const char *component, volume_t ml) {
  hmi_setValue(component, (int32_t)ml);
}

void hmi_setProgress(const char *component, uint8_t percent) {
  hmi_setValue(component, percent > 100 ? 100 : percent);
}

void hmi_setOptionEnabled(uint8_t option_index, bool enabled) {
  snprintf(s_tx, sizeof(s_tx), "OP %u %u", (unsigned)option_index, enabled ? 1u : 0u);
  send_line();
}

void hmi_showFault(fault_t fault) {
  // The acceptor is already inhibited by faults before this is called.
  snprintf(s_tx, sizeof(s_tx), "FT %u", (unsigned)fault);
  send_line();
}

bool hmi_event_available() {
  return s_evt != HMI_EVENT_NONE;
}

hmi_event_t hmi_take_event(int32_t *payload) {
  const hmi_event_t e = s_evt;
  if (payload) *payload = s_evt_payload;
  s_evt = HMI_EVENT_NONE;
  s_evt_payload = 0;
  return e;
}
