# Wiring and pin map

Project EMX-2026-WATERVENDO-01, eMathrix Technologies.
Controller: Arduino Mega 2560, 5 V logic.

This document is the source of truth for the physical build. The values here are
mirrored in `include/config.h` and the two must never disagree. If a pin moves, it
moves in both places in the same commit.

## Why the Mega, and why these pins

The Mega is load-bearing for two reasons stated in `CLAUDE.md`: the pin count and the
5 V logic. Most of the peripherals on this machine are 5 V industrial parts — the coin
acceptor pulse output, the float switches, the hopper outlet sensors and the relay
board all expect 5 V. A 3.3 V controller would need level shifting on nearly every
line. Do not port this to an ESP32 or a Pi.

## Interrupt allocation

Only two pins use hardware interrupts. This is a hard constraint from `CLAUDE.md`, not
a preference. Both ISRs set a `volatile` counter and return; all interpretation happens
in the owning module's `update()`.

| Signal | Pin | Vector | Why it must be an interrupt |
|---|---|---|---|
| Coin acceptor pulse | D2 | INT0 | Pulses are short and a missed one mis-identifies the denomination, which is a money error |
| Flow sensor pulse | D3 | INT1 | Pulse rate is high during a pour and a missed pulse under-counts volume |

Everything else is polled in `update()`. The hopper outlet sensors are polled
deliberately — coins leave a hopper at roughly 5–10 per second, which is slow enough
for a non-blocking poll and does not justify spending an interrupt. This depends on the
main loop staying fast, which is why `delay()` outside `setup()` is banned.

### Both pulse lines are shielded and filtered — this is not optional

D2 and D3 are the only two signals on this machine where a single corrupted edge is a
money error. A phantom edge on D2 credits a coin nobody inserted, which is free water
for the user and inventory drift for the operator. A lost edge on D3 under-counts a
pour. Both lines run the length of a cabinet containing a compressor, a pump and two
hopper motors.

**Shielding.** Run each pulse line in shielded cable — a single-core screened lead, or
one pair of a foil-screened multicore. Ground the shield **at the controller end only.**

Grounding a shield at both ends turns it into a ground loop: chassis current flows
along the screen and injects exactly the interference the screen was installed to
exclude. One end only, every time. Leave the far end cut back and insulated so it
cannot touch the sensor body.

**RC filter at the controller end**, on both inputs:

| | Series R | Shunt C to GND | External pull-up to 5 V | Corner | Rise τ |
|---|---|---|---|---|---|
| Coin pulse D2 | 1 kΩ | 100 nF | 4.7 kΩ | ≈1.6 kHz | ≈0.47 ms |
| Flow pulse D3 | 1 kΩ | 10 nF | 4.7 kΩ | ≈16 kHz | ≈47 µs |

Series resistor from the sensor to the pin, capacitor from the pin to ground, pull-up
from the pin to 5 V.

The corner frequencies are chosen to clear the slowest legitimate signal by a wide
margin. Coin acceptor pulses are tens of milliseconds wide, so 1.6 kHz passes them
untouched while shorting the microsecond-scale spikes that switching produces. Flow
pulses run to a few hundred hertz at the dispensing rate, so 16 kHz is likewise far
above anything real. **Do not raise either capacitor to "filter harder."** A 1 µF on D2
gives a 4.7 ms rise time and starts eating the edges of a real pulse train, which
mis-identifies denominations — the exact failure being prevented.

The external 4.7 kΩ pull-up matters as much as the capacitor. The Mega's internal
pull-up is 20–50 kΩ, and 30 kΩ against 100 nF is a 3 ms rise — slow enough to smear a
coin pulse. The external resistor dominates it and brings the edge back.

`INPUT_PULLUP` stays enabled in firmware on both pins anyway, so a disconnected or cut
line still reads inactive rather than floating. The two pull-ups in parallel are
harmless.

