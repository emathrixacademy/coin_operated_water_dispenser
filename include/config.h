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
// Team decision 2026-10-31: back to 2 L (P20); was P15 from 2026-10-27. A coin
// that crosses it is still credited (F8) and the excess comes back as change,
// so the hoppers must be able to pay up to P20 - P1 + P20. The display's
// MAX_BUY_ML and the phone page's MAX_ML must match.
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

// Allan universal coinslot as configured on this unit: one pulse per peso.
// (1/2/3/4 identifies a coin faster -- a P20 is 4 pulses instead of 20 -- and
// gives the servos more time before the coin reaches the gate. If the coinslot
// is reprogrammed, change these in the same visit.)
#define COIN_PULSES_P1  1
#define COIN_PULSES_P5  5
#define COIN_PULSES_P10 10
#define COIN_PULSES_P20 20

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

// Swapped 2026-10-30: D2 on the bench unit no longer goes LOW even shorted to
// GND (only ~20 us spikes reach the ISR), so the coinslot moved to D3. The
// flow sensor is not fitted (dispense is timed), so it takes the dead pin.
#define PIN_COIN_PULSE 3   // INT1. Missed pulse = wrong denomination = money error
#define PIN_FLOW_PULSE 2   // INT0. Missed pulse = under-counted volume

// THIRD INTERRUPT, approved by the team 2026-10-26: the tank ultrasonic we
// have is the PWM variant, and its pulse can be as short as ~0.2 ms near a
// full tank -- shorter than a loop pass, so it cannot be polled. The ISR only
// timestamps the two edges, nothing else. Only used when TANK_SONAR_PWM is 1.
#define PIN_TANK_SONAR_PWM 18   // INT3 (marked TX1 18). Sensor white wire

// Trigger for the PWM ultrasonic, on its yellow (RX) wire. Our unit measures
// only when told to: on the bench it sent ten readings at power-up and then
// went silent. Idle HIGH, pulled LOW for TANK_SONAR_TRIG_LOW_MS every
// TANK_SONAR_TRIG_PERIOD_MS. A free-running unit treats a HIGH line the same
// as an unconnected one, so this is harmless on that variant.
#define PIN_TANK_SONAR_TRIG 19   // marked RX1 19. Sensor yellow wire
#define TANK_SONAR_TRIG_PERIOD_MS 100
// 0 floating, 1 idle-high/low-pulse, 2 idle-low/high-pulse -- see sonar_trigger().
#ifndef TANK_SONAR_TRIG_MODE
#define TANK_SONAR_TRIG_MODE 0
#endif
#define TANK_SONAR_TRIG_LOW_MS 2

// ---------------------------------------------------------------------------
// Pin map -- digital inputs
// ---------------------------------------------------------------------------
//
// All INPUT_PULLUP, wired active-low to ground. This is the safe default: a
// broken wire reads as the inactive state rather than as a phantom coin or a
// phantom bottle.

#define PIN_BOTTLE_PROX     22  // LOW = bottle present
#define PIN_TEMP_ONEWIRE    23  // DS18B20 waterproof probe, 4.7k pull-up to 5V
// D24/D25 were the cold tank mid/high floats; the A02YYUW on Serial3 replaced
// both. Left unallocated.
//
// Capacitive non-contact sensors on the outside wall of the two supply gallons,
// at the reserve line -- see GALLON_SENSOR_WET_LEVEL for the polarity and why
// it changes the pinMode. EXCEPTION to the active-low rule above.
#define PIN_GALLON_A        26
#define PIN_GALLON_B        43
#define PIN_HOPPER_P1_COUNT 27  // Falling edge, polled. Confirms a coin left
#define PIN_HOPPER_P5_COUNT 28  // Falling edge, polled. Confirms a coin left
#define PIN_CHAMBER_FULL    29  // IR break-beam at fill line. LOW = beam broken = full

// 1 = coin box IR fitted on pin 29; locks with COIN STORAGE FULL when the beam
//     is blocked.
// 0 = not fitted. The machine never locks with COIN STORAGE FULL and pin 29 is
//     not read, so a missing sensor cannot float into a false lockout. What it
//     gives up: an overfull box backs coins up the chute and jams it, so empty
//     it between test sessions.
//
// Currently 0: the team decided not to fit it (2026-10-25) -- the box is large
// and this is a thesis prototype. The firmware cannot tell whether the sensor
// is there, so this must match the hardware.
#ifndef COIN_BOX_SENSOR_FITTED
#define COIN_BOX_SENSOR_FITTED 0
#endif
#define PIN_CONFIRM_BTN     40  // LOW = pressed. Advances ACCEPTING -> SELECTING
// 1: the cabinet has the push button on pin 40 (team, 2026-10-30). Set 0 for
// a unit with nothing wired there: a floating pin 40 reads as presses.
#define CONFIRM_BTN_FITTED  1
// IR at the top of each change hopper. LOW = full. A full hopper's coins are
// routed to the coin box instead -- see coin_level.h and coin_route.h.
#define PIN_HOPPER_P1_FULL  41
#define PIN_HOPPER_P5_FULL  42

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
// Swapped 2026-10-29 to match the bench wiring: relay IN1 (pin 31) drives the
// gallon-to-tank pump, IN2 (pin 32) the tank-to-bottle pump.
#define PIN_VALVE         32  // Dispense pump relay, active LOW
#define PIN_PUMP          31  // Fill pump relay, active LOW
#define PIN_HOPPER_P1_RUN 33  // Relay, active LOW
// The hopper relays are a 2-channel module: IN1 = P1 (pin 33), IN2 = P5 (pin
// 34). On 2026-10-29 the P5 relay did not switch because IN2 was not wired to
// pin 34 yet; once it was, it worked unchanged.
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

