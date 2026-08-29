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
// that is not a multiple of 100 centavos. The centavo resolution exists so the
// display can format exactly and so a future price change does not require
// retyping the arithmetic.
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

// ---------------------------------------------------------------------------
// Pin map -- digital outputs
// ---------------------------------------------------------------------------
//
// The relay board is opto-isolated and active-LOW. Nothing here may be driven
// from a Mega pin directly.

#define PIN_COIN_INHIBIT  30  // HIGH = acceptor inhibited
#define PIN_VALVE         31  // Relay, active LOW
#define PIN_PUMP          32  // Relay, active LOW
#define PIN_HOPPER_P1_RUN 33  // Relay, active LOW
#define PIN_HOPPER_P5_RUN 34  // Relay, active LOW
#define PIN_BUZZER        35  // Active HIGH

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
#define HMI_BAUD        115200
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

// Sanity ceiling on a pulse train. More pulses than this in one burst is noise
// on the line -- motor noise from the pump or a hopper coupling into D2 -- not a
// coin. Discard the train rather than crediting a denomination that does not
// exist.
#define COIN_PULSE_MAX 8

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

// Debounce on the outlet count pin. Coins leave at 5-10/sec, so a real coin
// cannot arrive within 15 ms of the previous one. Anything faster is contact
// bounce, and counting bounce as coins makes the machine believe it paid change
// it did not pay.
#define HOPPER_COUNT_DEBOUNCE_MS 15

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

// Coins loaded into each hopper at commissioning. Count them physically and set
// the Admin inventory to match -- see docs/calibration.md.
#define HOPPER_START_FLOAT 100

// Physical capacity, used to bound the Admin inventory edit so a typo cannot
// claim the machine holds more change than the hopper can physically contain.
#define HOPPER_CAPACITY 500

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

// Bottle proximity debounce. A momentary flicker -- a hand passing the sensor,
// or splash-back off the bottle neck -- must not trigger the removed-bottle
// pause and interrupt a good pour. 50 ms is well above sensor flicker and well
// below the time it physically takes to lift a bottle out.
#define BOTTLE_DEBOUNCE_MS 50

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

// MEASURED PER UNIT WITH A GRADUATED CYLINDER. Do not take this from the
// datasheet -- see docs/calibration.md. Sensors of this class carry 2-5%
// tolerance and the effective figure shifts with the plumbing it sits in.
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

// Float debounce. Water sloshes, especially while a pour is running or the pump
// is filling. Without this the pump chatters at the threshold, which is hard on
// the relay and the pump both.
#define FLOAT_DEBOUNCE_MS 250

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
#define TEMP_INVALID_C -127  // DS18B20 disconnected sentinel

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
#define EEPROM_LAYOUT_VERSION 1

#define EEPROM_ADDR_HEADER     0    // magic, version, checksum
#define EEPROM_ADDR_INVENTORY  16   // hopper counts, chamber count
#define EEPROM_ADDR_OPEN_TXN   48   // interrupted transaction, restored on boot
#define EEPROM_ADDR_DAILY_RING 96   // wear-levelled daily counters
#define EEPROM_ADDR_HISTORY    256  // transaction history ring buffer

// Daily counters are written far more often than anything else here, so they
// are wear-levelled across a small ring of cells rather than hammering one
// address. 8 slots multiplies the life of the daily counter region by 8.
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