With a 1 kΩ series resistor and an open-collector sensor pulling low, the pin sits at
roughly 5 V × 1 kΩ / (4.7 kΩ + 1 kΩ) ≈ 0.9 V, comfortably under the 1.5 V `V_IL` of a
5 V AVR input. Do not increase the series resistor past about 1.5 kΩ without redoing
that arithmetic.

## Serial

| Port | Pins | Use |
|---|---|---|
| Serial | D0 / D1 (USB) | Debug output, `DEBUG` builds only. Never enabled in a release build. |
| Serial2 | D16 TX2 / D17 RX2 | Nextion HMI, **9600 baud** |

The Nextion panel and the firmware must agree on baud or the display does not respond
at all. 9600 is the specified value (`SPECIFICATION.md` §1.4); if a panel is found
already flashed at another rate, change the panel rather than the firmware, or change
both documents together.

Nextion TX to Mega RX2 (D17), Nextion RX to Mega TX2 (D16), common ground. The Nextion
runs on its own processor and draws every screen itself — the Mega only sends short
commands terminated with three `0xFF` bytes.

## Digital inputs

All switch and sensor inputs use `INPUT_PULLUP` and are wired active-low to ground
unless noted. This is the safer default: a broken wire reads as the inactive state
rather than as a phantom coin or a phantom bottle.

| Signal | Pin | Mode | Active | Notes |
|---|---|---|---|---|
| Coin acceptor pulse | D2 | INPUT_PULLUP | Falling edge | Interrupt INT0 |
| Flow sensor pulse | D3 | INPUT_PULLUP | Falling edge | Interrupt INT1 |
| Bottle proximity | D22 | INPUT_PULLUP | LOW = bottle present | Debounced in software; a flicker must not trigger the removed-bottle pause |
| Cold tank float, mid | D24 | INPUT_PULLUP | LOW = below mid | Pump on |
| Cold tank float, high | D25 | INPUT_PULLUP | LOW = at high | Pump off |
| Gallon bay float | D26 | INPUT_PULLUP | LOW = empty | **Safety lockout.** Inhibits the pump unconditionally |
| ₱1 hopper outlet count | D27 | INPUT_PULLUP | Falling edge | Polled. Confirms coins actually left |
| ₱5 hopper outlet count | D28 | INPUT_PULLUP | Falling edge | Polled. Confirms coins actually left |
| Profit chamber full | D29 | INPUT_PULLUP | LOW = beam broken | IR break-beam at the fill line. Locks the machine |

### Profit chamber sensor — IR break-beam, not a microswitch

The beam crosses the chamber at the fill height and only breaks when the coin stack
physically reaches it. A lever microswitch was rejected: coins stack unevenly and one
leaning coin pushes the arm while the chamber is still half empty, locking the machine
early and sending a technician out for nothing.

It is also the same sensor family and the same polled-input code path as the hopper
outlet counters, so it needs no new driver.

### Temperature sensor — DS18B20, waterproof probe

D23, one-wire, with a 4.7 k pull-up to 5 V on the data line. Use the **stainless
waterproof probe** version, not the bare TO-92 part. The probe sits in or against
chilled water with condensation on it continuously, which is a corrosion and drift
problem for a thermistor. The DS18B20 is factory-calibrated and reads Celsius directly,
so it adds no per-unit calibration step to a build that already has flow calibration.

| Signal | Pin | Notes |
|---|---|---|
| Cold tank temperature | D23 | DS18B20 one-wire, 4.7 k pull-up to 5 V. Status screen only |

### Real-time clock — DS3231 on I²C

| Signal | Pin | Notes |
|---|---|---|
| RTC SDA | D20 | Hardware I²C. Fixed by the chip, not chosen |
| RTC SCL | D21 | |

Most DS3231 breakout modules carry their own 4.7 kΩ pull-ups on SDA and SCL. **Check
before adding more** — several modules in parallel on one bus stack their pull-ups and
drag the bus resistance down until the edges stop meeting spec. With one device on a
short run, the module's own resistors are correct and nothing else is needed.

