#include "slide_motor.h"
#include "config.h"
#include "relay.h"

#if SLIDE_ENABLED

namespace {

enum run_t : uint8_t { RUN_IDLE, RUN_FWD, RUN_REV, RUN_DEAD, RUN_PAUSE, RUN_FAULT };

struct limit_t {
  uint8_t pin;
  bool stable;       // true = pressed (LOW)
  bool raw;
  uint32_t since;
  uint16_t hits;     // presses seen since boot (console "lim")
};

struct motor_t {
  uint8_t pin_fwd, pin_rev;
  limit_t home, front;
  run_t run;
  run_t next;        // direction to start after the dead time
  bool cycle;        // after FRONT, come back HOME
  uint32_t t0;       // start of the current run / wait
  bool autorun;      // repeat the cycle forever (SLIDE_AUTO_CYCLE)
  bool hit_front;    // this cycle reached FRONT; HOME next = one full stroke
  uint16_t strokes;  // full HOME->FRONT->HOME strokes since boot (= coins paid)
};

motor_t s_m[2] = {
  {PIN_M1_FWD, PIN_M1_REV, {PIN_M1_HOME, false, false, 0, 0}, {PIN_M1_FRONT, false, false, 0, 0}, RUN_IDLE, RUN_IDLE, false, 0, false},
  {PIN_M2_FWD, PIN_M2_REV, {PIN_M2_HOME, false, false, 0, 0}, {PIN_M2_FRONT, false, false, 0, 0}, RUN_IDLE, RUN_IDLE, false, 0, false},
};

void limit_poll(limit_t &l, uint32_t now) {
  const bool r = digitalRead(l.pin) == LOW;
  if (r != l.raw) { l.raw = r; l.since = now; }
  else if (r != l.stable && now - l.since >= SLIDE_DEBOUNCE_MS) {
    l.stable = r;
    if (r) l.hits++;
    Serial.print(F("[slide] D")); Serial.print(l.pin);
    Serial.println(r ? F(" PINDOT") : F(" bitaw"));
  }
}

void drive(motor_t &m, run_t r) {
  relay_write(m.pin_fwd, r == RUN_FWD);
  relay_write(m.pin_rev, r == RUN_REV);
}

void log(uint8_t i, const __FlashStringHelper *what) {
  Serial.print(F("[slide] M")); Serial.print(i + 1);
  Serial.print(' ');            Serial.println(what);
}

// Begin a run in direction d, through the dead time if the other way was on.
void start(uint8_t i, run_t d) {
  motor_t &m = s_m[i];
  const bool there = (d == RUN_FWD) ? m.front.stable : m.home.stable;
  if (there) {
    drive(m, RUN_IDLE);
    m.run = RUN_IDLE;
    m.t0 = millis();
    if (d == RUN_FWD && m.cycle) { m.run = RUN_PAUSE; m.t0 = millis(); m.hit_front = true; }
    else m.cycle = false;
    log(i, d == RUN_FWD ? F("already at FRONT") : F("already at HOME"));
    return;
  }
  drive(m, RUN_IDLE);
  m.next = d;
  m.run = RUN_DEAD;
  m.t0 = millis();
}

}  // namespace

void slide_begin() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    motor_t &m = s_m[i];
    drive(m, RUN_IDLE);
    pinMode(m.home.pin, INPUT_PULLUP);
    pinMode(m.front.pin, INPUT_PULLUP);
    delayMicroseconds(50);
    m.home.stable  = m.home.raw  = digitalRead(m.home.pin)  == LOW; m.home.since  = now;
    m.front.stable = m.front.raw = digitalRead(m.front.pin) == LOW; m.front.since = now;
    m.t0 = now;
    m.autorun = SLIDE_AUTO_CYCLE;
#if SLIDE_HOME_AT_BOOT || SLIDE_AUTO_CYCLE
    start(i, RUN_REV);
#endif
  }
}

