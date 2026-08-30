#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "water_level.h"
#include "faults.h"

// Water level, pump control, cooling. SPEC 5.3, 5.4.
//
// THE GALLON BAY FLOAT IS A SAFETY LOCKOUT, NOT PUMP CONTROL.
//
// Running the pump dry destroys it, and is the single most likely way to kill
// this machine in service. The inhibit is unconditional and lives at the point
// the output is WRITTEN, not at the point the decision is made.

// ---------------------------------------------------------------------------
// Float debouncing
// ---------------------------------------------------------------------------
//
// Water sloshes, especially while a pour is running or the pump is filling. An
// undebounced float chatters the pump relay at the threshold, which is hard on
// the switching device and the pump both.

struct debounced_t {
  uint8_t  pin;
  uint16_t window_ms;
  bool     state;        // debounced
  bool     candidate;    // raw level being timed
  uint32_t since_ms;
};

static debounced_t s_mid    = { PIN_FLOAT_TANK_MID,  FLOAT_DEBOUNCE_MS,        false, false, 0 };
static debounced_t s_high   = { PIN_FLOAT_TANK_HIGH, FLOAT_DEBOUNCE_MS,        false, false, 0 };
static debounced_t s_gallon = { PIN_FLOAT_GALLON,    FLOAT_GALLON_DEBOUNCE_MS, false, false, 0 };

// All floats are active LOW: LOW means the named condition is true.
static bool raw_of(const debounced_t *d) {
  return digitalRead(d->pin) == LOW;
}

static void debounce_begin(debounced_t *d) {
  d->state = raw_of(d);
  d->candidate = d->state;
  d->since_ms = millis();
}

static void debounce_update(debounced_t *d) {
  const bool raw = raw_of(d);
  const uint32_t now = millis();

  if (raw != d->candidate) {
    d->candidate = raw;
    d->since_ms = now;
    return;
  }
  if (raw == d->state) return;
  if ((uint32_t)(now - d->since_ms) < d->window_ms) return;

  d->state = raw;
}

// ---------------------------------------------------------------------------
// Pump
// ---------------------------------------------------------------------------

static bool s_pump_on = false;
static uint32_t s_pump_off_since = 0;   // when the pump last stopped
static uint32_t s_pump_on_since = 0;    // when the current run started
static bool s_pump_overrun = false;

// =========================================================================
// THE SINGLE CHOKE POINT FOR THE PUMP OUTPUT -- SPEC 5.3, SPEC 9 invariant 1.
//
// Nothing else in the firmware may write PIN_PUMP. Every safety condition lives
// inside this function so that no later edit can add a path that energises the
// pump without passing all of them, however convenient that path looks.
//
// Order matters: the gallon interlock is checked FIRST and returns immediately,
// so no subsequent condition can override it.
// =========================================================================
static void pump_write(bool want_on) {
  const uint32_t now = millis();

  // 1. SAFETY INTERLOCK. Unconditional, checked first, no exceptions.
  //    Dry running destroys the pump. Whatever the cold tank floats say, an
  //    empty gallon bay means the pump does not run.
  if (s_gallon.state) {
    digitalWrite(PIN_PUMP, RELAY_OFF);
    if (s_pump_on) {
      s_pump_on = false;
      s_pump_off_since = now;
    }
    return;
  }

  // 2. Run ceiling. A pump that has run this long without reaching the high
  //    float is pumping air, pumping against a blockage, or looking at a failed
  //    float. All three destroy it.
  if (want_on && s_pump_on &&
      (uint32_t)(now - s_pump_on_since) >= PUMP_MAX_RUN_MS) {
    digitalWrite(PIN_PUMP, RELAY_OFF);
    s_pump_on = false;
    s_pump_off_since = now;

    if (!s_pump_overrun) {
      s_pump_overrun = true;
      // LATCHED, not raised -- SPEC 9 invariant 8. A pump overrun is a tank
      // problem, not a till problem; if a user is mid-transaction they must
      // still be able to finish and collect their change. The state machine
      // releases latched faults once nothing is owed.
      faults_latch(FAULT_PUMP_RUNTIME);
    }
    return;
  }

  // 3. Minimum rest between runs. Back-to-back starts on a diaphragm pump
  //    shorten its life, so this is enforced even when the floats ask for it.
  if (want_on && !s_pump_on &&
      (uint32_t)(now - s_pump_off_since) < PUMP_MIN_OFF_MS) {
    return;   // leave it off; the caller will ask again next pass
  }

  if (want_on == s_pump_on) return;   // no change, no write

  digitalWrite(PIN_PUMP, want_on ? RELAY_ON : RELAY_OFF);
  s_pump_on = want_on;
  if (want_on) {
    s_pump_on_since = now;
  } else {
    s_pump_off_since = now;
  }
}

