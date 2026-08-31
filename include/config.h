#ifndef CONFIG_H
#define CONFIG_H

// Pin map, timing constants and calibration constants.
// Project EMX-2026-WATERVENDO-01, eMathrix Technologies.
//
// This file is the only place a number with meaning is allowed to live. If a
// magic number appears in a logic file, it belongs here instead.
//
// The pin map mirrors docs/wiring.md. The two must never disagree -- if a pin
// moves it moves in both places, in the same commit.
//
// Every timing constant below carries a note saying what breaks if it changes.
// That is not decoration. Several of these were chosen against physical failure
// modes that are not obvious from reading the code.

#include <stdint.h>

// ---------------------------------------------------------------------------
// Units and types
// ---------------------------------------------------------------------------
//
// Money is integer CENTAVOS. Volume is integer MILLILITRES. There is no
// floating point anywhere in the money or volume path -- see CLAUDE.md. A float
// in the change computation is how a machine loses a coin per transaction.
//
// Every coin this machine handles is a whole peso and the smallest sellable
// volume is 100 mL = P1, so in practice no arithmetic here produces a value
// that is not a multiple of 100 centavos.
//
// KEEP THE CENTAVO RESOLUTION ANYWAY. It costs nothing today and it is what
// makes a future P0.50 price point or a promo rate a change to a constant
// rather than a refactor of every money path in the firmware. Do not "simplify"
// this to whole pesos.
//
// int32_t rather than int16_t for the daily totals: a busy day at P20 a
// transaction overflows 16 bits of centavos in about 33 transactions.

typedef int32_t money_t;    // centavos
typedef int32_t volume_t;   // millilitres

#define CENTAVOS_PER_PESO 100

// ---------------------------------------------------------------------------
// Billing
// ---------------------------------------------------------------------------
//
// COINS DETERMINE VOLUME. VOLUME NEVER DETERMINES COINS.
//
// The flow sensor is a cutoff, not a cashier. Nothing may compute an amount
// owed from a measured volume. See CLAUDE.md -- this is the bug the whole
// design exists to prevent.

// Billing rate. P1 buys 100 mL.
#define ML_PER_PESO 100

// Hard ceiling per transaction. P20 -> 2000 mL.
// Raising this raises the worst-case change owed, which the hoppers must be
// able to cover before any coin is accepted. Do not raise it without redoing
// the hopper float arithmetic.
#define MAX_TRANSACTION_PESOS 20
#define MAX_TRANSACTION_CENTAVOS (MAX_TRANSACTION_PESOS * CENTAVOS_PER_PESO)
#define MAX_TRANSACTION_ML (MAX_TRANSACTION_PESOS * ML_PER_PESO)

// Partial refunds round DOWN to this multiple. A measured 305 mL is charged as
// 300 mL. Rounding always favours the machine, never the user.
// NEVER round to nearest. NEVER round up. Rounding up charges a user for water
// they did not receive, and rounding to nearest does it half the time.
#define REFUND_ROUND_ML 100

// ---------------------------------------------------------------------------
// Coin denominations
// ---------------------------------------------------------------------------
//
// The acceptor identifies denominations by pulse count. These MUST match what
// the acceptor was taught in learning mode -- see docs/calibration.md. If the
// acceptor's pulse assignment is changed in service, change it here in the same
// visit or the machine will credit the wrong amount.

#define COIN_PULSES_P1  1
#define COIN_PULSES_P5  2
#define COIN_PULSES_P10 3
#define COIN_PULSES_P20 4

#define COIN_VALUE_P1  (1  * CENTAVOS_PER_PESO)
#define COIN_VALUE_P5  (5  * CENTAVOS_PER_PESO)
#define COIN_VALUE_P10 (10 * CENTAVOS_PER_PESO)
#define COIN_VALUE_P20 (20 * CENTAVOS_PER_PESO)

// ---------------------------------------------------------------------------
// Pin map -- interrupts
// ---------------------------------------------------------------------------
//
// These two pins are the ONLY interrupts in the system. Both ISRs set a
// volatile counter and return; all interpretation happens in update().
// Adding a third interrupt is a design change, not an implementation detail.