void slide_update() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    motor_t &m = s_m[i];
    limit_poll(m.home, now);
    limit_poll(m.front, now);

    switch (m.run) {
      case RUN_DEAD:
        if (now - m.t0 >= SLIDE_DEADTIME_MS) {
          m.run = m.next;
          m.t0 = now;
          drive(m, m.run);
          log(i, m.run == RUN_FWD ? F("ABANTE") : F("ATRAS"));
        }
        break;
      case RUN_FWD:
        if (m.front.stable) {
          drive(m, RUN_IDLE);
          log(i, F("tumama sa FRONT"));
          m.hit_front = true;
          if (m.cycle) { m.run = RUN_PAUSE; m.t0 = now; }
          else m.run = RUN_IDLE;
        }
        break;
      case RUN_REV:
        if (m.home.stable) {
          drive(m, RUN_IDLE);
          m.run = RUN_IDLE;
          m.t0 = now;
          m.cycle = false;
          log(i, F("tumama sa HOME"));
          if (m.hit_front) {
            // A full stroke: one coin pushed out.
            m.hit_front = false;
            m.strokes++;
            Serial.print(F("[slide] M")); Serial.print(i + 1);
            Serial.print(F(" isang barya, stroke #")); Serial.println(m.strokes);
          }
        }
        break;
      case RUN_PAUSE:
        if (now - m.t0 >= SLIDE_CYCLE_PAUSE_MS) { m.cycle = false; start(i, RUN_REV); }
        break;
      case RUN_IDLE:
        if (m.autorun) {
          if (!m.home.stable) start(i, RUN_REV);
          else if (now - m.t0 >= SLIDE_HOME_WAIT_MS) { m.cycle = true; start(i, RUN_FWD); }
        }
        break;
      default:
        break;
    }

    if ((m.run == RUN_FWD || m.run == RUN_REV) && now - m.t0 >= SLIDE_TIMEOUT_MS && m.autorun) {
      // Auto never stops: no limit in time -> turn around and keep going.
      const run_t back = (m.run == RUN_FWD) ? RUN_REV : RUN_FWD;
      log(i, F("TIMEOUT -- baliktad ang ikot"));
      m.cycle = (back == RUN_FWD);
      start(i, back);
    }
    else if ((m.run == RUN_FWD || m.run == RUN_REV) && now - m.t0 >= SLIDE_TIMEOUT_MS) {
      drive(m, RUN_IDLE);
      m.run = RUN_FAULT;
      m.cycle = false;
      m.autorun = false;
      log(i, F("TIMEOUT -- walang limit na tinamaan, hinto (FAULT)"));
    }
  }
}

void slide_command(uint8_t i, slide_cmd_t c) {
  if (i > 1) return;
  motor_t &m = s_m[i];
  switch (c) {
    case SLIDE_STOP:
      drive(m, RUN_IDLE);
      m.run = RUN_IDLE;
      m.cycle = false;
      m.autorun = false;
      log(i, F("STOP"));
      break;
    case SLIDE_FWD:   m.autorun = false; m.cycle = false; start(i, RUN_FWD); break;
    case SLIDE_REV:   m.autorun = false; m.cycle = false; start(i, RUN_REV); break;
    case SLIDE_CYCLE: m.autorun = false; m.cycle = true; m.hit_front = false; start(i, RUN_FWD); break;
    case SLIDE_AUTO:
      m.autorun = true; m.cycle = false;
      drive(m, RUN_IDLE); m.run = RUN_IDLE; m.t0 = millis();
      log(i, F("AUTO cycle"));
      break;
  }
}

void slide_follow(bool want) {
  static bool on = false;
  static uint32_t last_want = 0;
  const uint32_t now = millis();
  if (want) last_want = now;
  const bool run = want || (on && now - last_want < SLIDE_CHANGE_HOLD_MS);
  if (run == on) return;
  on = run;
  for (uint8_t i = 0; i < 2; i++) {
    motor_t &m = s_m[i];
    if (on) {
      if (!m.autorun) slide_command(i, SLIDE_AUTO);
    } else {
      // Change done: back to HOME and stop there.
      m.autorun = false;
      m.cycle = false;
      log(i, F("tapos ang sukli -- uwi sa HOME"));
      start(i, RUN_REV);
    }
  }
}

uint16_t slide_strokes(uint8_t i) { return i < 2 ? s_m[i].strokes : 0; }
bool slide_busy(uint8_t i)     { return i < 2 && s_m[i].run != RUN_IDLE && s_m[i].run != RUN_FAULT; }
bool slide_fault(uint8_t i)    { return i < 2 && s_m[i].run == RUN_FAULT; }
bool slide_at_home(uint8_t i)  { return i < 2 && s_m[i].home.stable; }
bool slide_at_front(uint8_t i) { return i < 2 && s_m[i].front.stable; }

void slide_print_status() {
  static const char *const names[] = {"idle", "ABANTE", "ATRAS", "dead", "pause", "FAULT"};
  for (uint8_t i = 0; i < 2; i++) {
    const motor_t &m = s_m[i];
    Serial.print(F("[slide] M")); Serial.print(i + 1);
    Serial.print(F(" run="));     Serial.print(names[m.run]);
    Serial.print(F(" HOME(D"));   Serial.print(m.home.pin);
    Serial.print(F(")="));        Serial.print(m.home.stable ? F("PINDOT") : F("bukas"));
    Serial.print(F(" x"));        Serial.print(m.home.hits);
    Serial.print(F(" FRONT(D"));  Serial.print(m.front.pin);
    Serial.print(F(")="));        Serial.print(m.front.stable ? F("PINDOT") : F("bukas"));
    Serial.print(F(" x"));        Serial.println(m.front.hits);
  }
}

#else

void slide_begin() {}
void slide_update() {}
void slide_command(uint8_t, slide_cmd_t) {}
void slide_follow(bool) {}
uint16_t slide_strokes(uint8_t) { return 0; }
bool slide_busy(uint8_t) { return false; }
bool slide_fault(uint8_t) { return false; }
bool slide_at_home(uint8_t) { return false; }
bool slide_at_front(uint8_t) { return false; }
void slide_print_status() {}

#endif