// Two MG996R gate servos in series on ONE chute under the coinslot:
//
//   GATE 1 (D9)    first. CLOSED deflects the coin into the P5 hopper tunnel.
//   GATE 2 (D10)   second. CLOSED deflects the coin into the P1 hopper tunnel.
//   Both OPEN      the coin runs straight on to the coin box.
//
//   P5, hopper not full    gate 1 CLOSED
//   P1, hopper not full    gate 1 OPEN,  gate 2 CLOSED
//   P10, P20, unknown, or a P1 / P5 whose hopper IR reads full: both OPEN
//
// REST POSITION IS BOTH OPEN. The coinslot only says what a coin was AFTER it
// has dropped, so a coin that beats the servos goes to the box. That is the
// safe direction: a P10 or P20 can never be caught in a change hopper and
// later paid out as a P1 or P5.
//
// MG996R stall current is ~2.5 A EACH. Feed both from a 5-6 V rail rated for at
// least 5 A, star-grounded with the Mega -- never from the Mega's 5 V pin.
#define PIN_GATE1_SERVO 9    // first gate  -> P5 tunnel
#define PIN_GATE2_SERVO 10   // second gate -> P1 tunnel

// Gate angles in degrees, as set by the fabricator: OPEN 55, CLOSED 0. A 55
// degree move is ~0.15-0.2 s on an MG996R, which is what lets the gate beat
// the coin. A gate a few degrees short of closed lets a coin slip past into the
// wrong place -- recheck these if a servo horn is refitted.
//
// 2026-10-26, on the bench: the team asked for the close to swing the other
// way (left), so CLOSED moved from 0 to 110 -- the same 55 degree swing from
// the same rest position, mirrored.
#define GATE1_ANGLE_OPEN   55
#define GATE1_ANGLE_CLOSED 110
#define GATE2_ANGLE_OPEN   55
#define GATE2_ANGLE_CLOSED 110

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------

// The display is an ESP32 LCD running its own UI, linked by plain UART. Same
// rule as the Nextion it replaced: the Mega never renders, it sends short text
// lines and reads events back. Protocol in hmi.h.
//
// The ESP32 is 3.3 V and NOT 5 V tolerant. Mega TX2 (D16) goes to ESP32 RX
// through a 1k/2k divider; ESP32 TX to Mega RX2 (D17) direct. Common ground.
// Its WiFi and Bluetooth stay off -- both are out of scope (CLAUDE.md).
#define HMI_SERIAL      Serial2  // D16 TX2 / D17 RX2

// Both ends must agree or nothing is understood. 115200 rather than the
// Nextion's 9600: HardwareSerial blocks once its 64-byte TX buffer is full, and
// at 9600 one full buffer is ~67 ms of loop stall -- long enough to drop hopper
// outlet counts. At 115200 it is under 6 ms.
#define HMI_BAUD        115200

// A02YYUW ultrasonic over the cold tank. Sensor TX (white) to Mega RX3 (D15).
// Sensor RX (yellow) left unconnected: that selects the sensor's own filtered
// output, one frame about every 100 ms. Powered from 5 V, so its TX is 5 V TTL.
#define TANK_SONAR_SERIAL Serial3  // D15 RX3 (D14 TX3 unused)
#define TANK_SONAR_BAUD   9600     // fixed by the sensor

// 1 = PWM variant (what we have): white wire on PIN_TANK_SONAR_PWM, distance
//     read from the width of its HIGH pulse.
// 0 = UART variant (A02YYUW): white wire on RX3, D15.
// The two look identical from outside; only the listing or label tells them
// apart. Wrong setting = "sensor LOST" and OUT OF WATER, never a wrong level.
#ifndef TANK_SONAR_PWM
#define TANK_SONAR_PWM 1
#endif

// PWM variant: pulse microseconds per millimetre, x100. 583 is the speed of
// sound there and back (2 / 0.343 mm/us = 5.83 us/mm). CALIBRATE: hold a flat
// board at a measured distance, read pwm_us from `st`, and set this to
// pwm_us * 100 / distance_mm.
#define TANK_SONAR_US_PER_MM_X100 583

// Shortest PWM pulse accepted: the blind zone's worth of echo time. Anything
// shorter is a glitch on the line, not a reading.
#define TANK_SONAR_MIN_PULSE_US \
  ((uint32_t)TANK_SONAR_BLIND_MM * TANK_SONAR_US_PER_MM_X100 / 100)

