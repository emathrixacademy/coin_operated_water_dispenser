// On-board touchscreen for the ESP32-3248S035C (3.5" ST7796, GT911 capacitive
// touch). Same screens and the same events as the web page.
//
// PIN MAP BELOW IS THE BOARD'S PUBLISHED ONE, NOT YET CHECKED ON OUR UNIT.
// Wrong colours (negative image): flip LCD_INVERT. Touch dead: try i2c_addr
// 0x14 instead of 0x5D. Screen blank but backlight on: check the SPI pins.

#include "ui.h"

#ifndef HAS_LCD
void ui_begin() {}
void ui_changed() {}
void ui_loop() {}
#else

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <Wire.h>
#include <time.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "model.h"

#ifndef LCD_INVERT
#define LCD_INVERT false
#endif

// GT911 touch controller bus and address. 0x5D or 0x14 -- the boot scan says
// which one answers on this unit.
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#ifndef TOUCH_ADDR
#define TOUCH_ADDR 0x5D
#endif

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_GT911 _touch;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = LCD_INVERT;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 27;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      // No reset and no INT line: resetting the GT911 lets it re-pick its
      // address from the INT level at that moment (0x5D or 0x14), and on the
      // bench the touch went silent after init although the scan found it at
      // 0x5D. Polled, it simply stays where the scan found it.
      cfg.pin_int = -1;
      cfg.pin_rst = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;
      cfg.i2c_addr = TOUCH_ADDR;
      cfg.pin_sda = TOUCH_SDA;
      cfg.pin_scl = TOUCH_SCL;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;

// --- Palette ---------------------------------------------------------------

static uint16_t C_BG, C_CARD, C_LINE, C_INK, C_MUTE, C_ACC, C_WATER, C_OK, C_WARN, C_BAD, C_DIM;

static const char *const FAULT_TEXT[] = {
  "", "Out of water", "Not enough change in the machine", "Coin box is full",
  "Change dispenser jammed", "Water flow problem", "Refill pump problem",
  "Coin slot problem"
};

// The Mega is "lost" after this long with no line. It re-sends its status
// every 2 s, so 5 s is two missed rounds, never a slow moment.
#define MEGA_LOST_MS 5000

static const int W = 480, H = 320, TOP = 44;

// Most one customer can buy -- must match MAX_TRANSACTION_PESOS x 100 mL in
// the Mega's config.h. The Mega enforces it either way; this only keeps the
// picker from offering more.
#define MAX_BUY_ML 1500

// --- Buttons ---------------------------------------------------------------
//
// Negative event codes are handled on the screen itself (the amount picker);
// everything else is sent to the Mega.

enum { UI_MINUS = -1, UI_PLUS = -2, UI_SET = -3, UI_STATS_CLOSE = -4, UI_STATS_OPEN = -5 };

// Sales screen. Opened by the Sales button on the idle screen (team request
// 2026-10-27), or by holding the WATER VENDO title for STATS_HOLD_MS. Closes
// on Back, after STATS_IDLE_MS, or when a sale starts.
#define STATS_HOLD_MS 2000
#define STATS_IDLE_MS 60000
static bool s_stats_open = false;
static uint32_t s_stats_since = 0;

struct btn_t {
  int16_t x, y, w, h;
  int ev;
  long payload;
  bool enabled;
};
static btn_t s_btn[20];   // 15 amount tiles + Refund + DISPENSE, with room
static uint8_t s_nbtn = 0;

// Amount chosen on the picker, in mL. Local to the screen until DISPENSE
// sends it; the Mega re-checks it against the credit either way.
static long s_sel_ml = 0;

// The built-in fonts have no peso glyph, so amounts read "PHP 10".
static void money(char *out, size_t n, int32_t c) {
  if (c % 100) snprintf(out, n, "PHP %ld.%02ld", (long)(c / 100), (long)(c % 100));
  else snprintf(out, n, "PHP %ld", (long)(c / 100));
}

static void volume(char *out, size_t n, long ml) {
  if (ml < 1000) snprintf(out, n, "%ld mL", ml);
  else if (ml % 1000 == 0) snprintf(out, n, "%ld L", ml / 1000);
  else snprintf(out, n, "%ld.%ld L", ml / 1000, (ml % 1000) / 100);
}