// ---------------------------------------------------------------------------
// Cooling -- SPEC 5.4
// ---------------------------------------------------------------------------

static OneWire s_wire(PIN_TEMP_ONEWIRE);
static DallasTemperature s_probe(&s_wire);
static DeviceAddress s_probe_addr;
static bool s_probe_found = false;

static int16_t s_temp_tenths = TEMP_INVALID_TENTHS;

// The DS18B20 read is a request/collect pair, never a blocking wait: a 12-bit
// conversion takes up to 750 ms and blocking for it would drop coin pulses.
static bool s_conv_pending = false;
static uint32_t s_conv_started = 0;
static uint32_t s_last_read_ms = 0;

static bool s_cooling = false;
static uint32_t s_cool_off_since = 0;
static uint32_t s_cool_on_since = 0;

static void compressor_write(bool want_on) {
  const uint32_t now = millis();

  // Minimum off-time. Restarting a compressor against residual head pressure
  // stalls the motor -- it draws locked-rotor current until the thermal
  // overload trips. This is how small compressors die.
  if (want_on && !s_cooling &&
      (uint32_t)(now - s_cool_off_since) < COMPRESSOR_MIN_OFF_MS) {
    return;
  }

  // Run ceiling. Past this it is not keeping up: low charge, a failed fan, or a
  // probe reading the wrong place. Stop rather than run it to destruction.
  //
  // No fault is raised. SPEC 5.4: cooling never blocks a transaction, and warm
  // water is a service quality issue rather than a reason to stop selling.
  if (want_on && s_cooling &&
      (uint32_t)(now - s_cool_on_since) >= COMPRESSOR_MAX_RUN_MS) {
    want_on = false;
  }

  if (want_on == s_cooling) return;

  digitalWrite(PIN_COMPRESSOR, want_on ? RELAY_ON : RELAY_OFF);
  s_cooling = want_on;
  if (want_on) {
    s_cool_on_since = now;
  } else {
    s_cool_off_since = now;
  }
}

static void temperature_update() {
  const uint32_t now = millis();

  if (!s_probe_found) return;

  if (!s_conv_pending) {
    if ((uint32_t)(now - s_last_read_ms) < TEMP_READ_INTERVAL_MS) return;
    s_probe.requestTemperatures();   // non-blocking: setWaitForConversion(false)
    s_conv_pending = true;
    s_conv_started = now;
    return;
  }

  if ((uint32_t)(now - s_conv_started) < TEMP_CONVERSION_MS) return;

  // Raw is sixteenths-of-a-sixteenth: the device reports 1/128 degC per count.
  // Converting with integer arithmetic keeps float out of the build entirely.
  const int16_t raw = s_probe.getTemp(s_probe_addr);
  if (raw == DEVICE_DISCONNECTED_RAW) {
    s_temp_tenths = TEMP_INVALID_TENTHS;
  } else {
    s_temp_tenths = (int16_t)(((int32_t)raw * 10) / 128);
  }

  s_conv_pending = false;
  s_last_read_ms = now;
}

