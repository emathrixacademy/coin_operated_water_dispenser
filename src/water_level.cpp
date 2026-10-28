#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "water_level.h"
#include "tank_level.h"
#include "faults.h"

// Water level, pump control, cooling. SPEC 5.3, 5.4.
//
// Cold tank: one A02YYUW ultrasonic looking down at the water. It drives the
// fill pump (keep the tank topped up) and decides OUT OF WATER (not enough left
// for a full P20 pour).
//
// Supply: two gallons below the machine, one capacitive sensor each at the
// reserve line. One low is a warning for the operator. BOTH LOW STOPS THE PUMP.
//
// Running the pump dry destroys it, and is the single most likely way to kill
// this machine in service. Every interlock lives at the point the output is
// WRITTEN, not at the point the decision is made.

// ---------------------------------------------------------------------------
// Supply gallons
// ---------------------------------------------------------------------------

struct gallon_t {
  uint8_t  pin;
  bool     low;          // debounced: true = dry at the sensor
  bool     candidate;    // raw level being timed
  uint32_t since_ms;
};

static gallon_t s_gal_a = { PIN_GALLON_A, false, false, 0 };
static gallon_t s_gal_b = { PIN_GALLON_B, false, false, 0 };

static bool gallon_raw_low(const gallon_t *g) {
  return digitalRead(g->pin) != GALLON_SENSOR_WET_LEVEL;
}

static void gallon_begin(gallon_t *g) {
  // The pull direction follows the polarity so that a broken wire reads DRY --
  // see GALLON_SENSOR_WET_LEVEL. A pull-up on a sensor that drives HIGH for
  // "wet" would turn a cut wire into a permanently full jug.
  pinMode(g->pin, GALLON_SENSOR_WET_LEVEL == HIGH ? INPUT : INPUT_PULLUP);
  g->low = gallon_raw_low(g);
  g->candidate = g->low;
  g->since_ms = millis();
}

static void gallon_update(gallon_t *g) {
  const bool raw = gallon_raw_low(g);
  const uint32_t now = millis();

  if (raw != g->candidate) {
    g->candidate = raw;
    g->since_ms = now;
  }
  if (raw == g->low) return;

  // Low acts at once; only "water back" waits -- GALLON_SENSOR_DEBOUNCE_MS.
  if (!raw && (uint32_t)(now - g->since_ms) < GALLON_SENSOR_DEBOUNCE_MS) return;

  g->low = raw;
#ifdef DEBUG
  Serial.print(g == &s_gal_a ? F("[gallon] A low: ") : F("[gallon] B low: "));
  Serial.println(raw ? F("YES") : F("no"));
#endif
}

// True when the supply can no longer be trusted to feed the pump.
//
// The two gallons are drawn through ONE Y fitting (bench photo 2026-10-27).
// Once either jug is down to its reserve line its hose is close to sucking
// air, and air in a shared suction line starves the pump just as surely as two
// empty jugs. So with a Y, either one low stops the pump -- GALLON_STOP_ON_EITHER.
static bool gallons_supply_low() {
  return GALLON_STOP_ON_EITHER ? (s_gal_a.low || s_gal_b.low)
                               : (s_gal_a.low && s_gal_b.low);
}

// ---------------------------------------------------------------------------
// Cold tank ultrasonic
// ---------------------------------------------------------------------------
//
// The sensor streams a frame every ~100 ms on its own. Drain whatever has
// arrived each pass -- never wait for a byte.

static a02_parser_t s_parser;
static tank_filter_t s_filter;
static uint16_t s_tank_mm = 0;       // median, meaningful only while valid
static bool s_tank_valid = false;
static uint32_t s_last_frame_ms = 0; // last PLAUSIBLE frame

// Diagnostics for bench bring-up: every checksummed frame, plausible or not.
// A sensor aimed past TANK_SONAR_EMPTY_MM reads as "lost" to the pump logic,
// and only these tell that apart from a sensor that sends nothing at all.
static uint16_t s_raw_mm = 0;
static uint32_t s_raw_frames = 0;
static uint32_t s_raw_bytes = 0;