#define PIN_COIN_PULSE 2   // INT0. Missed pulse = wrong denomination = money error
#define PIN_FLOW_PULSE 3   // INT1. Missed pulse = under-counted volume

// ---------------------------------------------------------------------------
// Pin map -- digital inputs
// ---------------------------------------------------------------------------
//
// All INPUT_PULLUP, wired active-low to ground. This is the safe default: a
// broken wire reads as the inactive state rather than as a phantom coin or a
// phantom bottle.

#define PIN_BOTTLE_PROX     22  // LOW = bottle present
#define PIN_TEMP_ONEWIRE    23  // DS18B20 waterproof probe, 4.7k pull-up to 5V
#define PIN_FLOAT_TANK_MID  24  // LOW = below mid  -> pump on
#define PIN_FLOAT_TANK_HIGH 25  // LOW = at high    -> pump off
#define PIN_FLOAT_GALLON    26  // LOW = empty      -> SAFETY LOCKOUT, pump inhibited
#define PIN_HOPPER_P1_COUNT 27  // Falling edge, polled. Confirms a coin left
#define PIN_HOPPER_P5_COUNT 28  // Falling edge, polled. Confirms a coin left
#define PIN_CHAMBER_FULL    29  // IR break-beam at fill line. LOW = beam broken = full
#define PIN_CONFIRM_BTN     40  // LOW = pressed. Advances ACCEPTING -> SELECTING

// ---------------------------------------------------------------------------
// Pin map -- I2C, real-time clock
// ---------------------------------------------------------------------------
//
// DS3231 on the Mega's hardware I2C. These pins are fixed by the chip, not
// chosen -- they are listed here so the map is complete and so nothing else
// claims them.
//
// D20/D21 have no other use in this build.

#define PIN_I2C_SDA 20
#define PIN_I2C_SCL 21

#define RTC_I2C_ADDR 0x68

// ---------------------------------------------------------------------------
// Pin map -- digital outputs
// ---------------------------------------------------------------------------
//
// The relay board is opto-isolated and active-LOW. Nothing here may be driven
// from a Mega pin directly.

#define PIN_COIN_INHIBIT  30  // HIGH = acceptor inhibited
#define PIN_VALVE         31  // Relay, active LOW
#define PIN_PUMP          32  // DC SSR, active LOW
#define PIN_HOPPER_P1_RUN 33  // Relay, active LOW
#define PIN_HOPPER_P5_RUN 34  // Relay, active LOW
#define PIN_BUZZER        35  // Active HIGH
#define PIN_COMPRESSOR    36  // AC SSR, active LOW
#define PIN_LED_LOW       37  // Active HIGH
#define PIN_LED_MID       38  // Active HIGH
#define PIN_LED_HIGH      39  // Active HIGH

// The pump and compressor are SOLID STATE, not mechanical -- SPEC 1.5.
//
// Contact arcing on a mechanical relay couples into the high-impedance pulse
// input on D2 and reads as phantom coin pulses: free water for the user and
// inventory drift for the operator, visible only under load and miserable to
// diagnose afterwards. Do not substitute a mechanical relay back in.
//
// Both SSR modules are active-LOW like the relay board, so RELAY_ON/OFF drive
// them unchanged.
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ---------------------------------------------------------------------------
// Pin map -- servo
// ---------------------------------------------------------------------------
//
// Signal only. Servo power comes from the 5V supply rail, not the Mega's
// regulator -- a stalling servo browns out the board and reboots it mid-coin.
//
// The Servo library claims Timer5 first on the Mega, which is why nothing in
// this map uses D44-D46.

#define PIN_DIVERTER_SERVO 9

// Diverter positions in degrees. MEASURE THESE ON THE ASSEMBLED CHUTE.
// These are starting values, not correct values. A diverter a few degrees off
// drops coins on the divider wall instead of into a chamber.
#define DIVERTER_ANGLE_P1_HOPPER 30
#define DIVERTER_ANGLE_P5_HOPPER 90
#define DIVERTER_ANGLE_PROFIT    150

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------

#define HMI_SERIAL      Serial2  // D16 TX2 / D17 RX2

