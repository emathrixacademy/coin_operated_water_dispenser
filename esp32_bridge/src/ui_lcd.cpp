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
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "model.h"

#ifndef LCD_INVERT
#define LCD_INVERT false
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
      cfg.pin_int = 21;
      cfg.pin_rst = 25;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x5D;
      cfg.pin_sda = 33;
      cfg.pin_scl = 32;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;

static uint16_t C_BG, C_CARD, C_INK, C_MUTE, C_ACC, C_OK, C_WARN, C_BAD, C_DIM;

static const char *const FAULT_TEXT[] = {
  "", "OUT OF WATER - PLEASE REFILL", "LOW CHANGE - SERVICE REQUIRED", "COIN STORAGE FULL",
  "CHANGE JAM - SERVICE REQUIRED", "SERVICE REQUIRED (flow)", "SERVICE REQUIRED (pump)",
  "SERVICE REQUIRED (coin slot)"
};

// The Mega is "lost" after this long with no line. It re-sends its status
// every 2 s, so 5 s is two missed rounds, never a slow moment.
#define MEGA_LOST_MS 5000

// --- Buttons ---------------------------------------------------------------

struct btn_t {
  int16_t x, y, w, h;
  int ev;
  long payload;
  bool enabled;
};
static btn_t s_btn[24];
static uint8_t s_nbtn = 0;

// The screen uses a plain "P" for the peso sign: the built-in fonts have no
// peso glyph.
static void peso(char *out, size_t n, int32_t c) {
  if (c % 100) snprintf(out, n, "P%ld.%02ld", (long)(c / 100), (long)(c % 100));
  else snprintf(out, n, "P%ld", (long)(c / 100));
}

static void button(int x, int y, int w, int h, const char *label, const char *sub,
                   int ev, long payload, bool enabled, bool primary) {
  const uint16_t fill = !enabled ? C_DIM : primary ? C_ACC : C_CARD;
  lcd.fillRoundRect(x, y, w, h, 8, fill);
  if (enabled && !primary) lcd.drawRoundRect(x, y, w, h, 8, C_MUTE);
  lcd.setTextColor(enabled ? C_INK : C_MUTE);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextDatum(sub ? bottom_center : middle_center);
  lcd.drawString(label, x + w / 2, sub ? y + h / 2 + 3 : y + h / 2);
  if (sub) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextDatum(top_center);
    lcd.drawString(sub, x + w / 2, y + h / 2 + 6);
  }
  if (s_nbtn < sizeof(s_btn) / sizeof(s_btn[0])) {
    s_btn[s_nbtn++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, ev, payload, enabled };
  }
}

static void text(const char *s, int y, const lgfx::IFont *f, uint16_t c) {
  lcd.setFont(f);
  lcd.setTextColor(c);
  lcd.setTextDatum(middle_center);
  lcd.drawString(s, 240, y);
}

// --- Top bar: tank level and the small gallon notice -----------------------

static void draw_top() {
  lcd.fillRect(0, 0, 480, 34, C_CARD);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextColor(C_INK);
  lcd.setTextDatum(middle_left);
  lcd.drawString("WATER VENDO", 10, 17);

  lcd.setFont(&fonts::Font2);
  const bool lost = (uint32_t)(millis() - g_m.seen_ms) > MEGA_LOST_MS;
  if (lost) {
    lcd.setTextColor(C_BAD);
    lcd.drawString("Walang signal mula sa Mega", 150, 17);
    return;
  }
  if (g_m.gal_a || g_m.gal_b) {
    lcd.setTextColor(C_WARN);
    lcd.drawString(g_m.gal_a && g_m.gal_b ? "! Parehong galon paubos"
                   : g_m.gal_a ? "! Galon A paubos" : "! Galon B paubos", 150, 17);
  }

  // Tank bar, right side.
  lcd.setTextColor(C_MUTE);
  lcd.setTextDatum(middle_right);
  lcd.drawString("Tangke", 330, 17);
  lcd.fillRoundRect(338, 11, 90, 12, 6, C_DIM);
  if (g_m.tank_ok) {
    lcd.fillRoundRect(338, 11, (int)(90 * g_m.tank / 100), 12, 6, C_ACC);
    char b[8];
    snprintf(b, sizeof(b), "%ld%%", (long)g_m.tank);
    lcd.setTextColor(C_INK);
    lcd.drawString(b, 472, 17);
  } else {
    lcd.setTextColor(C_BAD);
    lcd.drawString("--", 472, 17);
  }
}