// PWM variant: the ISR timestamps edges and publishes the last HIGH width.
// Everything else -- conversion, plausibility, filtering -- happens in
// sonar_update(), same as for the UART frames.
static volatile uint32_t s_pwm_rise_us = 0;
static volatile uint32_t s_pwm_width_us = 0;
static volatile uint32_t s_pwm_period_us = 0;
static volatile bool s_pwm_new = false;
static volatile uint32_t s_pwm_pulses = 0;
static volatile uint32_t s_pwm_edges = 0;     // every edge, noise included
static volatile bool s_pwm_have_rise = false;
static volatile uint32_t s_pwm_any_width_us = 0;

static void sonar_pwm_isr() {
  const uint32_t t = micros();
  s_pwm_edges++;
  if (digitalRead(PIN_TANK_SONAR_PWM)) {
    s_pwm_period_us = t - s_pwm_rise_us;
    s_pwm_rise_us = t;
    s_pwm_have_rise = true;
  } else {
    // A fall with no rise before it is half a glitch.
    if (!s_pwm_have_rise) return;
    s_pwm_have_rise = false;
    const uint32_t w = t - s_pwm_rise_us;
    s_pwm_any_width_us = w;   // diagnostics: every width, rejected or not
    // Shorter than the blind zone or longer than the tank is not an echo.
    // Rejected here so a glitch cannot overwrite a good width before
    // sonar_update() has read it.
    if (w < TANK_SONAR_MIN_PULSE_US || w > TANK_SONAR_MAX_PULSE_US) return;
    s_pwm_width_us = w;
    s_pwm_new = true;
    s_pwm_pulses++;
  }
}

// Interrupt-storm guard. A noisy line can fire this ISR tens of thousands of
// times a second, which starves the main loop -- hopper counting, the pump,
// everything. When the edge rate is far above what a sensor sends, stop
// listening for TANK_SONAR_STORM_HOLDOFF_MS; the sensor then reads as lost,
// which stops the fill pump and locks at STANDBY. Never wrong, just blind.
static uint32_t s_storm_check_ms = 0;
static uint32_t s_storm_edges_seen = 0;
static uint32_t s_storm_off_since = 0;
static bool s_storm_off = false;
static uint16_t s_storm_count = 0;

static void sonar_storm_guard(uint32_t now) {
  if (s_storm_off) {
    if ((uint32_t)(now - s_storm_off_since) < TANK_SONAR_STORM_HOLDOFF_MS) return;
    s_storm_off = false;
    s_pwm_have_rise = false;
    noInterrupts();
    s_storm_edges_seen = s_pwm_edges;
    interrupts();
    s_storm_check_ms = now;
    attachInterrupt(digitalPinToInterrupt(PIN_TANK_SONAR_PWM), sonar_pwm_isr, CHANGE);
    return;
  }
  if ((uint32_t)(now - s_storm_check_ms) < TANK_SONAR_STORM_WINDOW_MS) return;
  noInterrupts();
  const uint32_t edges = s_pwm_edges;
  interrupts();
  const uint32_t delta = edges - s_storm_edges_seen;
  s_storm_edges_seen = edges;
  s_storm_check_ms = now;
  if (delta > TANK_SONAR_STORM_MAX_EDGES) {
    detachInterrupt(digitalPinToInterrupt(PIN_TANK_SONAR_PWM));
    s_storm_off = true;
    s_storm_off_since = now;
    if (s_storm_count < 0xFFFF) s_storm_count++;
  }
}

// Trigger on the yellow wire, as two timed edges -- never a delay().
//
// Mode, selectable from the console ("trig <n>") while we find out which one
// this sensor wants. On the bench it measured only while the Mega was booting
// -- when pin 19 floats -- and went quiet once driven.
//   0 = pin left floating (input), the state it worked in at boot
//   1 = idle HIGH, pulled LOW for TANK_SONAR_TRIG_LOW_MS each period
//   2 = idle LOW, pulsed HIGH for TANK_SONAR_TRIG_LOW_MS each period
static uint8_t s_trig_mode = TANK_SONAR_TRIG_MODE;
static uint32_t s_trig_since = 0;
static bool s_trig_active = false;

static void sonar_trigger_apply_idle() {
  if (s_trig_mode == 0) {
    pinMode(PIN_TANK_SONAR_TRIG, INPUT);
  } else {
    pinMode(PIN_TANK_SONAR_TRIG, OUTPUT);
    digitalWrite(PIN_TANK_SONAR_TRIG, s_trig_mode == 1 ? HIGH : LOW);
  }
  s_trig_active = false;
}

