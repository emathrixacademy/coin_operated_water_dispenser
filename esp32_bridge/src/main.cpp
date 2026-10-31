// ESP32 display firmware. See platformio.ini for the two builds.
//
// Mega Serial2 lines   ->  the LCD (cyd35 only) and every connected browser
// Touch or browser "EV" ->  Mega Serial2
//
// Nothing here interprets money. The screens draw the Mega's own numbers, and
// the latest line per field is remembered so a phone that connects
// mid-transaction sees the current screen at once.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <stdio.h>
#include <string.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "page.h"
#include "model.h"
#include "ui.h"

static const char AP_SSID[] = "WATERVENDO-TEST";
static const char AP_PASS[] = "emx-bench-01";   // WPA2 needs at least 8 chars

// UART2 to the Mega. Mega TX2 (D16) -> 1k/2k divider -> MEGA_RX_PIN.
// MEGA_TX_PIN -> Mega RX2 (D17). Common ground. Set per board in
// platformio.ini: 16/17 on a plain dev board; on the CYD 16/17 drive its RGB
// LED, so that build uses header pins instead.
#ifndef MEGA_RX_PIN
#define MEGA_RX_PIN 16
#endif
#ifndef MEGA_TX_PIN
#define MEGA_TX_PIN 17
#endif
#define MEGA_BAUD   115200   // must match HMI_BAUD in the main project

#define WDT_TIMEOUT_S    8      // loop frozen this long -> reboot
#define ALIVE_EVERY_MS   5000   // heartbeat on the USB log

#define BRIDGE_LINE_MAX  64
#define CACHE_MAX 24
#define KEY_MAX   16

static WebServer s_http(80);
static WebSocketsServer s_ws(81);

// Latest line per field. Key = first token, or first two for VL / TX / OP,
// which name a component ("VL credit", "TX t_amount").
static char s_key[CACHE_MAX][KEY_MAX];
static char s_line[CACHE_MAX][BRIDGE_LINE_MAX];
static uint8_t s_cached = 0;

static void key_of(const char *line, char *key) {
  const bool two = !strncmp(line, "VL ", 3) || !strncmp(line, "TX ", 3) ||
                   !strncmp(line, "OP ", 3);
  uint8_t spaces = 0, i = 0;
  for (; line[i] && i < KEY_MAX - 1; i++) {
    if (line[i] == ' ' && ++spaces == (two ? 2 : 1)) break;
    key[i] = line[i];
  }
  key[i] = '\0';
}

static void cache_put(const char *line) {
  char key[KEY_MAX];
  key_of(line, key);
  for (uint8_t i = 0; i < s_cached; i++) {
    if (!strcmp(s_key[i], key)) {
      strncpy(s_line[i], line, BRIDGE_LINE_MAX - 1);
      return;
    }
  }
  if (s_cached >= CACHE_MAX) return;
  strncpy(s_key[s_cached], key, KEY_MAX - 1);
  strncpy(s_line[s_cached], line, BRIDGE_LINE_MAX - 1);
  s_cached++;
}

// Only well-formed events reach the Mega: "EV", digits and spaces, short.
// The Mega validates again; this just keeps browser junk off the wire.
// Returns the event number, or -1.
static long event_of(const uint8_t *p, size_t len) {
  if (len < 4 || len > 24 || memcmp(p, "EV ", 3) != 0) return -1;
  for (size_t i = 3; i < len; i++) {
    if (p[i] != ' ' && (p[i] < '0' || p[i] > '9')) return -1;
  }
  long ev = 0;
  for (size_t i = 3; i < len && p[i] != ' '; i++) ev = ev * 10 + (p[i] - '0');
  return ev;
}

// 10-14 and 16-18 change the machine; 15 is CONFIRM, a customer button.
static bool is_admin_event(long ev) {
  return ev >= EV_ADMIN_SET_P1 && ev != EV_CONFIRM;
}

void mega_send_event(int ev, long payload) {
  char b[24];
  if (payload < 0) snprintf(b, sizeof(b), "EV %d", ev);
  else snprintf(b, sizeof(b), "EV %d %ld", ev, payload);
  Serial2.print(b);
  Serial2.write('\n');
  Serial.print(F("-> "));
  Serial.println(b);
}

// --- Admin PIN -------------------------------------------------------------
//
// One PIN for the LCD and the phone page. Three wrong tries from either lock
// both for ADM_LOCK_MS. A phone that enters the right PIN may send admin
// events for ADM_SESSION_MS after its last one; every other phone may not.