// Longest accepted: twice the tank depth, so an echo off the floor still
// counts as "seen" (and is then rejected as past EMPTY) while a stuck-high
// line does not.
#define TANK_SONAR_MAX_PULSE_US \
  ((uint32_t)TANK_SONAR_EMPTY_MM * 2 * TANK_SONAR_US_PER_MM_X100 / 100)

// Storm guard. A sensor sends ~10 pulses a second = ~5 edges per 250 ms.
// Above 200 edges in a window the line is noise; the ISR is muted for the
// hold-off so the main loop keeps running. Raising the limit lets noise eat
// the loop; lowering it below a real sensor's rate mutes a good sensor.
#define TANK_SONAR_STORM_WINDOW_MS   250
#define TANK_SONAR_STORM_MAX_EDGES   200
#define TANK_SONAR_STORM_HOLDOFF_MS  1000

#define DEBUG_BAUD      115200

// ---------------------------------------------------------------------------
// Coin path timing
// ---------------------------------------------------------------------------

// One coin through the gates, in two phases. The servos have no position
// feedback, so these timers are all that stands between a coin and a jam.
//
// GATE_ROUTE_MS   gates set for this coin, held while it runs down its tunnel.
//                 Covers servo travel (MG996R at 5 V ~0.2 s per 60 deg, the
//                 move is 55 deg) plus the coin's run. Shorter and the gate
//                 swings back across a coin still in the chute.
// GATE_RETURN_MS  gates travelling back to the rest position (both open)
//                 before the next coin may come.
#define GATE_ROUTE_MS  900
#define GATE_RETURN_MS 400

// Test console "gate <n>": time per gate move. Long enough to watch each move
// complete; the MG996R needs ~0.2 s for the 55 degree swing.
#define GATE_EXERCISE_STEP_MS 600

// Servo signal is cut this long after the last gate move, once the gate has
// certainly arrived (a 55 degree swing is ~0.2 s). Shorter risks cutting a
// move short; the servo then stops wherever it was.
#define GATE_DETACH_MS 800

// Acceptor is inhibited for this long after each accepted coin -- the whole
// gate cycle above.
//
// DO NOT SHORTEN THIS TO MAKE THE MACHINE FEEL FASTER. A coin landing on a
// moving gate jams the chute, and clearing a chute jam is a service call.
#define COIN_LOCKOUT_MS (GATE_ROUTE_MS + GATE_RETURN_MS)

// A pulse train is considered complete when the pulse line has been idle this
// long. The acceptor emits its pulses in a burst; this gap terminates the burst
// so update() knows the count is final.
//
// Too short and a 20-pulse P20 is read as two coins -- a P20 credited as P10 and
// P10, or worse, as two separate coins that each trigger a diverter move.
// Too long and the machine feels sluggish between coins.
// Raised 200 -> 350 ms on 2026-10-29: this Allan coinslot pulses every ~169 ms,
// so 200 ms left only ~30 ms of margin and a P5 was read as a 2- or 3-pulse
// "unknown" coin (credited P1, sent to the coin box) twice in one bench
// session. 350 ms is twice the period and still far below two human inserts.
// 700, was 350: with the resistor gone from the coin line the Allan's pulse
// spacing measured 326 ms (169 ms before), so 350 split a P5 into "unknown"
// plus single P1s (bench 2026-10-30). Keep it about twice the spacing; much
// longer only delays the credit.
#define COIN_PULSE_GAP_MS 700

// Accepted width of one coinslot pulse (LOW time). The Allan coinslot sends
// pulses of tens of milliseconds; motor and relay spikes are microseconds,
// and before this filter they were being counted as P1 coins (2026-10-29: P12
// of credit appeared with no coin inserted). 5 ms is well under any real
// pulse and far above a spike. If real coins stop registering, read
// "coin_width_us" from the test console's "st" and lower the minimum.
//
// 1 ms, was 5 ms (bench 2026-10-30, "[pulse]" log): in the cabinet each real
// pulse arrives as 2-3 LOW fragments of 1-3 ms within ~25 ms, ~130 ms apart.
// Merged (below) a pulse spans 5-25 ms, but a lone fragment must still count.
// The noise spikes this filter exists for are tens of microseconds.
#define COIN_PULSE_MIN_US 1000UL
#define COIN_PULSE_MAX_US 200000UL
// A HIGH shorter than this inside a pulse is chatter, not the end of it (see
// coin_isr()). 30 ms covers the fragment spread seen on the bench and stays
// well under the ~100 ms HIGH between real pulses; much higher and two pulses
// merge into one (a P5 reads as less).
#define COIN_PULSE_MERGE_US 30000UL