void water_tank_trigger_mode(uint8_t mode) {
  s_trig_mode = mode > 2 ? 2 : mode;
  sonar_trigger_apply_idle();
}

static void sonar_trigger(uint32_t now) {
  if (s_trig_mode == 0) return;
  const uint8_t idle = (s_trig_mode == 1) ? HIGH : LOW;
  if (s_trig_active) {
    if ((uint32_t)(now - s_trig_since) >= TANK_SONAR_TRIG_LOW_MS) {
      digitalWrite(PIN_TANK_SONAR_TRIG, idle);
      s_trig_active = false;
    }
  } else if ((uint32_t)(now - s_trig_since) >= TANK_SONAR_TRIG_PERIOD_MS) {
    digitalWrite(PIN_TANK_SONAR_TRIG, idle == HIGH ? LOW : HIGH);
    s_trig_active = true;
    s_trig_since = now;
  }
}

static void sonar_update() {
  const uint32_t now = millis();

#if TANK_SONAR_PWM
  sonar_trigger(now);
  sonar_storm_guard(now);
  noInterrupts();
  const bool fresh = s_pwm_new;
  const uint32_t width = s_pwm_width_us;
  s_pwm_new = false;
  interrupts();
  if (fresh) {
    const uint32_t mm32 = width * 100UL / TANK_SONAR_US_PER_MM_X100;
    const uint16_t mm = mm32 > 0xFFFFUL ? 0xFFFF : (uint16_t)mm32;
    s_raw_mm = mm;
    s_raw_frames++;
    if (tank_reading_plausible(mm)) {
      tank_filter_add(&s_filter, mm);
      s_last_frame_ms = now;
    }
  }
#endif

  while (TANK_SONAR_SERIAL.available()) {
    uint16_t mm = 0;
    s_raw_bytes++;
    if (!a02_parser_feed(&s_parser, (uint8_t)TANK_SONAR_SERIAL.read(), &mm)) continue;
    s_raw_mm = mm;
    s_raw_frames++;
    // Blind-zone and past-the-floor readings are not levels. Dropped rather
    // than filtered, and they do not count as a sign of life.
    if (!tank_reading_plausible(mm)) continue;
    tank_filter_add(&s_filter, mm);
    s_last_frame_ms = now;
  }

  const bool was_valid = s_tank_valid;
  if ((uint32_t)(now - s_last_frame_ms) >= TANK_SONAR_TIMEOUT_MS) {
    // Lost. Start the median over: a level carried across a dropout is stale.
    tank_filter_reset(&s_filter);
    s_tank_valid = false;
  } else {
    s_tank_valid = tank_filter_median(&s_filter, &s_tank_mm);
  }

#ifdef DEBUG
  if (was_valid != s_tank_valid) {
    Serial.println(s_tank_valid ? F("[tank] sensor OK")
                                : F("[tank] sensor LOST -- fill pump stopped"));
  }
#else
  (void)was_valid;
#endif
}

// ---------------------------------------------------------------------------
// Pump
// ---------------------------------------------------------------------------

static bool s_pump_on = false;
static uint32_t s_pump_off_since = 0;   // when the pump last stopped
static uint32_t s_pump_on_since = 0;    // when the current run started
static bool s_pump_overrun = false;

// Manual fill from the test console ("fill <s>"). A person is standing at the
// machine watching the tank, so the sensor interlocks are set aside for this
// run only; the run ceiling and the rest period still apply. Exists because a
// first fill can deadlock: an empty tank the sensor cannot see, or gallon
// sensors not yet adjusted, both hold the pump off -- and the tank never fills.
static uint32_t s_manual_since = 0;
static uint32_t s_manual_ms = 0;

static bool manual_fill_active() {
  return s_manual_ms != 0 && (uint32_t)(millis() - s_manual_since) < s_manual_ms;
}

// Volume-matched refill (team request 2026-10-28): after every pour, the fill
// pump puts back what was poured, timed at FILL_MS_PER_100ML. Counted down only
// while the pump is actually running, so a rest period or a refusal does not
// lose any of it. Stops early if the tank sensor sees the tank full.
static uint32_t s_refill_owed_ms = 0;
static uint32_t s_refill_tick_ms = 0;

