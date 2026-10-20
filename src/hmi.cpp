#include <Arduino.h>
#include "hmi.h"
#ifdef SERIAL_CONSOLE
#include <stdlib.h>
#include <string.h>
#include "billing.h"
#include "persist.h"
#include "faults.h"
#include "coin_level.h"
#include "bottle.h"
#include "dispense.h"
#include "water_level.h"
#endif

// Milestone 2: contract only. Implementation lands in Milestone 6.
//
// Implementation notes:
//   - Fixed char[HMI_TX_BUFFER]. No String, no dynamic allocation.
//   - Amounts format from integer centavos with snprintf("%ld.%02ld") using
//     integer division and modulo. No float in the display path -- the number
//     shown is the number charged.
//   - RX is parsed incrementally in update(). Never block waiting for a byte.
//   - Refresh during a pour is rate-limited to HMI_DISPENSE_REFRESH_MS so the
//     serial line is not saturated while flow pulses arrive on an interrupt.

static page_t s_page = PAGE_STANDBY;

static void hmi_terminate() {
  for (uint8_t i = 0; i < HMI_TERMINATOR_COUNT; i++) {
    HMI_SERIAL.write((uint8_t)HMI_TERMINATOR_BYTE);
  }
}

void hmi_begin() {
  HMI_SERIAL.begin(HMI_BAUD);
  hmi_terminate();  // flush any partial command left in the display's parser
}

#ifdef SERIAL_CONSOLE
// ===========================================================================
// TEST CONSOLE -- stands in for the touchscreen until the LCD exists.
//
// Commands typed into the Serial Monitor (115200, newline) become the same
// touch events the panel will send, so the real state machine runs end to
// end. DEBUG builds only (env:test); never in a build that takes money.
// ===========================================================================
static hmi_event_t s_evt = HMI_EVENT_NONE;
static int32_t s_evt_payload = 0;
static bool s_console_confirm = false;
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
  Serial.print(F("[status] gallon_empty=")); Serial.print(water_gallon_empty() ? 1 : 0);
  Serial.print(F(" fill_pump="));         Serial.print(water_pump_running() ? F("ON") : F("off"));
  Serial.print(F(" fault="));             Serial.print((int)faults_active());
  Serial.print(F(" today=P"));            Serial.print(persist_daily_profit() / 100);
  Serial.print(F(" "));                   Serial.print(persist_daily_volume());
  Serial.println(F("mL"));
}

static void console_exec(char *cmd) {
  char *arg = strchr(cmd, ' ');
  int32_t n = 0;
  if (arg) { *arg++ = '\0'; n = atol(arg); }

  if      (!strcmp(cmd, "c"))  { s_console_confirm = true; }
  else if (!strcmp(cmd, "s"))  { s_evt = HMI_EVENT_SELECT_VOLUME; s_evt_payload = n; }
  else if (!strcmp(cmd, "f"))  { s_evt = HMI_EVENT_FINISH; }
  else if (!strcmp(cmd, "b"))  { s_evt = HMI_EVENT_BACK; }
  else if (!strcmp(cmd, "a"))  { s_evt = HMI_EVENT_DISPENSE_AGAIN; }
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

bool hmi_console_confirm() {
  const bool c = s_console_confirm;
  s_console_confirm = false;
  return c;
}
#endif

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
  // TODO(M6): incremental RX parse, rate-limited refresh.
}

void hmi_setPage(page_t page) {
  s_page = page;
  // TODO(M6)
}

page_t hmi_currentPage() {
  return s_page;
}

void hmi_setText(const char *component, const char *value) {
  (void)component;
  (void)value;
  // TODO(M6)
}

void hmi_setValue(const char *component, int32_t value) {
  (void)component;
  (void)value;
  // TODO(M6)
}

void hmi_setAmount(const char *component, money_t centavos) {
  (void)component;
  (void)centavos;
  // TODO(M6): integer format only.
}

void hmi_setVolume(const char *component, volume_t ml) {
  (void)component;
  (void)ml;
  // TODO(M6)
}

void hmi_setProgress(const char *component, uint8_t percent) {
  (void)component;
  (void)percent;
  // TODO(M6)
}

void hmi_setOptionEnabled(uint8_t option_index, bool enabled) {
  (void)option_index;
  (void)enabled;
  // TODO(M6)
}

void hmi_showFault(fault_t fault) {
  (void)fault;
  // TODO(M6): the acceptor is already inhibited by faults before this is called.
}

bool hmi_event_available() {
#ifdef SERIAL_CONSOLE
  return s_evt != HMI_EVENT_NONE;
#else
  return false;
#endif
}

hmi_event_t hmi_take_event(int32_t *payload) {
#ifdef SERIAL_CONSOLE
  const hmi_event_t e = s_evt;
  if (payload) *payload = s_evt_payload;
  s_evt = HMI_EVENT_NONE;
  s_evt_payload = 0;
  return e;
#else
  if (payload) *payload = 0;
  return HMI_EVENT_NONE;
#endif
}