// SPEC 1.4. The panel and the firmware must agree or the display does not
// respond at all. If a panel is found already flashed at another rate, change
// the PANEL -- or change config.h, SPECIFICATION.md and wiring.md together.
//
// Budget note for M6: a pour refresh every HMI_DISPENSE_REFRESH_MS sends on the
// order of 60 characters, which is ~62 ms of transmit time at this rate against
// a 250 ms budget. Workable, with little headroom for adding fields.
#define HMI_BAUD        9600

#define DEBUG_BAUD      115200

// ---------------------------------------------------------------------------
// Coin path timing
// ---------------------------------------------------------------------------

// Acceptor is inhibited for this long after each accepted coin, while the servo
// travels and settles.
//
// DO NOT SHORTEN THIS TO MAKE THE MACHINE FEEL FASTER. The diverter must be in
// position before the next coin arrives. A coin landing mid-travel jams the
// chute, and clearing a chute jam is a service call. 900 ms is the servo's
// worst-case full sweep plus settle plus margin.
#define COIN_LOCKOUT_MS 900

// A pulse train is considered complete when the pulse line has been idle this
// long. The acceptor emits its pulses in a burst; this gap terminates the burst
// so update() knows the count is final.
//
// Too short and a 4-pulse P20 is read as two coins -- a P20 credited as P10 and
// P10, or worse, as two separate coins that each trigger a diverter move.
// Too long and the machine feels sluggish between coins. 200 ms is comfortably
// wider than the acceptor's inter-pulse gap and far narrower than the fastest a
// human can insert two coins.
#define COIN_PULSE_GAP_MS 200

// Sanity ceiling on a pulse train. A P20 at 4 pulses is the longest legitimate
// train, so more than this in one burst is noise on the line -- motor noise from
// the pump or a hopper coupling into D2 -- or a stuck acceptor output. Discard
// the train rather than crediting a denomination that does not exist.
#define COIN_PULSE_MAX 8

// ...but do not discard forever. A stuck acceptor output line that silently
// swallows every coin is indistinguishable from a dead acceptor from the user's
// side: they put money in and nothing happens, with no fault shown and no way
// to know the machine will never respond.
//
// After this many CONSECUTIVE over-max trains, raise FAULT_ACCEPTOR and show a
// service message. The counter resets on any train that resolves to a real
// denomination, so ordinary intermittent noise never trips it -- it takes a
// genuinely stuck line or a persistently noisy one.
#define COIN_OVERMAX_FAULT_MAX 5

// ---------------------------------------------------------------------------
// Hopper timing
// ---------------------------------------------------------------------------

// The outlet counting sensor must report the expected coins within this window.
// A payout is NOT complete because the hopper was told to run -- it is complete
// when the sensor counts the coins out.
//
// 5000 ms comfortably covers a full payout at 5-10 coins/sec plus spin-up. Much
// shorter and a slow but healthy hopper is declared jammed under a paying user.
#define HOPPER_TIMEOUT_MS 5000

// Retries after an undercount, paying only the shortfall each time. After the
// last one the machine locks with CHANGE JAM -- SERVICE REQUIRED.
// Never assume a payout succeeded.
#define HOPPER_RETRY_MAX 3

// Debounce on the outlet count pin -- SPEC 1.2.
//
// Coins leave at 5-10/sec, so the real interval between two coins is 100-200 ms.
// 25 ms leaves a 4x margin against contact bounce while staying an order of
// magnitude clear of a genuine coin.
//
// TAKE THE MARGIN. Counting a bounce as a coin records change that was never
// paid AND corrupts the inventory in the same event -- the worst pair of
// consequences available in this machine, and silent in both directions.
#define HOPPER_COUNT_DEBOUNCE_MS 25

// Settle time after the motor stops before the count is declared final. A coin
// already in the outlet throat when the motor cut still needs to fall past the
// sensor. Without this the machine under-counts a payout that actually
// succeeded and retries it, over-paying the user.
#define HOPPER_SETTLE_MS 300

// ---------------------------------------------------------------------------
// Hopper inventory
// ---------------------------------------------------------------------------

// Below either of these the machine locks with LOW CHANGE -- SERVICE REQUIRED.
// Never accept money the machine cannot honour.
#define HOPPER_LOW_P1 25
#define HOPPER_LOW_P5 5