static void text(const char *s, int x, int y, const lgfx::IFont *f, uint16_t c,
                 textdatum_t d = middle_center) {
  lcd.setFont(f);
  lcd.setTextColor(c);
  lcd.setTextDatum(d);
  lcd.drawString(s, x, y);
}

static void button(int x, int y, int w, int h, const char *label, int ev, long payload,
                   bool enabled, bool primary, const lgfx::IFont *f = &fonts::FreeSansBold12pt7b) {
  const uint16_t fill = !enabled ? C_DIM : primary ? C_ACC : C_CARD;
  lcd.fillRoundRect(x, y, w, h, 12, fill);
  if (enabled && !primary) lcd.drawRoundRect(x, y, w, h, 12, C_LINE);
  text(label, x + w / 2, y + h / 2, f, enabled ? C_INK : C_MUTE);
  if (s_nbtn < sizeof(s_btn) / sizeof(s_btn[0])) {
    s_btn[s_nbtn++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, ev, payload, enabled };
  }
}

// --- Icons (drawn, so no image files to flash) ------------------------------

static void icon_drop(int cx, int cy, int r, uint16_t c) {
  lcd.fillCircle(cx, cy + r / 3, r, c);
  lcd.fillTriangle(cx, cy - r * 3 / 2, cx - r * 9 / 10, cy, cx + r * 9 / 10, cy, c);
  lcd.fillCircle(cx - r / 3, cy + r / 4, r / 4, C_INK);   // highlight
}

static void icon_check(int cx, int cy, int r, uint16_t c) {
  lcd.fillCircle(cx, cy, r, c);
  const int t = r / 6 + 1;
  for (int i = -t; i <= t; i++) {
    lcd.drawLine(cx - r / 2, cy + i, cx - r / 8, cy + r / 3 + i, C_BG);
    lcd.drawLine(cx - r / 8, cy + r / 3 + i, cx + r / 2, cy - r / 3 + i, C_BG);
  }
}

static void icon_warn(int cx, int cy, int r, uint16_t c) {
  lcd.fillTriangle(cx, cy - r, cx - r, cy + r * 3 / 4, cx + r, cy + r * 3 / 4, c);
  lcd.fillRoundRect(cx - r / 10, cy - r / 2, r / 5 + 1, r * 3 / 4, 2, C_BG);
  lcd.fillCircle(cx, cy + r / 2, r / 8 + 1, C_BG);
}

static void icon_bottle(int cx, int cy, int h, uint16_t c) {
  const int w = h / 2;
  lcd.fillRoundRect(cx - w / 2, cy - h / 4, w, h * 3 / 4, 8, c);
  lcd.fillRect(cx - w / 5, cy - h / 2, w * 2 / 5, h / 4 + 2, c);
  lcd.fillRect(cx - w / 4, cy - h / 2 - 6, w / 2, 8, C_ACC);   // cap
  lcd.fillRect(cx - w / 2 + 4, cy + h / 8, w - 8, h * 3 / 8 - 6, C_WATER);
}

static void icon_coin(int cx, int cy, int r) {
  lcd.fillCircle(cx, cy, r, C_WARN);
  lcd.drawCircle(cx, cy, r - 5, C_BG);
  text("P", cx, cy + 1, &fonts::FreeSansBold18pt7b, C_BG);
}

// --- Header ----------------------------------------------------------------

static bool mega_lost() {
  return (uint32_t)(millis() - g_m.seen_ms) > MEGA_LOST_MS;
}