// ADAPTIVE GAP. 200 ms is the safe figure before anything is known about the
// coinslot, but it is pure dead time before the gates can move -- and the coin
// is already falling. Once a multi-pulse train has been seen, its average
// pulse period is learned and the gap becomes COIN_PULSE_GAP_FACTOR x that
// period, never below COIN_PULSE_GAP_MIN_MS and never above COIN_PULSE_GAP_MS.
// The coinslot's pulses are timer-generated and regular, so 3x the period is
// well clear of any gap inside a train. Lower the factor and a jittery train
// can split into two coins (a P20 read as P10 + P10); raise it and the gates
// move later.
#define COIN_PULSE_GAP_FACTOR 3
// 50 let a noise burst teach a tiny period (bench 2026-10-30, a resistor on
// the coin line): the gap fell below the Allan's ~169 ms pulse spacing, every
// P5 resolved as a P1 on its first pulse, and the diverter lockout drained the
// other four. Must stay above the slowest real pulse spacing.
#define COIN_PULSE_GAP_MIN_MS 300

// A gate aimed for an identified coin that is then NOT routed (it was
// discarded by a state change) goes back to rest after this, so the next coin
// cannot fall into a hopper tunnel it was never meant for.
#define COIN_AIM_TIMEOUT_MS 500

// Sanity ceiling on a pulse train. A P20 at COIN_PULSES_P20 is the longest
// legitimate train, so clearly more than that in one burst is noise on the line
// -- motor noise from the pump or a hopper coupling into D2 -- or a stuck
// acceptor output. Discard the train rather than crediting a denomination that
// does not exist. Margin above P20 so one noise spike on a real P20 is not
// thrown away (it resolves as unknown and is credited the minimum instead).
#define COIN_PULSE_MAX (COIN_PULSES_P20 + 4)

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

// 1 = the hoppers pay one coin per start and stop by themselves (what ours do,
// bench 2026-10-29), so the run line is pulsed once per coin.
// 0 = the hopper runs while the line is held and pays continuously.
#define HOPPER_PULSE_MODE 1
// One pulse = one coin. ON must be long enough for a full cycle of the hopper
// (it made one turn in under 2 s on the bench); OFF long enough for it to see
// the line drop and re-arm. Too short ON skips coins; too short OFF and the
// hopper may not take the next start.
// Per hopper: on the bench the P1 hopper paid 3 coins for 3 pulses at
// 1200/400, while the P5 hopper paid only 2 for 5 -- it cycles slower, so it
// gets a longer pulse and a longer rest.
// P1 re-measured 2026-10-29: at 3000/3000, 10 relay pulses gave 5 turns and
// 5 coins -- the P1 hopper turns once per TWO pulses. So it is driven with
// two pulses per coin (team's call). The P5 hopper turns on every pulse.
#define HOPPER_PULSE_ON_MS     3000   // P1
#define HOPPER_PULSE_OFF_MS    3000   // P1
#define HOPPER_P1_PULSES_PER_COIN 2
#define HOPPER_P5_PULSES_PER_COIN 1
// 2000/800 paid 6 coins for 10 pulses; lengthened again.
#define HOPPER_P5_PULSE_ON_MS  3000
// 3000/1500 paid 8-9 of 10: the hopper misses a pulse when the rest is short.
// Same rest as P1, which pays 10 of 10.
#define HOPPER_P5_PULSE_OFF_MS 3000

// 1 = no working outlet sensor: each completed pulse counts as one coin paid
//     (bench 2026-10-29 -- three pulses gave exactly three P1, while the
//     sensor on pin 27 counted none). What it gives up: a hopper that is
//     empty or jammed still "pays", so the machine believes change went out
//     that did not. CLAUDE.md requires the outlet count; set this back to 0
//     once CT1 / CT2 actually see the coins.
// 0 = the outlet sensors on pins 27 / 28 count the coins (the design).
#define HOPPER_COUNT_BY_PULSE 1

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

// The coin box is NOT a hopper and holds far more than one. Clamping it at
// HOPPER_CAPACITY silently stopped a counter rising past 500, which would
// understate a full day's take. Separate ceiling, generous, PER COUNTER, because
// its only job is to stop a corrupt value being believed. The physical "full"
// signal is the IR beam, not this number.
#define COIN_BOX_CAPACITY 2000

// ---------------------------------------------------------------------------
// Coin routing mode
// ---------------------------------------------------------------------------
//
// The cabinet is fabricated to drawing Rev B with both gate housings, because
// the diverter cannot be retrofitted. It can then run in either of two ways:
//
//   RECIRCULATE  Both gate servos fitted. P1 and P5 go back into the hoppers
//                and are given out again as change; P10, P20 and unidentified
//                coins go to the locked coin box. THE DELIVERED CONFIGURATION:
//                the client's stated novelty is coin recirculation.
//
//   COLLECT_ALL  No servos fitted. Gate A is mechanically PINNED TOWARD THE
//                COIN BOX, so every accepted coin goes there. The hoppers are
//                operator-loaded reserves that only dispense. An interim build:
//                it lets the whole machine be bench-tested before the servos
//                arrive, and gives the no-recirculation baseline that
//                recirculation is measured against.
//
// COMPILE-TIME, DELIBERATELY. A mode switch on the Admin screen would let an
// operator put the firmware into a state the hardware does not match, and the
// firmware cannot detect a servo: one that is not fitted and one that has
// failed look identical. So the mode is fixed per build, shown in the boot
// trace and on Admin, and stored with the inventory so that a reflash into the
// other mode is detected rather than silently reinterpreting the saved counts.
//
// THE FAIL-SAFE REASONING INVERTS BETWEEN THE MODES. Drawing sheet 2 has Gate A
// rest on the HOPPER path with power off. That is the RECIRCULATE fail-safe: a
// dead servo then sends every coin to the hoppers, which shows up as an
// inventory mismatch an operator can see, where the reverse would silently feed
// the box with coins the machine believes are change. In COLLECT_ALL there is
// no servo to fail; the flap is pinned on purpose and the box is the intended
// destination. Sheet 2 and this file are both right. They describe different
// builds.
//
// Select with a build flag, e.g. -DCOIN_ROUTING_MODE=COIN_ROUTING_COLLECT_ALL
// (see the *_collect environments in platformio.ini). The values are stored in
// EEPROM and must never be renumbered.
#define COIN_ROUTING_RECIRCULATE 1
#define COIN_ROUTING_COLLECT_ALL 2