#define ADM_DEFAULT_PIN  1234
#define ADM_MEGA_SECRET  1234     // must equal ADMIN_PIN in the Mega's config.h
#define ADM_LOCK_MS      60000
#define ADM_SESSION_MS   120000

static Preferences s_prefs;
static uint16_t s_admin_pin = ADM_DEFAULT_PIN;
static uint8_t s_fails = 0;
static bool s_locked = false;
static uint32_t s_lock_since = 0;
static bool s_auth[WEBSOCKETS_SERVER_CLIENT_MAX];
static uint32_t s_auth_ms[WEBSOCKETS_SERVER_CLIENT_MAX];

bool admin_locked() {
  if (s_locked && (uint32_t)(millis() - s_lock_since) >= ADM_LOCK_MS) {
    s_locked = false;
    s_fails = 0;
  }
  return s_locked;
}

uint32_t admin_lock_left_s() {
  if (!admin_locked()) return 0;
  return (ADM_LOCK_MS - (millis() - s_lock_since)) / 1000 + 1;
}

uint8_t admin_fails() { return s_fails; }

int admin_try(uint16_t pin) {
  if (admin_locked()) return -1;
  if (pin == s_admin_pin) {
    s_fails = 0;
    return 1;
  }
  if (++s_fails >= ADM_MAX_TRIES) {
    s_locked = true;
    s_lock_since = millis();
    return -1;
  }
  return 0;
}

void admin_set_pin(uint16_t pin) {
  s_prefs.begin("wv", false);
  s_prefs.putUShort("pin", pin);
  s_prefs.end();
  s_admin_pin = pin;
}

void admin_mega(int ev, long payload) {
  mega_send_event(EV_ADMIN_UNLOCK, ADM_MEGA_SECRET);   // the Mega's own gate
  if (ev != EV_ADMIN_UNLOCK) mega_send_event(ev, payload);
}

// "1234" -> 1234; anything that is not exactly four digits -> -1.
static long pin_of(const uint8_t *p, size_t len) {
  if (len != 4) return -1;
  long v = 0;
  for (size_t i = 0; i < 4; i++) {
    if (p[i] < '0' || p[i] > '9') return -1;
    v = v * 10 + (p[i] - '0');
  }
  return v;
}

static bool client_authed(uint8_t c) {
  return c < WEBSOCKETS_SERVER_CLIENT_MAX && s_auth[c] &&
         (uint32_t)(millis() - s_auth_ms[c]) < ADM_SESSION_MS;
}

// Phone admin lines (replies go to that phone only, as "AA ..."):
//   AD <pin>  log in      -> AA 1 | AA 0 <tries> | AA L <seconds left>
//   AP <pin>  new PIN     -> AA P | AA N (not logged in)
//   AX        log out
static void on_admin_line(uint8_t c, const uint8_t *p, size_t len) {
  char r[24];
  if (len >= 2 && !memcmp(p, "AX", 2)) {
    if (c < WEBSOCKETS_SERVER_CLIENT_MAX) s_auth[c] = false;
    return;
  }
  if (len < 3) return;
  const long pin = pin_of(p + 3, len - 3);
  if (pin < 0) return;
  if (!memcmp(p, "AD ", 3)) {
    const int ok = admin_try((uint16_t)pin);
    if (ok == 1) {
      if (c < WEBSOCKETS_SERVER_CLIENT_MAX) { s_auth[c] = true; s_auth_ms[c] = millis(); }
      snprintf(r, sizeof(r), "AA 1");
    } else if (ok == 0) {
      snprintf(r, sizeof(r), "AA 0 %u", s_fails);
    } else {
      snprintf(r, sizeof(r), "AA L %lu", (unsigned long)admin_lock_left_s());
    }
    s_ws.sendTXT(c, r);
  } else if (!memcmp(p, "AP ", 3)) {
    if (!client_authed(c)) { s_ws.sendTXT(c, "AA N"); return; }
    s_auth_ms[c] = millis();
    admin_set_pin((uint16_t)pin);
    s_ws.sendTXT(c, "AA P");
  }
}