// --- Body ------------------------------------------------------------------

static void draw_progress() {
  const int32_t t = g_m.target > 0 ? g_m.target : 1;
  int32_t pc = g_m.target > 0 ? g_m.poured * 100 / t : 100;
  if (pc > 100) pc = 100;
  lcd.fillRoundRect(40, 160, 400, 30, 15, C_DIM);
  lcd.fillRoundRect(40, 160, 400 * pc / 100, 30, 15, C_OK);
  lcd.fillRect(0, 200, 480, 30, C_BG);
  char b[32];
  snprintf(b, sizeof(b), "%ld / %ld mL", (long)g_m.poured, (long)g_m.target);
  text(b, 215, &fonts::FreeSans9pt7b, C_MUTE);
}

static void draw_body() {
  lcd.fillRect(0, 36, 480, 284, C_BG);
  s_nbtn = 0;
  char b[48], c[16];
  peso(c, sizeof(c), g_m.credit);

  switch (g_m.state) {
    case ST_ACCEPTING:
      text("Credit", 80, &fonts::FreeSans9pt7b, C_MUTE);
      text(c, 125, &fonts::FreeSansBold24pt7b, C_INK);
      text("Maghulog pa, o pindutin ang CONFIRM", 180, &fonts::FreeSans9pt7b, C_MUTE);
      button(140, 215, 200, 64, "CONFIRM", NULL, EV_CONFIRM, 0, true, true);
      break;

    case ST_SELECTING: {
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(C_MUTE);
      lcd.setTextDatum(top_left);
      lcd.drawString("Credit", 12, 44);
      lcd.setFont(&fonts::FreeSansBold18pt7b);
      lcd.setTextColor(C_INK);
      lcd.drawString(c, 12, 58);
      button(300, 44, 170, 42, "Tapos / sukli", NULL, EV_FINISH, 0, true, false);
      for (int p = 1; p <= 20; p++) {
        const int i = p - 1, col = i % 5, row = i / 5;
        const long ml = p * 100L;
        char lab[12], sub[8];
        if (ml < 1000) snprintf(lab, sizeof(lab), "%ld mL", ml);
        else if (ml % 1000 == 0) snprintf(lab, sizeof(lab), "%ld L", ml / 1000);
        else snprintf(lab, sizeof(lab), "%ld.%ld L", ml / 1000, (ml % 1000) / 100);
        snprintf(sub, sizeof(sub), "P%d", p);
        button(8 + col * 94, 96 + row * 54, 88, 48, lab, sub, EV_SELECT_VOLUME, ml,
               ml <= g_m.maxml, false);
      }
      break;
    }

    case ST_AWAITING:
      text("Ilagay ang bote", 125, &fonts::FreeSansBold18pt7b, C_INK);
      snprintf(b, sizeof(b), "%ld mL ang ilalabas", (long)g_m.target);
      text(b, 170, &fonts::FreeSans9pt7b, C_MUTE);
      button(170, 215, 140, 56, "Bumalik", NULL, EV_BACK, 0, true, false);
      break;

    case ST_DISPENSING:
    case ST_SETTLING:
      text("Naglalabas ng tubig...", 110, &fonts::FreeSansBold18pt7b, C_INK);
      draw_progress();
      break;

    case ST_PAUSED:
      text("Ibalik ang bote", 130, &fonts::FreeSansBold18pt7b, C_WARN);
      text("May 10 segundo bago matapos", 175, &fonts::FreeSans9pt7b, C_MUTE);
      break;

    case ST_COMPLETE:
      text("Tapos!", 75, &fonts::FreeSansBold18pt7b, C_INK);
      snprintf(b, sizeof(b), "Nailabas: %ld mL   Natitirang credit:", (long)g_m.total);
      text(b, 118, &fonts::FreeSans9pt7b, C_MUTE);
      text(c, 165, &fonts::FreeSansBold24pt7b, C_INK);
      button(60, 230, 170, 60, "Dagdag", NULL, EV_DISPENSE_AGAIN, 0, true, true);
      button(250, 230, 170, 60, "Tapusin", NULL, EV_FINISH, 0, true, false);
      break;

    case ST_PAYING:
      text("Ibinabalik ang sukli...", 120, &fonts::FreeSansBold18pt7b, C_INK);
      text(c, 180, &fonts::FreeSansBold24pt7b, C_OK);
      break;

    case ST_THANKYOU:
      text("Salamat!", 120, &fonts::FreeSansBold24pt7b, C_OK);
      snprintf(b, sizeof(b), "Sukli: %s   Nailabas: %ld mL", c, (long)g_m.total);
      text(b, 180, &fonts::FreeSans9pt7b, C_MUTE);
      break;

    case ST_FAULT:
      text("Hindi muna magagamit", 110, &fonts::FreeSansBold18pt7b, C_INK);
      if (g_m.fault > 0 && g_m.fault < (int32_t)(sizeof(FAULT_TEXT) / sizeof(FAULT_TEXT[0]))) {
        text(FAULT_TEXT[g_m.fault], 170, &fonts::FreeSansBold12pt7b, C_BAD);
      }
      break;

    case ST_ADMIN:
      text("Admin", 150, &fonts::FreeSansBold18pt7b, C_INK);
      break;

    case ST_BOOT:
    case ST_STANDBY:
    default:
      text("Maghulog ng barya", 140, &fonts::FreeSansBold24pt7b, C_INK);
      text("P1 = 100 mL  -  hanggang P20", 195, &fonts::FreeSans9pt7b, C_MUTE);
      break;
  }
}