#ifndef COIN_ROUTING_MODE
#define COIN_ROUTING_MODE COIN_ROUTING_RECIRCULATE
#endif

#if COIN_ROUTING_MODE != COIN_ROUTING_RECIRCULATE && \
    COIN_ROUTING_MODE != COIN_ROUTING_COLLECT_ALL
#error "COIN_ROUTING_MODE must be COIN_ROUTING_RECIRCULATE or COIN_ROUTING_COLLECT_ALL"
#endif

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
//
// THE RESERVE IS A RECIRCULATION CONCEPT, AND IT IS ZERO IN COLLECT_ALL ON
// PURPOSE. Its whole justification is refill: P5 is "scarce" only because it
// comes back more slowly than P1 does. In COLLECT_ALL nothing comes back --
// both hoppers are operator-loaded and only ever dispense -- so P5 is no
// scarcer than P1, and ten coins held back are simply P50 of float the machine
// owns and never spends. On the P285 mockup float that is 36 sales before
// lockout against 45 (docs/change-economics.md). The zero is not a mistake.
//
// 2026-10-27, bench: set to 0 in both modes at the team's request. The test
// hopper holds only a few P5, and with a reserve of 10 it never paid a P5 at
// all -- P18 change came out as P1 coins. Revisit (10 was the design value)
// once the hopper is loaded to a working float.
#define HOPPER_RESERVE_P5 0

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

// COMPLETE holds this long for the user to choose more-or-finish, then finishes
// by itself and pays the change out (decisions.md R-9).
//
// Without a timeout a balance sits on screen until somebody presses FINISH, and
// that somebody may not be the person who paid. Sixty seconds rather than the
// twenty of the bottle wait because here the user is handling a full bottle and
// a cap.
//
// SHORTEN THIS and a user who paid loses their change to the next person in
// line. Lengthen it and an abandoned machine sits dead, earning nothing.
#define COMPLETE_TIMEOUT_MS 60000

// The last part of that wait is shown as a countdown, so a user who is simply
// slow is not surprised by the payout.
#define COMPLETE_COUNTDOWN_MS 15000

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
// 150, was 50: the button's long run past the relays picked up noise that
// held for 50 ms and sent the machine to the picker mid-insert (bench
// 2026-10-30). A real press is held far longer than 150 ms.
#define CONFIRM_DEBOUNCE_MS 150

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

// ---------------------------------------------------------------------------
// TIMED DISPENSING -- for a unit with NO flow sensor fitted.
//
// 1 = volume is the time the dispense pump has run, at a fixed calibrated rate.
//     The clock runs ONLY while the pump is on: lifting the bottle stops pump
//     and clock together, and a replaced bottle resumes both, so a pour split
//     across two containers still totals the paid volume.
// 0 = volume is counted by the flow sensor on D3 (preferred when fitted).
//
// What timed mode gives up, and why the flow sensor is preferred:
//   - Accuracy follows the pump. The rate drops as the supply voltage sags and
//     as the cold tank level falls (less head on a submersible pump), so the
//     same seconds pour less. Recalibrate if the pump, tubing or supply change.
//   - NO STALL DETECTION. Nothing can tell that water stopped coming -- an
//     empty tank or a dead pump still "pours" on the clock, and the user is
//     charged for water they did not get. TANK_SONAR_MIN_SERVE_MM is what
//     stands between that and the user: keep the tank sensor working.
#ifndef DISPENSE_TIMED
#define DISPENSE_TIMED 1
#endif