Three things to get right at purchase and at install:

**DS3231, not DS1307.** The DS3231 is temperature-compensated and holds a couple of
minutes a year; a DS1307 drifts that much in a month, and this one sits in a cabinet
with a compressor cycling beside it.

**Not a DS3231M.** The M part is a lower-grade MEMS oscillator sold in the same
footprint at a similar price and is far worse. Read the chip marking, not the listing.

**Buy it with the CR2032 fitted**, and confirm the cell is not already flat — a lot of
cheap modules ship with a dead one, which produces exactly the symptom the firmware
treats as a failed clock. The battery is what makes the daily total mean "today"
rather than "since the last power cut".

Some modules include a charging circuit for a rechargeable LIR2032 and will slowly
cook a non-rechargeable CR2032 fitted in its place. If the module has a diode and
resistor between VCC and the cell, either fit the LIR2032 it expects or remove that
resistor. This is worth five minutes at build time and is a warranty call later.

## Digital outputs

The relay board is opto-isolated and active-low. Drive the pump, valve and hopper
motors through it — none of these loads may be driven from a Mega pin directly.

| Signal | Pin | Active | Load | Notes |
|---|---|---|---|---|
| Coin acceptor inhibit | D30 | HIGH = inhibited | Acceptor inhibit line | Asserted first in every lockout, and during `COIN_LOCKOUT_MS` |
| Solenoid valve | D31 | LOW (relay) | 12 V solenoid | Flyback diode required at the valve |
| Pump | D32 | **LOW (DC SSR)** | 12 V diaphragm pump | Gated by the gallon bay float in firmware. See SSR note below |
| ₱1 hopper motor | D33 | LOW (relay) | Hopper motor | Runs only until the outlet sensor counts out |
| ₱5 hopper motor | D34 | LOW (relay) | Hopper motor | Runs only until the outlet sensor counts out |
| Buzzer | D35 | HIGH | Active buzzer | Bottle wait warnings at 15 s and 18 s |
| Compressor | D36 | **LOW (AC SSR)** | Mains compressor | Thermostat control. See SSR note below |
| Level LED, low | D37 | HIGH | Indicator | Driven from the debounced floats |
| Level LED, mid | D38 | HIGH | Indicator | |
| Level LED, high | D39 | HIGH | Indicator | |

### Confirm button

| Signal | Pin | Mode | Active | Notes |
|---|---|---|---|---|
| Confirm | D40 | INPUT_PULLUP | LOW = pressed | 50 ms debounce. Advances ACCEPTING → SELECTING |

Whether the mockup also carries an on-screen confirm target is still open — the
physical button is specified and pinned either way.

### Pump and compressor are solid state, not mechanical — SSR

Decided in `SPECIFICATION.md` §1.5. The mechanical-contact-life argument is real but
secondary. The deciding reason is that contact arcing couples into a high-impedance
pulse input and reads as phantom coin pulses on D2, and a phantom coin is free water
for the user and inventory drift for the operator. It only shows up under load, so it
is miserable to chase after the machine is built and trivial to prevent now.

| Load | Device | Sizing | Notes |
|---|---|---|---|
| Compressor | AC SSR, zero-cross | ≥ 3× running current, for locked-rotor inrush | **Heatsink it.** An SSR dissipates roughly 1–1.5 W per amp passed and will run hot at compressor currents |
| Pump | DC SSR or MOSFET module | ≥ 2× running current | An AC SSR will not switch DC — do not substitute one |

Two things that catch people out:

**The compressor is inductive.** Fit an RC snubber across its terminals. A zero-cross
SSR switches off at a current zero crossing, which on an inductive load is not a
voltage zero crossing, so the SSR sees a step of several hundred volts at turn-off.