static void draw_top() {
  lcd.fillRect(0, 0, W, TOP - 4, C_CARD);
  lcd.drawFastHLine(0, TOP - 4, W, C_LINE);
  icon_drop(20, 19, 7, C_WATER);
  text("WATER VENDO", 36, 20, &fonts::FreeSansBold12pt7b, C_INK, middle_left);

  if (!mega_lost() && (g_m.gal_a || g_m.gal_b)) {
    const char *msg = g_m.gal_a && g_m.gal_b ? "Both gallons low"
                      : g_m.gal_a ? "Gallon A low" : "Gallon B low";
    lcd.fillRoundRect(210, 9, 130, 22, 11, C_WARN);
    text(msg, 275, 20, &fonts::Font2, C_BG);
  }

  // Tank level, right side.
  lcd.drawRoundRect(352, 11, 76, 18, 9, C_LINE);
  if (!mega_lost() && g_m.tank_ok) {
    const int fill = 72 * g_m.tank / 100;
    if (fill > 0) lcd.fillRoundRect(354, 13, fill, 14, 7, g_m.tank < 25 ? C_WARN : C_WATER);
    char b[8];
    snprintf(b, sizeof(b), "%ld%%", (long)g_m.tank);
    text(b, 470, 20, &fonts::Font2, C_INK, middle_right);
  } else {
    text("--", 470, 20, &fonts::Font2, C_MUTE, middle_right);
  }
}

// --- Body ------------------------------------------------------------------

static void draw_progress() {
  const int32_t t = g_m.target > 0 ? g_m.target : 1;
  int32_t pc = g_m.target > 0 ? g_m.poured * 100 / t : 100;
  if (pc > 100) pc = 100;
  lcd.fillRect(0, 120, W, 120, C_BG);
  char b[32];
  snprintf(b, sizeof(b), "%ld%%", (long)pc);
  text(b, W / 2, 150, &fonts::FreeSansBold24pt7b, C_INK);
  lcd.fillRoundRect(40, 190, 400, 24, 12, C_DIM);
  if (pc > 0) lcd.fillRoundRect(40, 190, 400 * pc / 100, 24, 12, C_WATER);
  char a[16], c[16];
  volume(a, sizeof(a), g_m.poured);
  volume(c, sizeof(c), g_m.target);
  snprintf(b, sizeof(b), "%s of %s", a, c);
  text(b, W / 2, 232, &fonts::FreeSans9pt7b, C_MUTE);
}

static void clamp_sel() {
  if (s_sel_ml > g_m.maxml) s_sel_ml = g_m.maxml;
  if (s_sel_ml > MAX_BUY_ML) s_sel_ml = MAX_BUY_ML;
  if (s_sel_ml < 100) s_sel_ml = 100;
}

// One amount tile: volume on top, price under it. Tapping selects; DISPENSE
// confirms, so a stray touch never pours.
static void tile(int x, int y, int w, int h, long ml) {
  const bool enabled = ml <= g_m.maxml;
  const bool chosen = enabled && ml == s_sel_ml;
  const uint16_t fill = !enabled ? C_DIM : chosen ? C_ACC : C_CARD;
  lcd.fillRoundRect(x, y, w, h, 10, fill);
  if (enabled && !chosen) lcd.drawRoundRect(x, y, w, h, 10, C_LINE);
  char v[12], p[12];
  volume(v, sizeof(v), ml);
  snprintf(p, sizeof(p), "PHP %ld", ml / 100);   // 100 mL = 1 peso
  text(v, x + w / 2, y + h / 2 - 8, &fonts::FreeSansBold9pt7b, enabled ? C_INK : C_MUTE);
  text(p, x + w / 2, y + h / 2 + 13, &fonts::Font2, enabled ? (chosen ? C_INK : C_WATER) : C_MUTE);
  if (s_nbtn < sizeof(s_btn) / sizeof(s_btn[0])) {
    s_btn[s_nbtn++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, UI_SET, ml, enabled };
  }
}