// Timed mode only: milliseconds the dispense pump takes to deliver 100 mL.
// MEASURE IT: run the pump into a measuring cup for a known time (say 30 s),
// then DISPENSE_MS_PER_100ML = 30000 * 100 / mL collected. Measure with the
// cold tank near full, the level it runs at most of the time.
// 6000 (1 L/min) is a PLACEHOLDER.
// 2026-10-26: set to 7000 by the team on the bench -- P1 (100 mL) pours for
// 7 seconds. Still to be checked against a measuring cup.
//
// TWO-POINT CALIBRATION (do this, a single number is only right at one size):
//   1. Tank full. "pour 5" into the measuring cup, read mL = V1 (t1 = 5000).
//   2. "pour 20" into the cup, read mL = V2 (t2 = 20000).
//   3. Repeat each three times and average.
//   4. DISPENSE_MS_PER_100ML = 100 * (t2 - t1) / (V2 - V1)
//      DISPENSE_DEAD_MS      = t1 - V1 * (t2 - t1) / (V2 - V1)
// Example: 5 s -> 60 mL, 20 s -> 300 mL gives 6250 ms per 100 mL and a dead
// time of 1250 ms.
// Bench 2026-10-29: "pour 15" gave 445 mL -> 15000 * 100 / 445 = 3371.
// One-point only; do the two-point run above to remove the dead time.
// Bench 2026-10-30, new smaller hose + solenoid valve: a P5 (500 mL) sale
// poured 400 mL -> 3371 * 500 / 400 = 4214.
// 2026-10-31: a two-point fit (5057 / dead -4214) started every pour at ~12%
// and then poured 730 mL for 800. The three runs disagree because the rate
// falls as the cold tank drains (gravity head into the pump): 500 mL in
// 21070 ms, 750 in 33712 ms, 730 in 36242 ms. Single rate from the mean of
// all three, no dead time. Calibrate with the tank kept full.
// Then 800 mL sale poured 820 mL -> 4600 * 800 / 820 = 4490.
// Two-point at 4490: 300 mL sale poured 315 (13470 ms), 800 poured 800
// (35920 ms) -> 100 * 22450 / 485 = 4629, dead = 13470 - 315 * 22450 / 485
// = -1111 (~24 mL beyond the steady rate per pour).
#define DISPENSE_MS_PER_100ML 4629

// Run time that delivers nothing at the cup: hose fill at the start, less the
// dribble after the pump stops. 0 until the two-point calibration is done.
// Signed -- negative when the dribble outweighs the hose fill.
#define DISPENSE_DEAD_MS (-1111)

// 1 = no amount picker. CONFIRM (push button, or Continue on the screen) pours
// the whole credit, P1 = 100 mL, once the bottle IR sees a bottle.
// 0 = the screen's amount picker chooses how much to pour; what is left is
//     poured again or returned as change.
// Team decision 2026-10-27: the customer chooses the amount, so 0. (Was 1
// from 2026-10-26.)
#ifndef AUTO_DISPENSE_ALL_CREDIT
#define AUTO_DISPENSE_ALL_CREDIT 0
#endif

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

// --- Cold tank: A02YYUW ultrasonic ----------------------------------------
//
// All distances are mm from the sensor face DOWN to the water. Larger = less
// water. EVERY NUMBER BELOW IS A PLACEHOLDER until measured on the real tank:
// fill it with a measuring jug and read the distances off the `st` status
// line of the test console.
//
// Order must hold: BLIND < FULL < REFILL < MIN_SERVE < EMPTY.

// The A02YYUW cannot see closer than 30 mm. Mount it so that FULL is at least
// 20 mm below that, or the top of every fill reads as garbage.
#define TANK_SONAR_BLIND_MM 30

// Pump stops here. The tank is kept as full as it can be, so the dispense side
// always has head and the timed-dispense rate stays where it was calibrated.
#define TANK_SONAR_FULL_MM 60

// Pump starts here. A 20 mm band is the "keep it topped up" setting: the tank
// never sits more than a cupful down. Narrower and every pour short-cycles the
// pump, which shortens its life; PUMP_MIN_OFF_MS backs this up regardless.
#define TANK_SONAR_REFILL_MM 80

// OUT OF WATER lockout. Set it so the water ABOVE this line is a full P20 pour
// (2000 mL) plus a 20% margin, all above the dispense pump intake -- measured,
// not computed from a drawing. Lower than that and a P20 customer can be sold
// air on a timed-dispense unit, which cannot detect it. Checked only in
// STANDBY, so a pour already paid for always finishes.
#define TANK_SONAR_MIN_SERVE_MM 220

// The tank floor. A reading past it is an echo off something else and is
// dropped. Also the 0% point on the display.
// 2026-10-26: raised from 300 on the bench -- the empty tank reads deeper than
// 300 mm, so every echo off its floor was thrown away and the sensor looked
// dead. Set to the measured sensor-to-floor distance once known.
#define TANK_SONAR_EMPTY_MM 1000

// Readings in the median. Five frames is ~0.5 s: enough to reject a wave or an
// echo off the inlet stream, short enough that the pump still stops at FULL
// rather than a centimetre past it.
#define TANK_FILTER_SAMPLES 5

// No plausible frame for this long = sensor lost (unplugged, condensation on
// the face, wrong sensor variant). The pump stops at once and the machine
// locks at the next STANDBY. Ten missed frames: a lone dropout never trips it.
#define TANK_SONAR_TIMEOUT_MS 1000

