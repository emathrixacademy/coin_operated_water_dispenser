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
#include <stdio.h>
#include <string.h>

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

#define LINE_MAX  64
#define CACHE_MAX 24
#define KEY_MAX   16

static WebServer s_http(80);
static WebSocketsServer s_ws(81);

// Latest line per field. Key = first token, or first two for VL / TX / OP,
// which name a component ("VL credit", "TX t_amount").
static char s_key[CACHE_MAX][KEY_MAX];
static char s_line[CACHE_MAX][LINE_MAX];
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
      strncpy(s_line[i], line, LINE_MAX - 1);
      return;
    }
  }
  if (s_cached >= CACHE_MAX) return;
  strncpy(s_key[s_cached], key, KEY_MAX - 1);
  strncpy(s_line[s_cached], line, LINE_MAX - 1);
  s_cached++;
}

// Only well-formed events reach the Mega: "EV", digits and spaces, short.
// The Mega validates again; this just keeps browser junk off the wire.
static bool is_event(const uint8_t *p, size_t len) {
  if (len < 4 || len > 24 || memcmp(p, "EV ", 3) != 0) return false;
  for (size_t i = 3; i < len; i++) {
    if (p[i] != ' ' && (p[i] < '0' || p[i] > '9')) return false;
  }
  return true;
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

static void on_ws(uint8_t client, WStype_t type, uint8_t *payload, size_t len) {
  if (type == WStype_CONNECTED) {
    for (uint8_t i = 0; i < s_cached; i++) s_ws.sendTXT(client, s_line[i]);
  } else if (type == WStype_TEXT && is_event(payload, len)) {
    Serial2.write(payload, len);
    Serial2.write('\n');
    Serial.print(F("-> "));
    Serial.write(payload, len);
    Serial.println();
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(MEGA_BAUD, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print(F("[bridge] WiFi "));
  Serial.print(AP_SSID);
  Serial.print(F("  http://"));
  Serial.println(WiFi.softAPIP());

  s_http.on("/", []() { s_http.send_P(200, "text/html", PAGE_HTML); });
  s_http.begin();
  s_ws.begin();
  s_ws.onEvent(on_ws);

  ui_begin();
}

void loop() {
  s_http.handleClient();
  s_ws.loop();
  ui_loop();

  static char buf[LINE_MAX];
  static uint8_t len = 0;
  static bool overflow = false;
  while (Serial2.available()) {
    const char ch = (char)Serial2.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (!overflow && len) {
        buf[len] = '\0';
        cache_put(buf);
        if (model_apply(buf)) ui_changed();
        s_ws.broadcastTXT(buf);
        Serial.print(F("<- "));
        Serial.println(buf);
      }
      len = 0;
      overflow = false;
    } else if (len < LINE_MAX - 1) {
      buf[len++] = ch;
    } else {
      overflow = true;   // never relay a truncated line
    }
  }
}