static void draw_select() {
  char b[32], c[24];

  // Header line: credit on the left, the current choice on the right.
  money(b, sizeof(b), g_m.credit);
  snprintf(c, sizeof(c), "Credit  %s", b);
  text(c, 14, TOP + 18, &fonts::FreeSansBold12pt7b, C_INK, middle_left);
  clamp_sel();
  const bool can_buy = g_m.maxml >= 100;
  if (can_buy) {
    volume(b, sizeof(b), s_sel_ml);
    text(b, W - 14, TOP + 18, &fonts::FreeSansBold12pt7b, C_WATER, middle_right);
  }

  // Every size from 100 mL to the ceiling in 100 mL steps: 15 tiles, 5 x 3.
  for (int i = 0; i < MAX_BUY_ML / 100; i++) {
    const int col = i % 5, row = i / 5;
    tile(10 + col * 93, TOP + 40 + row * 58, 86, 52, (long)(i + 1) * 100);
  }

  button(10, H - 58, 150, 52, "Refund", EV_FINISH, -1, true, false, &fonts::FreeSansBold12pt7b);
  button(170, H - 58, 300, 52, "DISPENSE", EV_SELECT_VOLUME, s_sel_ml, can_buy, true,
         &fonts::FreeSansBold18pt7b);
}

static void stat_card(int x, int y, const char *label, const char *value, uint16_t c) {
  lcd.fillRoundRect(x, y, 148, 66, 10, C_CARD);
  text(label, x + 12, y + 17, &fonts::Font2, C_MUTE, middle_left);
  text(value, x + 12, y + 45, &fonts::FreeSansBold12pt7b, c, middle_left);
}

static void draw_stats() {
  static const char *const MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  char b[48], v[16];

  if (g_stats.year && g_stats.month >= 1 && g_stats.month <= 12) {
    snprintf(b, sizeof(b), "Sales today  -  %u %s %u", g_stats.day,
             MON[g_stats.month - 1], g_stats.year);
  } else {
    snprintf(b, sizeof(b), "Sales  (no clock fitted)");
  }
  text(b, 14, TOP + 21, &fonts::FreeSansBold12pt7b, C_INK, middle_left);
  button(W - 104, TOP + 4, 94, 34, "Back", UI_STATS_CLOSE, 0, true, false,
         &fonts::FreeSansBold9pt7b);

  if (!g_stats.ready) {
    text("Loading...", W / 2, 180, &fonts::FreeSansBold12pt7b, C_MUTE);
    return;
  }

  money(b, sizeof(b), g_stats.income);
  stat_card(10, TOP + 46, "Total income", b, C_OK);
  volume(v, sizeof(v), g_stats.volume);
  stat_card(166, TOP + 46, "Water dispensed", v, C_WATER);
  snprintf(b, sizeof(b), "%ld", (long)g_stats.count);
  stat_card(322, TOP + 46, "Purchases", b, C_INK);

  text("Last 10 purchases", 14, TOP + 128, &fonts::Font2, C_MUTE, middle_left);
  lcd.drawFastHLine(14, TOP + 138, W - 28, C_LINE);
  if (g_stats.n == 0) {
    text("No purchases yet", W / 2, TOP + 190, &fonts::FreeSans9pt7b, C_MUTE);
    return;
  }
  for (uint8_t i = 0; i < g_stats.n; i++) {
    const sale_t &s = g_stats.sale[i];
    const int x = 14 + (i / 5) * 234, y = TOP + 153 + (i % 5) * 25;
    if (s.ts) {
      const time_t t = (time_t)s.ts;
      struct tm tmv;
      gmtime_r(&t, &tmv);   // the Mega stores local time as if it were UTC
      snprintf(b, sizeof(b), "%02d/%02d %02d:%02d", tmv.tm_mday, tmv.tm_mon + 1,
               tmv.tm_hour, tmv.tm_min);
    } else {
      snprintf(b, sizeof(b), "--/-- --:--");
    }
    text(b, x, y, &fonts::Font2, C_MUTE, middle_left);
    volume(v, sizeof(v), s.ml);
    if (s.owed > 0) {
      // Change not given: show what the machine owes, in the warning colour.
      char o[20];
      money(o, sizeof(o), s.owed);
      snprintf(b, sizeof(b), "%s owed %s", v, o + 4);   // "+4" drops "PHP "
      text(b, x + 96, y, &fonts::Font2, C_WARN, middle_left);
      continue;
    }
    money(b, sizeof(b), s.in);
    text(b, x + 96, y, &fonts::Font2, C_INK, middle_left);
    text(v, x + 160, y, &fonts::Font2, C_WATER, middle_left);
  }
}