// (HOPPER_START_FLOAT was removed. Loading the change float is an operator
// action confirmed in Admin against a physical count -- see SPEC 8 -- not a
// firmware constant. A default here would only ever be wrong, and wrong in the
// direction of claiming change the machine does not hold.)

// Physical capacity, used to bound the Admin inventory edit so a typo cannot
// claim the machine holds more change than the hopper can physically contain.
#define HOPPER_CAPACITY 500

// The profit chamber is NOT a hopper and holds far more than one. Clamping it
// at HOPPER_CAPACITY silently stopped the chamber count rising past 500, which
// would understate a full day's take. Separate ceiling, generous, because its
// only job is to stop a corrupt value being believed.
#define PROFIT_CHAMBER_CAPACITY 2000

// ---------------------------------------------------------------------------
// Change payout strategy
// ---------------------------------------------------------------------------
//
// SPEC 3.4: largest-coin-first with a hard P5 reserve.
//
// P5 is the scarce coin. It arrives slowly and leaves fast in a machine where
// P15 change is a common outcome; P1 recirculates heavily and absorbs the
// pressure. Below this reserve, P5 payouts stop and change is made entirely in
// P1 so the P5 hopper is never drained to empty by a run of large transactions.
//
// Lowering this trades service uptime for change quality -- more P1-heavy
// payouts. Raising it locks the machine on LOW CHANGE more often.
#define HOPPER_RESERVE_P5 10

// ---------------------------------------------------------------------------
// Bottle and dispense timing
// ---------------------------------------------------------------------------

// Bottle wait after volume selection. Silent 0-15 s, buzzer, buzzer, cancel.
// The user gets two audible warnings before losing the transaction. Shortening
// the cancel means a user who stepped away to grab their bottle comes back to a
// refund instead of water.
#define BOTTLE_WAIT_WARN1_MS  15000  // first buzzer
#define BOTTLE_WAIT_WARN2_MS  18000  // second buzzer
#define BOTTLE_WAIT_CANCEL_MS 20000  // cancel and refund in full

#define BUZZER_BEEP_MS 300  // Non-blocking. Long enough to hear over a running pump

// Grace to replace a bottle removed mid-pour. Replaced within the window
// resumes from the volume already dispensed; not replaced ends the transaction
// and any change due is paid on confirm.
#define BOTTLE_REMOVED_GRACE_MS 10000

// Bottle proximity debounce -- SPEC 1.2.
//
// A momentary flicker -- a hand passing the sensor, or splash-back off the
// bottle neck -- must not trigger the removed-bottle pause and interrupt a good
// pour. 80 ms is well above sensor flicker and well below the time it
// physically takes to lift a bottle out.
//
// Short, because the removal grace period depends on prompt detection. This is
// the one debounce on the machine that is deliberately NOT generous.
#define BOTTLE_DEBOUNCE_MS 80

// Confirm button debounce -- SPEC 1.2. An ordinary momentary pushbutton.
#define CONFIRM_DEBOUNCE_MS 50

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

// MEASURED PER UNIT WITH A GRADUATED CYLINDER, AT THE ACTUAL DISPENSING FLOW
// RATE. Do not take this from the datasheet -- see docs/calibration.md.
//
// The 450 figure below is the YF-S201 datasheet number and it is NOT a
// constant: that sensor's pulses-per-litre shifts 5-8% between low and high
// flow. This machine runs one flow rate through one valve on a gravity-and-pump
// fed line, so a single figure is valid here -- but only if it was measured at
// that rate. Calibrating by dumping water through the sensor fast produces a
// number wrong by more than the 2-5% tolerance the calibration exists to
// control.
//
// Expressed as an exact integer ratio rather than a decimal, because there is
// no floating point in the volume path and the true figure (around 2.22 mL per
// pulse for a 450 pulse/litre sensor) does not divide cleanly. Volume is
// computed as pulses * NUM / DEN with the remainder carried, so the truncation
// error stays bounded at under one millilitre across a whole pour instead of
// accumulating once per pulse.
//
// 450 is a PLACEHOLDER. Replace it with the measured figure for this unit
// before the machine handles money.
#define FLOW_PULSES_PER_LITRE 450
#define ML_PER_PULSE_NUM 1000
#define ML_PER_PULSE_DEN FLOW_PULSES_PER_LITRE