// --- Supply gallons: capacitive sensors -----------------------------------
//
// One XKC-Y25-class sensor per gallon, strapped flat to the outside wall at the
// RESERVE line: about 50 mm above the floor of a round 5-gallon jug, which is
// still ~2.5-3 L of water. The pump pickup tube must reach BELOW the sensor.
//
//   one gallon low   -> small warning on the display, nothing else
//   both gallons low -> warning, and the fill pump stops so it never runs dry.
//                       The cold tank keeps selling until MIN_SERVE locks it.
//
// Polarity: the pin level that means "water at the sensor".
//   HIGH (default) -- XKC-Y25-V / PNP output. Fit a 10k pull-DOWN on the
//                     signal; the pin is set INPUT. A broken wire then reads
//                     LOW = dry, which fails safe (warning shown, pump stopped).
//   LOW            -- NPN output. Pin is INPUT_PULLUP; a broken wire reads
//                     HIGH = dry, also safe.
// CHECK ON THE BENCH: hold the sensor against a full bottle and an empty one
// and measure the signal wire. If it is the other way round, flip this.
// Bench 2026-10-25: our sensors read LOW against a full gallon -- NPN output.
// The 10k pull-downs were removed; the pin runs INPUT_PULLUP.
#define GALLON_SENSOR_WET_LEVEL LOW

// 1 = EITHER gallon at its reserve line stops the fill pump. Right for our
//     machine: both jugs feed one Y fitting, and one jug sucking air starves
//     the pump. 0 = only both low stops it (separate suction lines).
#define GALLON_STOP_ON_EITHER 1

// "Low" acts at once -- it can stop the pump, and a sloshing jug near the line
// may never hold low for a full window. Only the return to "water present" is
// debounced, so a jug being swapped (which sloshes hard) does not flicker the
// warning off and on.
#define GALLON_SENSOR_DEBOUNCE_MS 2000

// Coin level IR debounce -- SPEC 1.2. Applies to the coin box beam and the two
// hopper-full beams. Coins tumbling past a beam break it for a few ms; only a
// stack that sits at the beam is "full". Much shorter and a falling coin reads
// as full and sends the next P1/P5 to the box for no reason (harmless, but it
// starves the change float). Much longer and a coin can still be sent to a
// hopper that has just filled.
#define COIN_LEVEL_DEBOUNCE_MS 500

// Going FULL needs the beam blocked this long. Bench 2026-10-30: the P5 beam
// read full for under a second just as a P5 dropped, so the coin went to the
// box and the refund came out in P1s. A real full hopper stays blocked; a
// passing hand or a coin settling does not. Much shorter and that comes back;
// longer only delays the switch to the box once a hopper truly fills.
#define COIN_LEVEL_FULL_MS 5000

// Minimum time the pump stays off after stopping, before it may start again.
// Back-to-back starts on a diaphragm pump shorten its life; this enforces a
// rest even if the floats say otherwise.
#define PUMP_MIN_OFF_MS 3000

// Absolute ceiling on a single pump run. A pump that has run this long without
// bringing the tank to TANK_SONAR_FULL_MM is pumping air, pumping against a
// blockage, or looking at a sensor that is lying. Any of those destroys it.
// Stop and raise a fault.
#define PUMP_MAX_RUN_MS 300000  // 5 minutes

// Longest single manual fill from the test console. Two minutes is long enough
// to see water arrive and short enough that a forgotten command cannot run a
// gallon dry for long. PUMP_MAX_RUN_MS still applies on top.
#define MANUAL_FILL_MAX_S 120

// Volume-matched refill: fill-pump milliseconds per 100 mL, gallons -> tank
// (a 1 m lift, so slower than the dispense pump). 7000 is a PLACEHOLDER --
// measure it: "fill 10" into a measuring jug at the tank inlet, then
// FILL_MS_PER_100ML = 10000 * 100 / mL collected. Too high overfills the tank,
// too low lets it drain over the day.
#define FILL_MS_PER_100ML 7000

// Owed refill never banks more than this, so a miscount cannot run the pump
// for ages. 120 s is more than one 1.5 L sale needs at the placeholder rate.
#define REFILL_OWED_MAX_MS 120000UL

// 1 = the refill runs even when the tank sensor is lost or a gallon reads low.
// Bench setting (2026-10-28): neither sensor works yet, and with them in the
// loop the refill would never run. It RISKS RUNNING THE PUMP DRY on an empty
// gallon -- set to 0 once the gallon sensors read correctly.
#define REFILL_IGNORES_SENSORS 1

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

// Fixed line buffers. No String, no dynamic allocation -- CLAUDE.md. The
// longest line this firmware sends is a TX with a formatted amount, comfortably
// inside TX. RX only ever carries "EV <n> <payload>".
#define HMI_TX_BUFFER 64
#define HMI_RX_BUFFER 24

// The LV status line (tank and gallons) is re-sent this often even when nothing
// changed, so an ESP32 that rebooted on its own shows the right warning within
// two seconds instead of waiting for the next level change.
#define HMI_STATUS_RESEND_MS 2000