// --- Change tracking -------------------------------------------------------

struct body_sig_t { int32_t state, credit, maxml, target, total, fault; };
struct top_sig_t { int32_t tank, tank_ok, gal_a, gal_b; bool lost; };

static body_sig_t s_body;
static top_sig_t s_top;
static int32_t s_poured = -1;
static bool s_drawn = false;
static bool s_dirty = true;
static bool s_was_down = false;
static uint32_t s_check_ms = 0;

void ui_begin() {
  lcd.init();
  lcd.setRotation(1);   // landscape, 480 x 320
  lcd.setBrightness(255);
  C_BG = lcd.color565(15, 22, 28);
  C_CARD = lcd.color565(24, 34, 43);
  C_INK = lcd.color565(230, 237, 242);
  C_MUTE = lcd.color565(147, 163, 174);
  C_ACC = lcd.color565(10, 110, 189);
  C_OK = lcd.color565(62, 194, 122);
  C_WARN = lcd.color565(224, 160, 58);
  C_BAD = lcd.color565(239, 106, 85);
  C_DIM = lcd.color565(51, 66, 78);
  lcd.fillScreen(C_BG);
  g_m.seen_ms = millis();
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

  if (s_dirty) {
    s_dirty = false;
    const top_sig_t t = { g_m.tank, g_m.tank_ok, g_m.gal_a, g_m.gal_b,
                          (uint32_t)(now - g_m.seen_ms) > MEGA_LOST_MS };
    if (!s_drawn || memcmp(&t, &s_top, sizeof(t)) != 0) {
      s_top = t;
      draw_top();
    }
    const body_sig_t b = { g_m.state, g_m.credit, g_m.maxml, g_m.target, g_m.total, g_m.fault };
    if (!s_drawn || memcmp(&b, &s_body, sizeof(b)) != 0) {
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
  if (down && !s_was_down) {
    for (uint8_t i = 0; i < s_nbtn; i++) {
      const btn_t &bt = s_btn[i];
      if (!bt.enabled) continue;
      if (x < bt.x || x >= bt.x + bt.w || y < bt.y || y >= bt.y + bt.h) continue;
      lcd.drawRoundRect(bt.x, bt.y, bt.w, bt.h, 8, C_INK);   // pressed feedback
      mega_send_event(bt.ev, bt.ev == EV_SELECT_VOLUME ? bt.payload : -1);
      break;
    }
  }
  s_was_down = down;
}

#endif  // HAS_LCD