static void draw_body() {
  lcd.fillRect(0, TOP, W, H - TOP, C_BG);
  s_nbtn = 0;
  char b[48], c[24], v[16];
  money(c, sizeof(c), g_m.credit);

  if (mega_lost()) {
    icon_drop(W / 2, 120, 26, C_DIM);
    text("Connecting to the machine...", W / 2, 205, &fonts::FreeSansBold12pt7b, C_INK);
    text("Check the cable to the controller (pins 16, 17, GND)", W / 2, 240,
         &fonts::FreeSans9pt7b, C_MUTE);
    return;
  }

  if (s_stats_open) {
    draw_stats();
    return;
  }

  switch (g_m.state) {
    case ST_ACCEPTING:
      text("Credit", W / 2, TOP + 36, &fonts::FreeSans9pt7b, C_MUTE);
      text(c, W / 2, TOP + 84, &fonts::FreeSansBold24pt7b, C_INK);
      text("Insert more coins, or tap Continue", W / 2, TOP + 132, &fonts::FreeSans9pt7b, C_MUTE);
      button(120, H - 92, 240, 70, "CONTINUE", EV_CONFIRM, -1, true, true,
             &fonts::FreeSansBold18pt7b);
      break;

    case ST_SELECTING:
      draw_select();
      break;

    case ST_AWAITING:
      icon_bottle(W / 2, 112, 84, C_INK);
      text("Place your bottle under the nozzle", W / 2, 192, &fonts::FreeSansBold12pt7b, C_INK);
      volume(v, sizeof(v), g_m.target);
      snprintf(b, sizeof(b), "%s will be dispensed", v);
      text(b, W / 2, 222, &fonts::FreeSans9pt7b, C_MUTE);
      button(150, H - 70, 180, 54, "Change amount", EV_BACK, -1, true, false,
             &fonts::FreeSansBold9pt7b);
      break;

    case ST_DISPENSING:
    case ST_SETTLING:
      text("Dispensing", W / 2, TOP + 30, &fonts::FreeSansBold12pt7b, C_MUTE);
      draw_progress();
      break;

    case ST_PAUSED:
      icon_warn(W / 2, 118, 34, C_WARN);
      text("Bottle removed", W / 2, 192, &fonts::FreeSansBold18pt7b, C_INK);
      text("Put it back within 10 seconds to continue", W / 2, 230, &fonts::FreeSans9pt7b, C_MUTE);
      break;

    case ST_COMPLETE:
      icon_check(60, TOP + 46, 26, C_OK);
      volume(v, sizeof(v), g_m.total);
      snprintf(b, sizeof(b), "%s dispensed", v);
      text(b, 100, TOP + 46, &fonts::FreeSansBold18pt7b, C_INK, middle_left);
      text("Credit left", W / 2, TOP + 104, &fonts::FreeSans9pt7b, C_MUTE);
      text(c, W / 2, TOP + 140, &fonts::FreeSansBold24pt7b, C_INK);
      button(12, H - 78, 222, 64, "Dispense more", EV_DISPENSE_AGAIN, -1, true, true);
      button(246, H - 78, 222, 64, "Finish", EV_FINISH, -1, true, false);
      break;

    case ST_PAYING:
      icon_coin(W / 2, 120, 32);
      text("Returning your change", W / 2, 192, &fonts::FreeSansBold12pt7b, C_INK);
      text(c, W / 2, 232, &fonts::FreeSansBold18pt7b, C_WARN);
      break;

    case ST_THANKYOU:
      if (g_m.unpaid > 0) {
        // The payout failed; the machine recorded what it owes and carries on.
        icon_warn(W / 2, 110, 32, C_WARN);
        text("Sorry, your change could not be given", W / 2, 172,
             &fonts::FreeSansBold12pt7b, C_INK);
        money(b, sizeof(b), g_m.unpaid);
        text(b, W / 2, 210, &fonts::FreeSansBold24pt7b, C_WARN);
        text("Please contact the attendant", W / 2, 252, &fonts::FreeSans9pt7b, C_MUTE);
        break;
      }
      icon_check(W / 2, 118, 36, C_OK);
      text("Thank you!", W / 2, 192, &fonts::FreeSansBold24pt7b, C_INK);
      volume(v, sizeof(v), g_m.total);
      snprintf(b, sizeof(b), "Dispensed %s   -   Change %s", v, c);
      text(b, W / 2, 240, &fonts::FreeSans9pt7b, C_MUTE);
      break;

    case ST_FAULT:
      icon_warn(W / 2, 112, 34, C_BAD);
      text("Out of service", W / 2, 180, &fonts::FreeSansBold18pt7b, C_INK);
      if (g_m.fault > 0 && g_m.fault < (int32_t)(sizeof(FAULT_TEXT) / sizeof(FAULT_TEXT[0]))) {
        text(FAULT_TEXT[g_m.fault], W / 2, 216, &fonts::FreeSansBold9pt7b, C_BAD);
      }
      text("Please contact the attendant", W / 2, 250, &fonts::FreeSans9pt7b, C_MUTE);
      break;

    case ST_ADMIN:
      text("Service mode", W / 2, 170, &fonts::FreeSansBold18pt7b, C_INK);
      break;

    case ST_BOOT:
    case ST_STANDBY:
    default:
      icon_drop(W / 2, 118, 32, C_WATER);
      text("Insert coins to start", W / 2, 200, &fonts::FreeSansBold18pt7b, C_INK);
      text("1 peso = 100 mL   -   up to 15 pesos (1.5 L)", W / 2, 238, &fonts::FreeSans9pt7b, C_MUTE);
      button(W - 112, H - 48, 102, 38, "Sales", UI_STATS_OPEN, 0, true, false,
             &fonts::FreeSansBold9pt7b);
      break;
  }
}

