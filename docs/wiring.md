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

## Serial

| Port | Pins | Use |
|---|---|---|
| Serial | D0 / D1 (USB) | Debug output, `DEBUG` builds only. Never enabled in a release build. |
| Serial2 | D16 TX2 / D17 RX2 | Nextion HMI, 115200 baud |

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

## Digital outputs

The relay board is opto-isolated and active-low. Drive the pump, valve and hopper
motors through it — none of these loads may be driven from a Mega pin directly.

| Signal | Pin | Active | Load | Notes |
|---|---|---|---|---|
| Coin acceptor inhibit | D30 | HIGH = inhibited | Acceptor inhibit line | Asserted first in every lockout, and during `COIN_LOCKOUT_MS` |
| Solenoid valve | D31 | LOW (relay) | 12 V solenoid | Flyback diode required at the valve |
| Pump | D32 | LOW (relay) | 12 V diaphragm pump | Gated by the gallon bay float in firmware |
| ₱1 hopper motor | D33 | LOW (relay) | Hopper motor | Runs only until the outlet sensor counts out |
| ₱5 hopper motor | D34 | LOW (relay) | Hopper motor | Runs only until the outlet sensor counts out |
| Buzzer | D35 | HIGH | Active buzzer | Bottle wait warnings at 15 s and 18 s |

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
| Profit chamber | ₱10, ₱20 |

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
mains independently of the Mega. The firmware reads its state for the System Status
screen but does not control it.

Ground all supplies to a common point. The hopper motors and the pump are the noisiest
loads on the machine and share a ground with the coin acceptor pulse line — star-ground
at the supply rather than daisy-chaining, or motor noise will show up as phantom coin
pulses on D2.

## Parts added beyond the original bill of materials

Two sensors are specified here that neither `README.md`'s hardware table nor the
client's document lists. Both were signed off before Milestone 2 and are within the
existing build budget.

| Part | Pin | Approx cost | Why |
|---|---|---|---|
| DS18B20 waterproof stainless probe | D23 | ₱150 | Status screen temperature. Factory-calibrated, survives condensation |
| IR break-beam pair | D29 | — | Profit chamber full detection at the fill line |

Update the hardware table in `README.md` to match before handover.