// Sales screen: how many of the most recent purchases it lists. The EEPROM
// ring holds HISTORY_ENTRIES events in all, purchases and others mixed.
#define HMI_STATS_HISTORY 10

// Admin PIN, checked on the Mega (hmi.cpp) -- the screen only forwards what was
// typed. 1234 is a PLACEHOLDER until the team picks one; change it before the
// machine is shown to anyone, and keep the real value out of public copies.
#define ADMIN_PIN 1234
// How long a correct PIN keeps admin open; every admin action restarts it.
// Long enough to load coins and run a test; short enough that a screen left
// in admin closes itself.
#define ADMIN_UNLOCK_MS 120000UL

// Screen refresh during a pour. Fast enough that the progress bar looks live,
// slow enough that the serial line is not saturated while flow pulses are
// arriving on an interrupt.
#define HMI_DISPENSE_REFRESH_MS 250

// How long the Thank You screen holds before returning to Standby, if the user
// does not dismiss it. Long enough to read the change due and collect it.
#define HMI_THANKYOU_HOLD_MS 8000

// Bench bypass only: how long "change could not be given" stays on screen
// before the machine goes back to taking coins. Long enough to read the
// amount owed and photograph it; the amount is in the EEPROM history anyway.
#define CHANGE_UNPAID_HOLD_MS 20000

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

// Layout version 5: the inventory record became a wear-levelled ring at
// EEPROM_ADDR_INVENTORY_RING (at one address it was rewritten on every coin in
// and every payout leg -- ~100-200 days of service), and the daily record
// gained the day number it belongs to, so a machine switched off over midnight
// still closes the previous day on its next boot.
//
// Layout version 4: the coin-box counters were renamed from profit_* to box_*
// and extended to all five (P1, P5, P10, P20, unknown), and the routing mode is
// stored with the inventory. The inventory record grew to 16 bytes, which moved
// the fault flags from 32 to 40.
//
// Layout version 3: the open-transaction record was redesigned (resume phase,
// pour checkpoints, payout leg) and its ring doubled to 64 slots, which moved
// the coin-in-flight ring. Version 2 made both regions rings.
//
// NO MIGRATION, by ruling: there are no units in the field. A record from any
// other version is rejected by record_unpack(), every region initialises
// fresh, the inventory reads zero and the machine locks on LOW CHANGE until an
// operator loads the float -- SPEC 7.3. Misreading an old layout would report
// inventory that never existed.
#define EEPROM_LAYOUT_VERSION 5

#define EEPROM_ADDR_HEADER     0    // magic, version, checksum
// 16..39 held the single inventory record up to layout 4. Unused since layout
// 5 moved inventory to EEPROM_ADDR_INVENTORY_RING; left empty rather than
// reclaimed so the regions after it do not move.
#define EEPROM_ADDR_INVENTORY  16

// Persistent fault flags -- SPEC 6.1 and 7.1.
//
// A persistent fault that clears on power cycle is worse than not claiming
// persistence at all: the operator learns that the fix is a reboot, the coins
// stay jammed, and the machine returns to accepting money it cannot pay out.
//
// Sits in the gap between the inventory record and the open transaction. The
// static_asserts in persist.cpp enforce that it fits.
#define EEPROM_ADDR_FAULTS     40

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

#define EEPROM_ADDR_OPEN_TXN_RING 1024   // 64 slots x 28 B -> 1024..2816
#define EEPROM_ADDR_INFLIGHT_RING 2816   // 64 slots x 12 B -> 2816..3584
#define EEPROM_ADDR_INVENTORY_RING 3584  // 20 slots x 24 B -> 3584..4064
// 4064..4096 is FREE.

// Open transaction. Written on open, on EVERY COIN, on selection, at every
// 100 mL of a pour, at each bottle pause, on settle, at the start and end of
// each payout leg, and on close.
//
// Worst case ~50 writes/transaction: 20 coins (P20 in P1s) + 20 checkpoints
// (one 2000 mL pour) + open, select, pauses, settle, four leg writes, close.
//
//   64 x 100,000 = 6,400,000 writes
//   50 w/txn x 100 txn/day = 5,000 writes/day
//   -> 1,280 days = 3.5 years at ABSOLUTE worst case
//
// The 100 mL checkpoint is what doubled the write count and therefore the
// ring: it is the only granularity at which a power cut mid-pour costs the
// user nothing beyond normal rounding. At 32 slots this would be 1.75 years.
//
// Sized against the worst row, not the typical one. An earlier draft sized 8
// slots against the typical row and reported it as the worst case, which would
// have shipped a ring good for 333 days.
#define TXN_RING_SLOTS 64

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

// Coin inventory. Written on every accepted coin (diverter commit) and at the
// end of every payout leg -- roughly 5-10 writes per transaction. At a single
// address that was the shortest-lived record left in the machine:
//
//   100,000 / (10 w/txn x 100 txn/day) = 100 days at worst case
//
// With 20 slots: 2,000 days = 5.5 years worst case. Each slot is 24 B, so 20
// is what fits in the space left above the in-flight ring.
#define INVENTORY_RING_SLOTS 20

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