void water_refill_add(volume_t ml) {
  if (ml <= 0) return;
  s_refill_owed_ms += (uint32_t)ml * FILL_MS_PER_100ML / 100UL;
  if (s_refill_owed_ms > REFILL_OWED_MAX_MS) s_refill_owed_ms = REFILL_OWED_MAX_MS;
}

uint32_t water_refill_owed_ms() {
  return s_refill_owed_ms;
}

static bool refill_active() {
  return s_refill_owed_ms > 0;
}

void water_manual_fill(uint32_t seconds) {
  if (seconds > MANUAL_FILL_MAX_S) seconds = MANUAL_FILL_MAX_S;
  s_manual_ms = seconds * 1000UL;
  s_manual_since = millis();
}

// =========================================================================
// THE SINGLE CHOKE POINT FOR THE PUMP OUTPUT -- SPEC 5.3, SPEC 9 invariant 1.
//
// Nothing else in the firmware may write PIN_PUMP. Every safety condition lives
// inside this function so that no later edit can add a path that energises the
// pump without passing all of them, however convenient that path looks.
//
// Order matters: the dry-run interlock is checked FIRST and returns
// immediately, so no subsequent condition can override it.
// =========================================================================
static void pump_write(bool want_on) {
  const uint32_t now = millis();

  // 1. SAFETY INTERLOCK. Unconditional, checked first, no exceptions.
  //    Dry running destroys the pump. Whatever the tank wants, a supply
  //    gallon at its reserve line (see gallons_supply_low()) means the pump
  //    does not run. So does a tank sensor that has gone quiet: blind, the
  //    pump can overflow the tank or empty a gallon the sensors have not
  //    caught yet. The one exception is a console manual fill, watched by a
  //    person.
  //    A volume-matched refill is the same kind of exception while
  //    REFILL_IGNORES_SENSORS is 1: it pumps a measured amount, not "until a
  //    sensor says stop", so it does not need the tank sensor to run.
  if ((gallons_supply_low() || !s_tank_valid) && !manual_fill_active() &&
      !(REFILL_IGNORES_SENSORS && refill_active())) {
    digitalWrite(PIN_PUMP, RELAY_OFF);
    if (s_pump_on) {
      s_pump_on = false;
      s_pump_off_since = now;
    }
    return;
  }

  // 1b. A latched overrun keeps the pump off until reboot/service. Without this
  //     the tank still asks for water (that is the overrun case), so the pump
  //     would rest PUMP_MIN_OFF_MS and run dry for another PUMP_MAX_RUN_MS,
  //     forever.
  if (s_pump_overrun) {
    digitalWrite(PIN_PUMP, RELAY_OFF);
    if (s_pump_on) {
      s_pump_on = false;
      s_pump_off_since = now;
    }
    return;
  }

  // 2. Run ceiling. A pump that has run this long without filling the tank is
  //    pumping air, pumping against a blockage, or looking at a sensor that is
  //    lying. All three destroy it.
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
  //    shorten its life, so this is enforced even when the tank asks for it.
  if (want_on && !s_pump_on &&
      (uint32_t)(now - s_pump_off_since) < PUMP_MIN_OFF_MS) {
    return;   // leave it off; the caller will ask again next pass
  }

  if (want_on == s_pump_on) return;   // no change, no write

  digitalWrite(PIN_PUMP, want_on ? RELAY_ON : RELAY_OFF);
  s_pump_on = want_on;
#ifdef DEBUG
  Serial.println(want_on ? F("[fill pump] ON") : F("[fill pump] OFF"));
#endif
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
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_COMPRESSOR, OUTPUT);
  pinMode(PIN_LED_LOW, OUTPUT);
  pinMode(PIN_LED_MID, OUTPUT);
  pinMode(PIN_LED_HIGH, OUTPUT);

  // Both loads off before anything else can ask for them.
  digitalWrite(PIN_PUMP, RELAY_OFF);
  digitalWrite(PIN_COMPRESSOR, RELAY_OFF);

  gallon_begin(&s_gal_a);
  gallon_begin(&s_gal_b);