// No flow pulses for this long while the valve is open = stall. Three physical
// causes -- blocked line, dead sensor, closed upstream tap -- and all three need
// a person, so the machine closes the valve, settles up, and locks rather than
// retrying.
//
// At any real flow rate pulses arrive continuously, so 5 s never trips on a
// normal pour. It also covers the never-started case: valve open and nothing
// arrives at all trips at 5 s like any other stall.
//
// Lengthening this leaves a user watching a dead machine holding their money.
// Shortening it risks tripping on the valve-open transient at the start of a
// pour.
#define FLOW_STALL_TIMEOUT_MS 5000

// Valve settle after the target is reached. The valve does not close
// instantaneously and a little water is still in flight. Volume that arrives
// during this window is counted toward what the user received, so they are
// never charged for water measured after their bottle was already full.
#define VALVE_CLOSE_SETTLE_MS 500

// ---------------------------------------------------------------------------
// Water level
// ---------------------------------------------------------------------------

// Float debounce -- SPEC 1.2. Water sloshes, especially while a pour is running
// or the pump is filling. Without this the pump chatters at the threshold,
// which is hard on the switching device and the pump both.
#define FLOAT_DEBOUNCE_MS 250

// The gallon bay float is debounced HARDER than the cold tank floats.
//
// It is a safety interlock rather than pump control, and the cost of the two
// error directions is not symmetric: a spurious "empty" costs an unnecessary
// lockout, while a spurious "not empty" runs the pump dry and destroys it. The
// bay is also refilled by hand, which sloshes far more than the tank ever does.
#define FLOAT_GALLON_DEBOUNCE_MS 500

// Profit chamber IR beam debounce -- SPEC 1.2. Coins tumbling past the beam
// break it momentarily; only a stack that sits at the fill line is "full".
#define CHAMBER_DEBOUNCE_MS 500

// Minimum time the pump stays off after stopping, before it may start again.
// Back-to-back starts on a diaphragm pump shorten its life; this enforces a
// rest even if the floats say otherwise.
#define PUMP_MIN_OFF_MS 3000

// Absolute ceiling on a single pump run. A pump that has run this long without
// reaching the high float is pumping air, pumping against a blockage, or
// looking at a failed float. Any of those destroys it. Stop and raise a fault.
#define PUMP_MAX_RUN_MS 300000  // 5 minutes

// ---------------------------------------------------------------------------
// Temperature
// ---------------------------------------------------------------------------

// Status screen only. NEVER gates billing or dispensing -- a failed temperature
// probe must not be able to stop the machine selling water.
#define TEMP_READ_INTERVAL_MS 5000

// Temperature is carried in TENTHS of a degree Celsius as an integer.
//
// The client mockup's System Status screen shows "4.8 degC", so whole degrees
// cannot render it. Tenths in an int16_t keeps one decimal place with no
// floating point anywhere -- the display path formats it as value/10 and
// value%10, the same integer trick used for pesos and centavos.
//
// The DS18B20's native resolution is 1/128 degC, so tenths costs no accuracy.
#define TEMP_INVALID_TENTHS -1270  // disconnected sentinel, was -127 degC
#define TEMP_INVALID_C -127        // whole-degree sentinel, kept for callers

// A DS18B20 conversion takes up to 750 ms at 12-bit resolution. The read is
// asynchronous -- request, then collect on a later pass. NEVER block waiting.
#define TEMP_CONVERSION_MS 800

// ---------------------------------------------------------------------------
// Cooling -- SPEC 5.4
// ---------------------------------------------------------------------------
//
// Thermostat with hysteresis on the DS18B20 reading. Compressor ON above the
// upper setpoint, OFF below the lower.
//
// STARTING VALUES. Measure against the actual tank and compressor at
// commissioning -- see docs/calibration.md. A band this wide is deliberate: a
// narrow band short-cycles the compressor, and short-cycling is how they die.
#define COOL_SETPOINT_ON_C  12  // at or above this, start cooling
#define COOL_SETPOINT_OFF_C  8  // at or below this, stop

// Minimum time the compressor stays off before it may restart.
//
// Restarting a compressor against residual head pressure stalls the motor: it
// draws locked-rotor current until the thermal overload trips. Three minutes is
// the usual figure for the small compressors these dispensers use. DO NOT
// SHORTEN THIS to make the water cold faster.
#define COMPRESSOR_MIN_OFF_MS 180000  // 3 minutes