static void cooling_update() {
  if (s_temp_tenths == TEMP_INVALID_TENTHS) {
    // No probe, no thermostat. Fail with the compressor OFF: an unread probe
    // must never leave it running indefinitely.
    compressor_write(false);
    return;
  }

  // Thermostat with hysteresis. A narrow band short-cycles the compressor, and
  // short-cycling is how they fail.
  if (s_temp_tenths >= (int16_t)(COOL_SETPOINT_ON_C * 10)) {
    compressor_write(true);
  } else if (s_temp_tenths <= (int16_t)(COOL_SETPOINT_OFF_C * 10)) {
    compressor_write(false);
  }
  // Between the setpoints: hold whatever it is doing. That gap IS the
  // hysteresis.
}

// ---------------------------------------------------------------------------
// Level indication
// ---------------------------------------------------------------------------

static void leds_update() {
  // Bar graph: light every segment up to the current level, so a single dark
  // LED reads as a fault rather than as an ambiguous level.
  const water_level_t lvl = water_tank_level();
  digitalWrite(PIN_LED_LOW,  HIGH);
  digitalWrite(PIN_LED_MID,  (lvl >= WATER_LEVEL_MID)  ? HIGH : LOW);
  digitalWrite(PIN_LED_HIGH, (lvl >= WATER_LEVEL_HIGH) ? HIGH : LOW);
}

// ---------------------------------------------------------------------------

void water_level_begin() {
  pinMode(PIN_FLOAT_TANK_MID, INPUT_PULLUP);
  pinMode(PIN_FLOAT_TANK_HIGH, INPUT_PULLUP);
  pinMode(PIN_FLOAT_GALLON, INPUT_PULLUP);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_COMPRESSOR, OUTPUT);
  pinMode(PIN_LED_LOW, OUTPUT);
  pinMode(PIN_LED_MID, OUTPUT);
  pinMode(PIN_LED_HIGH, OUTPUT);

  // Both loads off before anything else can ask for them.
  digitalWrite(PIN_PUMP, RELAY_OFF);
  digitalWrite(PIN_COMPRESSOR, RELAY_OFF);

  debounce_begin(&s_mid);
  debounce_begin(&s_high);
  debounce_begin(&s_gallon);

  s_pump_on = false;
  s_pump_overrun = false;
  s_pump_off_since = millis();
  s_cooling = false;
  s_cool_off_since = millis();

  s_probe.begin();
  // Request-and-collect, never block. This is the line that keeps a 750 ms
  // conversion out of the main loop.
  s_probe.setWaitForConversion(false);
  s_probe_found = s_probe.getAddress(s_probe_addr, 0);

  pump_write(false);
  leds_update();
}

void water_level_update() {
  debounce_update(&s_mid);
  debounce_update(&s_high);
  debounce_update(&s_gallon);

  // Pump hysteresis: mid turns it on, high turns it off.
  //
  // The decision is only a REQUEST. pump_write() applies the gallon interlock,
  // the rest period and the run ceiling, and may refuse it.
  bool want = s_pump_on;
  if (s_high.state) {
    want = false;               // tank full
  } else if (s_mid.state) {
    want = true;                // below mid
  }
  pump_write(want);

  temperature_update();
  cooling_update();
  leds_update();
}

bool water_tank_below_mid() { return s_mid.state; }
bool water_tank_at_high()   { return s_high.state; }
bool water_gallon_empty()   { return s_gallon.state; }

water_level_t water_tank_level() {
  if (s_high.state) return WATER_LEVEL_HIGH;
  if (s_mid.state)  return WATER_LEVEL_LOW;   // "below mid" is asserted
  return WATER_LEVEL_MID;
}

bool water_is_locked_out() {
  // The gallon bay is empty: the machine cannot guarantee it can serve the next
  // user, so it must stop taking money. SPEC 6.1, transient.
  return s_gallon.state;
}

bool water_pump_running() {
  return s_pump_on;
}

bool water_pump_overrun() {
  return s_pump_overrun;
}

int16_t water_temperature_tenths_c() {
  return s_temp_tenths;
}

int8_t water_temperature_c() {
  if (s_temp_tenths == TEMP_INVALID_TENTHS) return TEMP_INVALID_C;
  return (int8_t)(s_temp_tenths / 10);
}

bool water_cooling_active() {
  return s_cooling;
}