#if TANK_SONAR_PWM
  // Pull-up: on the bench the line floated between pulses and picked up
  // ~10 kHz of noise, every edge of it an interrupt. The sensor drives the line
  // during a pulse, so the pull-up costs nothing when it is talking.
  pinMode(PIN_TANK_SONAR_PWM, INPUT_PULLUP);
  sonar_trigger_apply_idle();
  attachInterrupt(digitalPinToInterrupt(PIN_TANK_SONAR_PWM), sonar_pwm_isr, CHANGE);
#else
  TANK_SONAR_SERIAL.begin(TANK_SONAR_BAUD);
#endif
  a02_parser_reset(&s_parser);
  tank_filter_reset(&s_filter);
  s_tank_valid = false;
  s_last_frame_ms = millis();

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
  gallon_update(&s_gal_a);
  gallon_update(&s_gal_b);
  sonar_update();

  // Pump hysteresis: REFILL turns it on, FULL turns it off.
  //
  // The decision is only a REQUEST. pump_write() applies the dry-run
  // interlock, the rest period and the run ceiling, and may refuse it.
  // A manual fill still ends the moment the sensor sees the tank full -- the
  // person watching should not have to be the only thing stopping an overflow.
  if (manual_fill_active() && s_tank_valid && s_tank_mm <= TANK_SONAR_FULL_MM) {
    s_manual_ms = 0;
#ifdef DEBUG
    Serial.println(F("[fill pump] tank FULL (ultrasonic) -- manual fill stopped"));
#endif
  }
  // Refill: pay down the owed run time while the pump is on; a full tank (if
  // the sensor can see it) cancels the rest.
  {
    const uint32_t now_ms = millis();
    if (refill_active()) {
      if (s_tank_valid && s_tank_mm <= TANK_SONAR_FULL_MM) {
        s_refill_owed_ms = 0;
      } else if (s_pump_on) {
        const uint32_t ran = now_ms - s_refill_tick_ms;
        s_refill_owed_ms = (ran >= s_refill_owed_ms) ? 0 : s_refill_owed_ms - ran;
      }
    }
    s_refill_tick_ms = now_ms;
  }
  pump_write((manual_fill_active() || refill_active())
                 ? true
                 : tank_pump_want(s_tank_mm, s_tank_valid, s_pump_on));

  temperature_update();
  cooling_update();
  leds_update();
}

bool water_tank_sensor_ok()        { return s_tank_valid; }
uint16_t water_tank_distance_mm()  { return s_tank_valid ? s_tank_mm : 0; }
uint8_t water_tank_percent()       { return tank_percent(s_tank_mm, s_tank_valid); }
uint16_t water_tank_raw_mm()       { return s_raw_mm; }
uint32_t water_tank_raw_frames()   { return s_raw_frames; }
uint32_t water_tank_raw_bytes()    { return s_raw_bytes; }

void water_tank_pwm_diag(uint32_t *width_us, uint32_t *period_us, uint32_t *pulses) {
  noInterrupts();
  *width_us = s_pwm_width_us;
  *period_us = s_pwm_any_width_us;
  *pulses = s_pwm_pulses;
  interrupts();
}

uint16_t water_tank_storm_count() { return s_storm_count; }

bool water_gallon_low(uint8_t which) {
  return which == 0 ? s_gal_a.low : s_gal_b.low;
}

uint8_t water_gallons_low_count() {
  return (uint8_t)((s_gal_a.low ? 1 : 0) + (s_gal_b.low ? 1 : 0));
}

water_level_t water_tank_level() {
  // Same bands the pump works to, so the LEDs and the pump never disagree.
  if (!s_tank_valid || !tank_can_serve(s_tank_mm, true)) return WATER_LEVEL_LOW;
  if (s_tank_mm <= TANK_SONAR_REFILL_MM) return WATER_LEVEL_HIGH;
  return WATER_LEVEL_MID;
}

bool water_is_locked_out() {
  // Not enough in the cold tank for a full P20 pour, or no way to tell. The
  // machine cannot guarantee it can serve the next user, so it must stop
  // taking money. SPEC 6.1, transient. The gallons do NOT lock: with both low
  // the pump stops, and the tank keeps selling until it reaches this line.
  return !tank_can_serve(s_tank_mm, s_tank_valid);
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