// Absolute ceiling on a single compressor run. Past this it is not keeping up:
// low charge, a failed fan, or a probe reading the wrong place. Stop and report
// rather than running it to destruction.
#define COMPRESSOR_MAX_RUN_MS 1800000  // 30 minutes

// ---------------------------------------------------------------------------
// Real-time clock
// ---------------------------------------------------------------------------
//
// DS3231, temperature-compensated, battery-backed. NOT a DS1307 and NOT the
// Mega's own millis() timekeeping.
//
// The daily profit total is a number the owner counts money against. A "daily"
// total that silently means "since the last power cut" is worse than no total
// at all, because the operator cannot tell which one they are reading and will
// trust it either way. That is what the battery is for.
//
// The DS3231 is temperature-compensated and holds a couple of minutes a YEAR;
// a DS1307 drifts that much in a MONTH inside a cabinet that runs a compressor.
// Check the module is not a DS3231M -- the M part is a lower-grade oscillator
// sold in the same footprint and is much worse.

// An implausible reading is treated as a FAILED CLOCK, never trusted.
//
// The lower bound is the year this firmware was written: the clock cannot
// legitimately read earlier than the code that reads it. A dead battery, a
// corrupt I2C read or a knockoff part all land outside this window, and the
// machine shows a clock-not-set state rather than stamping a receipt with a
// date that is confidently wrong.
#define RTC_MIN_YEAR 2026
#define RTC_MAX_YEAR 2099

// Poll interval. The clock is read for display and for the midnight rollover;
// neither needs sub-second accuracy, and I2C traffic in the main loop costs
// time that the coin path needs.
#define RTC_READ_INTERVAL_MS 1000

// I2C bus timeout. NOT OPTIONAL -- this is what keeps a failed clock from
// freezing the whole machine.
//
// The Arduino Wire library spins on the TWI interrupt flag with NO timeout by
// default: twi.c has five unbounded `while` loops. A DS3231 that fails with SDA
// held low -- a dead module, a wiring short, or a brownout part-way through a
// transfer -- hangs the caller forever. rtc_update() runs in loop(), so that is
// the entire machine stopped: no coin pulses, no flow counting, no hopper
// polling, mid-transaction, with the user's money inside it.
//
// A real 7-byte read at 100 kHz takes about 1 ms, so 3 ms is a 3x margin on a
// healthy bus and bounds the worst case below LOOP_WARN_US on a broken one.
//
// Paired with reset_with_timeout = true, which resets the TWI hardware on
// expiry -- without that the peripheral stays wedged and every later read fails
// too, turning a momentary glitch into a permanently dead clock.
#define RTC_I2C_TIMEOUT_US 3000

// Timestamp meaning "no valid time". Written into history entries recorded
// while the clock is failed, so a later reader can tell an unknown time from a
// real one instead of seeing a plausible-looking 1970.
#define RTC_TIMESTAMP_INVALID 0UL

// ---------------------------------------------------------------------------
// HMI
// ---------------------------------------------------------------------------

// Every Nextion command terminates with three 0xFF bytes.
#define HMI_TERMINATOR_BYTE 0xFF
#define HMI_TERMINATOR_COUNT 3

// Fixed command buffer. No String, no dynamic allocation -- CLAUDE.md. The
// longest command this firmware sends is a setText with a formatted amount,
// comfortably inside this.
#define HMI_TX_BUFFER 64
#define HMI_RX_BUFFER 16

// Screen refresh during a pour. Fast enough that the progress bar looks live,
// slow enough that the serial line is not saturated while flow pulses are
// arriving on an interrupt.
#define HMI_DISPENSE_REFRESH_MS 250

// How long the Thank You screen holds before returning to Standby, if the user
// does not dismiss it. Long enough to read the change due and collect it.
#define HMI_THANKYOU_HOLD_MS 8000