// --- Change tracking -------------------------------------------------------

// All-int32 so memcmp never trips over uninitialised padding.
struct body_sig_t {
  int32_t state, credit, maxml, target, total, unpaid, fault, lost;
  int32_t stats_open, stats_ready, stats_n, stats_count;
};
struct top_sig_t { int32_t tank, tank_ok, gal_a, gal_b, lost; };

static body_sig_t s_body;
static top_sig_t s_top;
static int32_t s_poured = -1;
static bool s_drawn = false;
static bool s_dirty = true;
static bool s_was_down = false;
static uint32_t s_check_ms = 0;
static bool s_lp_active = false;   // title held down
static bool s_lp_fired = false;
static uint32_t s_lp_since = 0;

static void open_stats() {
  s_stats_open = true;
  s_stats_since = millis();
  g_stats.ready = false;
  mega_send_event(EV_STATS, -1);
  s_dirty = true;
}

static void close_stats() {
  s_stats_open = false;
  s_dirty = true;
}

// Bring-up check: which I2C addresses answer on the touch bus. The GT911
// answers at 0x5D or 0x14 depending on how it was reset; seeing neither means
// the touch controller is on other pins (or this is the resistive board).
static void touch_bus_scan() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Serial.print(F("[touch] I2C scan:"));
  uint8_t found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  Serial.println(found ? "" : " nothing");
  Wire.end();
}

void ui_begin() {
  touch_bus_scan();
  lcd.init();
  lcd.setRotation(1);   // landscape, 480 x 320
  {
    int32_t tx = 0, ty = 0;
    Serial.printf("[touch] after init: getTouch=%d\n", (int)lcd.getTouch(&tx, &ty));
  }
  lcd.setBrightness(255);
  C_BG = lcd.color565(12, 24, 36);
  C_CARD = lcd.color565(22, 38, 56);
  C_LINE = lcd.color565(48, 70, 92);
  C_INK = lcd.color565(236, 243, 248);
  C_MUTE = lcd.color565(140, 163, 181);
  C_ACC = lcd.color565(30, 136, 229);
  C_WATER = lcd.color565(38, 198, 218);
  C_OK = lcd.color565(46, 204, 113);
  C_WARN = lcd.color565(245, 166, 35);
  C_BAD = lcd.color565(229, 83, 61);
  C_DIM = lcd.color565(42, 59, 77);
  lcd.fillScreen(C_BG);
  g_m.seen_ms = millis() - MEGA_LOST_MS - 1;   // "connecting" until the first line
}