**AC SSRs leak.** A few milliamps of off-state leakage will make an unswitched load
tingle to the touch and can hold a small contactor partly energised. Confirm the
compressor is genuinely off, with a meter, before signing off the build.

Mains wiring is outside the scope of this firmware document. Have it done by someone
qualified for it, and fuse the compressor leg separately.

## Servo

| Signal | Pin | Notes |
|---|---|---|
| Coin diverter servo | D9 | Signal only. Servo power comes from the 5 V supply rail, **not** from the Mega's regulator |

The diverter must be in position before the coin arrives. The acceptor is inhibited for
`COIN_LOCKOUT_MS` (900 ms) after each accepted coin while the servo settles. The Servo
library on the Mega claims Timer5 first, which is why nothing else in this map uses the
Timer5 PWM pins D44–D46.

Diverter positions are set in `config.h` and must be measured on the assembled chute,
not assumed:

| Destination | Coins |
|---|---|
| ₱1 hopper | ₱1 |
| ₱5 hopper | ₱5 |
| Profit chamber | ₱10, ₱20, and any coin the firmware could not identify |

Three physical positions only. The firmware tracks ₱10, ₱20 and unidentified coins in
three separate chamber counters, but they all share the one profit angle — the split is
in the books, not in the mechanism.

## Analog inputs

None. A0–A15 are unused and available for service expansion.

Temperature is digital on D23 — see the DS18B20 note above. It feeds the System Status
screen only and never gates billing or dispensing.

## Power

| Rail | Feeds |
|---|---|
| 12 V | Solenoid valve, diaphragm pump, hopper motors |
| 5 V | Mega, coin acceptor, servo, sensors, Nextion |

The compressor cooling circuit is harvested from a bottom-load dispenser and runs on
mains. **The Mega switches it** through the AC SSR on D36, under thermostat control
with hysteresis and a minimum off-time — see `SPECIFICATION.md` §5.4. Restarting a
compressor against head pressure is how they fail, so the minimum off-time is not
optional.

Cooling never gates a transaction. Warm water is a service quality issue, not a fault.

Ground all supplies to a common point. The hopper motors and the pump are the noisiest
loads on the machine and share a ground with the coin acceptor pulse line — star-ground
at the supply rather than daisy-chaining, or motor noise will show up as phantom coin
pulses on D2.

Star-grounding is necessary but **not sufficient on its own**: the pulse-line shielding
and RC filtering described in the interrupt section above, and the move to solid-state
switching for the pump and compressor, are the other two thirds of the same fix. All
three are cheap during the build and expensive to retrofit into a finished cabinet.

## Parts added beyond the original bill of materials

Two sensors are specified here that neither `README.md`'s hardware table nor the
client's document lists. Both were signed off before Milestone 2 and are within the
existing build budget.

| Part | Pin | Approx cost | Why |
|---|---|---|---|
| DS18B20 waterproof stainless probe | D23 | ₱150 | Status screen temperature. Factory-calibrated, survives condensation |
| IR break-beam pair | D29 | — | Profit chamber full detection at the fill line |
| AC SSR + heatsink + RC snubber | D36 | — | Compressor switching. Replaces a mechanical relay — see the SSR note |
| DC SSR / MOSFET module | D32 | — | Pump switching. Replaces a mechanical relay |
| 3 × indicator LED + series resistor | D37–D39 | — | Tank level display, `SPECIFICATION.md` §1.3 |
| Momentary pushbutton | D40 | — | Confirm, `SPECIFICATION.md` §1.4 |
| DS3231 RTC module + CR2032 | D20/D21 | ₱150 | Wall-clock date and time. Without it "daily" totals mean "since the last power cut" — see `SPECIFICATION.md` §1.4.1 |
| Shielded cable ×2, 2 × (1 kΩ, 4.7 kΩ), 100 nF, 10 nF | D2, D3 | — | Pulse-line shielding and RC filtering |

Update the hardware table in `README.md` to match before handover.