// ---------------------------------------------------------------------------
// EEPROM
// ---------------------------------------------------------------------------
//
// Write per transaction, never per loop iteration. EEPROM is rated for roughly
// 100,000 writes per cell. Use EEPROM.update(), never EEPROM.write().
//
// The magic number and checksum exist so a first boot or a corrupted cell is
// detected and initialised, rather than read as garbage and acted on. A corrupt
// inventory read as valid means the machine believes it holds change it does
// not have.

#define EEPROM_MAGIC 0x5756u  // 'WV'

// Layout version 2: the open-transaction and coin-in-flight records became
// wear-levelled rings and moved. A version 1 record is rejected by
// record_unpack() and the region initialises fresh, which is the correct
// outcome -- misreading an old layout would report inventory that never existed.
#define EEPROM_LAYOUT_VERSION 2

#define EEPROM_ADDR_HEADER     0    // magic, version, checksum
#define EEPROM_ADDR_INVENTORY  16   // hopper counts, chamber count

// Persistent fault flags -- SPEC 6.1 and 7.1.
//
// A persistent fault that clears on power cycle is worse than not claiming
// persistence at all: the operator learns that the fix is a reboot, the coins
// stay jammed, and the machine returns to accepting money it cannot pay out.
//
// Sits in the gap between the inventory record and the open transaction. The
// static_asserts in persist.cpp enforce that it fits.
#define EEPROM_ADDR_FAULTS     32

#define EEPROM_ADDR_DAILY_RING 96   // wear-levelled daily counters
#define EEPROM_ADDR_HISTORY    256  // transaction history ring buffer

// ---------------------------------------------------------------------------
// The two hot regions, and why they are rings
// ---------------------------------------------------------------------------
//
// AVR EEPROM is rated ~100,000 writes PER CELL. Both regions below are written
// several times per transaction at what used to be one fixed address, and both
// would have worn out inside a year of real service. The full arithmetic is in
// docs/decisions.md; the summary is here so nobody shrinks a ring without
// meeting the numbers first.
//
// Demand assumption: 100 transactions/day. That is not a paranoid figure -- a
// school with cheap cold water, no competition on site, and demand that
// concentrates at lunch. Size for the day it works, not the average day.

#define EEPROM_ADDR_OPEN_TXN_RING 1024   // 32 slots x 32 B -> 1024..2048
#define EEPROM_ADDR_INFLIGHT_RING 2048   // 64 slots x 12 B -> 2048..2816

// Open transaction. Written on open, on EVERY COIN, on selection, on settle,
// and on close -- worst case 24 writes/transaction (P20 paid in 20 x P1).
//
//   32 x 100,000 = 3,200,000 writes
//   24 w/txn x 100 txn/day = 2,400 writes/day
//   -> 1,333 days = 3.7 years at ABSOLUTE worst case
//   -> 8.8 years at a realistic 10 writes/txn
//
// Sized against the worst row, not the typical one. An earlier draft sized 8
// slots against the typical row and reported it as the worst case, which would
// have shipped a ring good for 333 days.
#define TXN_RING_SLOTS 32

// Coin in flight. TWO writes per coin -- one marking it before the servo moves,
// one clearing it after the diverter settles. Worse than the record above, and
// at a single address it was the shortest-lived cell in the machine:
//
//   100,000 / (40 w/txn x 100 txn/day) = 25 DAYS at worst case
//   100,000 / (12 w/txn x 100 txn/day) = 83 days at a realistic average
//
// With 64 slots: 4.4 years worst case, 14.6 years realistic. More slots than
// the transaction ring because it is written twice per coin rather than once,
// and each slot is small.
#define INFLIGHT_RING_SLOTS 64

// Daily counters: one write per transaction.
//   8 x 100,000 / 100 per day = 8,000 days = 21 years. Already adequate.
#define DAILY_RING_SLOTS 8

// Transaction history. Twenty entries of timestamp, amount in, volume out,
// change out. The Mega has 4 KB of EEPROM and CLAUDE.md rules out an SD logger,
// so this is the ceiling. A longer history is a new scope item to be quoted,
// not built.
#define HISTORY_ENTRIES 20

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

// No blocking calls, no delay() outside setup(). A blocked loop is a missed
// coin pulse. This is the watchdog on that rule: in a DEBUG build, a loop
// iteration slower than this prints a warning naming the slow module.
#define LOOP_WARN_US 5000

#endif  // CONFIG_H