void ui_changed() {
  s_dirty = true;
}

void ui_loop() {
  const uint32_t now = millis();
  if ((uint32_t)(now - s_check_ms) >= 500) {   // catches "Mega lost" with no line arriving
    s_check_ms = now;
    s_dirty = true;
  }

  // The sales screen gives way to a customer, and does not stay up forever.
  if (s_stats_open &&
      ((uint32_t)(now - s_stats_since) >= STATS_IDLE_MS ||
       (g_m.state != ST_STANDBY && g_m.state != ST_BOOT && g_m.state != ST_FAULT))) {
    close_stats();
  }

  if (s_dirty) {
    s_dirty = false;
    const bool lost = mega_lost();
    const top_sig_t t = { g_m.tank, g_m.tank_ok, g_m.gal_a, g_m.gal_b, lost ? 1 : 0 };
    if (!s_drawn || memcmp(&t, &s_top, sizeof(t)) != 0) {
      s_top = t;
      draw_top();
    }
    const body_sig_t b = { g_m.state, g_m.credit, g_m.maxml, g_m.target, g_m.total, g_m.unpaid,
                           g_m.fault,
                           lost ? 1 : 0, s_stats_open ? 1 : 0, g_stats.ready ? 1 : 0,
                           g_stats.n, g_stats.count };
    if (!s_drawn || memcmp(&b, &s_body, sizeof(b)) != 0) {
      // A fresh visit to the picker starts at the smallest size, 100 mL (team
      // decision 2026-10-27).
      if (b.state == ST_SELECTING && (!s_drawn || s_body.state != ST_SELECTING)) {
        s_sel_ml = 100;
      }
      s_body = b;
      draw_body();
      s_poured = g_m.poured;
    } else if (g_m.poured != s_poured &&
               (g_m.state == ST_DISPENSING || g_m.state == ST_SETTLING)) {
      // Only the bar moves during a pour; redrawing the whole body every
      // 250 ms would flicker.
      s_poured = g_m.poured;
      draw_progress();
    }
    s_drawn = true;
  }

  // Touch: act on the press, not while held, so one tap is one event.
  int32_t x = 0, y = 0;
  const bool down = lcd.getTouch(&x, &y);
  if (down && !s_was_down) Serial.printf("[touch] %ld,%ld\n", (long)x, (long)y);

  // Hold the title to open the sales screen.
  if (down && y < TOP && x < 260) {
    if (!s_lp_active) {
      s_lp_active = true;
      s_lp_fired = false;
      s_lp_since = now;
    } else if (!s_lp_fired && (uint32_t)(now - s_lp_since) >= STATS_HOLD_MS &&
               !s_stats_open && !mega_lost() &&
               (g_m.state == ST_STANDBY || g_m.state == ST_BOOT || g_m.state == ST_FAULT)) {
      s_lp_fired = true;
      open_stats();
    }
  } else if (!down) {
    s_lp_active = false;
  }

  if (down && !s_was_down) {
    for (uint8_t i = 0; i < s_nbtn; i++) {
      const btn_t bt = s_btn[i];
      if (!bt.enabled) continue;
      if (x < bt.x || x >= bt.x + bt.w || y < bt.y || y >= bt.y + bt.h) continue;
      lcd.drawRoundRect(bt.x, bt.y, bt.w, bt.h, 12, C_INK);   // pressed feedback
      if (bt.ev == UI_MINUS) { s_sel_ml -= 100; clamp_sel(); draw_body(); }
      else if (bt.ev == UI_PLUS) { s_sel_ml += 100; clamp_sel(); draw_body(); }
      else if (bt.ev == UI_SET) { s_sel_ml = bt.payload; clamp_sel(); draw_body(); }
      else if (bt.ev == UI_STATS_CLOSE) { close_stats(); }
      else if (bt.ev == UI_STATS_OPEN) { open_stats(); }
      else mega_send_event(bt.ev, bt.payload);
      break;
    }
  }
  s_was_down = down;
}

#endif  // HAS_LCD