static void on_ws(uint8_t client, WStype_t type, uint8_t *payload, size_t len) {
  if (type == WStype_CONNECTED) {
    if (client < WEBSOCKETS_SERVER_CLIENT_MAX) s_auth[client] = false;
    for (uint8_t i = 0; i < s_cached; i++) s_ws.sendTXT(client, s_line[i]);
    return;
  }
  if (type == WStype_DISCONNECTED) {
    if (client < WEBSOCKETS_SERVER_CLIENT_MAX) s_auth[client] = false;
    return;
  }
  if (type != WStype_TEXT) return;
  if (len >= 2 && payload[0] == 'A') {
    on_admin_line(client, payload, len);
    return;
  }
  const long ev = event_of(payload, len);
  if (ev < 0) return;
  // An older page logs in with "EV 18 <pin>" and waits for the Mega's AU.
  // Check it against the PIN here and answer both ways.
  if (ev == EV_ADMIN_UNLOCK && !client_authed(client) && len > 6 && payload[5] == ' ') {
    char b[8];
    snprintf(b, sizeof(b), "AD %.4s", (const char *)payload + 6);
    on_admin_line(client, (const uint8_t *)b, strlen(b));
    if (client_authed(client)) admin_mega(EV_ADMIN_UNLOCK, -1);
    return;
  }
  if (is_admin_event(ev)) {
    if (!client_authed(client)) {
      s_ws.sendTXT(client, "AA N");
      return;
    }
    s_auth_ms[client] = millis();
    admin_mega(EV_ADMIN_UNLOCK, -1);
  }
  Serial2.write(payload, len);
  Serial2.write('\n');
  Serial.print(F("-> "));
  Serial.write(payload, len);
  Serial.println();
}

void setup() {
  // BENCH WORKAROUND, 2026-10-25: brownout detector off. Wired to the Mega, the
  // board tripped it every few seconds even on USB with the radio turned down,
  // and rebooted in a loop. With it off the board rides through the dips. This
  // hides a supply problem rather than fixing it -- the fix is a 470-1000 uF
  // capacitor across 5V/GND at the board and a short, thick USB cable or a
  // 5 V >= 1 A supply. Remove this line once that is fitted.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial2.begin(MEGA_BAUD, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);

  // Let the supply settle before the radio's first current spike, and run the
  // radio at reduced power. At full power the AP start pulls ~400 mA peaks; on
  // the bench that tripped the brownout detector in a reboot loop as soon as
  // the board was wired to the Mega. 8.5 dBm still reaches a phone a few
  // metres away, which is all a bench test needs.
  delay(500);
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  Serial.print(F("[bridge] WiFi "));
  Serial.print(AP_SSID);
  Serial.print(F("  http://"));
  Serial.println(WiFi.softAPIP());

  s_prefs.begin("wv", true);
  s_admin_pin = s_prefs.getUShort("pin", ADM_DEFAULT_PIN);
  s_prefs.end();

  // no-store: a phone that kept an old copy of the page logs in the old way
  // and never reaches the admin menu.
  s_http.on("/", []() {
    s_http.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    s_http.sendHeader("Pragma", "no-cache");
    s_http.send_P(200, "text/html", PAGE_HTML);
  });
  s_http.begin();
  s_ws.begin();
  s_ws.onEvent(on_ws);

  ui_begin();

  // Bench 2026-10-30: the board took a few lines after boot, then froze on the
  // last screen -- no touches, nothing from the Mega -- until power-cycled. A
  // frozen loop now reboots the board instead of leaving a dead screen.
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset();

  // Heartbeat on USB: proves the loop is alive and counts what the Mega sent.
  static uint32_t s_alive_ms = 0, s_rx_lines = 0, s_rx_bytes = 0;
  if ((uint32_t)(millis() - s_alive_ms) >= ALIVE_EVERY_MS) {
    s_alive_ms = millis();
    Serial.printf("[alive] rx_lines=%lu rx_bytes=%lu\n",
                  (unsigned long)s_rx_lines, (unsigned long)s_rx_bytes);
  }

  s_http.handleClient();
  s_ws.loop();
  ui_loop();

  static char buf[BRIDGE_LINE_MAX];
  static uint8_t len = 0;
  static bool overflow = false;
  while (Serial2.available()) {
    const char ch = (char)Serial2.read();
    s_rx_bytes++;
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (!overflow && len) {
        s_rx_lines++;
        buf[len] = '\0';
        cache_put(buf);
        if (model_apply(buf)) ui_changed();
        s_ws.broadcastTXT(buf);
        Serial.print(F("<- "));
        Serial.println(buf);
      }
      len = 0;
      overflow = false;
    } else if (len < BRIDGE_LINE_MAX - 1) {
      buf[len++] = ch;
    } else {
      overflow = true;   // never relay a truncated line
    }
  }
}
