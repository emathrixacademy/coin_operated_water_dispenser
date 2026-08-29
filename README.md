# Water Refill Vending Machine

Firmware for a coin-operated water refill vending machine with bottle detection,
flow-controlled dispensing, automatic change recirculation, and a touchscreen
monitoring interface.

Built by eMathrix Technologies. Project EMX-2026-WATERVENDO-01.

---

## What it does

A student inserts coins, places their own bottle, and gets chilled drinking water.
Change comes back automatically from coins other users paid in.

₱1 buys 100 mL. Maximum ₱20 per transaction, so 2 litres per fill. ₱10 and ₱20 coins go
to a locked profit chamber. ₱1 and ₱5 coins go to hoppers and get reused as change.

## Hardware

| Part | Role |
|---|---|
| Arduino Mega 2560 | Main controller |
| Nextion HMI 3.5in | Touchscreen UI, runs on its own processor |
| Multi-coin acceptor | Identifies ₱1, ₱5, ₱10, ₱20 by pulse output |
| Servo diverter | Routes each coin to hopper or profit chamber |
| Coin hoppers ×2 | ₱1 and ₱5 change payout, with outlet counting sensors |
| Flow sensor | Volume cutoff |
| Solenoid valve | Dispense control |
| Diaphragm pump | Gallon bay to cold tank |
| Float switches ×3 | Two in the cold tank, one in the gallon bay |
| Proximity sensor | Bottle detection |
| Compressor cooling | Chilled 6 L tank, harvested from a bottom-load dispenser |

Cabinet is a 178 × 69 × 61 cm powder-coated steel frame. Two 5-gallon bottles sit in the
base, chilled tank above.

## How billing works

Coins first, then volume. Never the other way round.

The machine reads the coins, works out the volume they buy, opens the valve, and closes
it when the flow sensor reaches the target. The flow sensor is a stopper, not a cashier.

This matters because flow sensors of this class carry 2–5% tolerance. Billing from a
measured volume means the charge drifts on every transaction, and the drift comes out of
the change hoppers. Fixing the coin math before the valve opens removes the problem
entirely.

Partial refunds round down to the nearest 100 mL. Stop at a measured 305 mL and the
machine counts 300 mL, refunding ₱2. Rounding always favours the machine.

## Operating states

Normal flow is insert coins, press confirm, place bottle, dispense, collect change.

The machine locks itself and stops accepting coins when the gallon bay is empty, when
change stock runs low, when the profit chamber is full, or when a hopper jams. It never
takes money it cannot honour.

Bottle removed mid-dispense pauses the pour and gives 10 seconds to replace it. No bottle
after confirm buzzes at 15 and 18 seconds and cancels at 20.

Coin inventory, daily totals, and any interrupted transaction survive a power cut through
EEPROM. On boot the machine restores its state and resumes.

## Screens

Home shows the amount inserted, volume bought, volume dispensed, and remaining balance.
Statistics shows water consumed and profit for the day. System Status shows water level,
temperature, and cooling state. Coin Inventory shows how much ₱1 and ₱5 change is left.
An admin page handles change loading and inventory correction.

## Build

Requires [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/emathrixacademy/watervendo.git
cd watervendo
pio run                  # build
pio run -t upload        # flash the Mega
pio device monitor       # serial output, DEBUG builds only
```

The Nextion UI is a separate project. Open `hmi/watervendo.HMI` in the Nextion Editor and
flash it to the display over microSD or serial. Firmware and HMI versions must match —
adding a screen means changing both.

## Calibration

Flow calibration is per unit. Dispense into a graduated cylinder, count pulses, and set
`ML_PER_PULSE` in `config.h`. Do not use the datasheet figure.

Coin calibration is done on the acceptor itself, in learning mode, with real coins. Sample
both the old and current series of each denomination, and sample the ₱20 more times than
the rest since it is thinner and lighter than the others.

## Repository layout

```
src/            firmware modules
include/        headers
hmi/            Nextion editor project
docs/           scenario cases, wiring, calibration notes
test/           unit tests
platformio.ini  build configuration
```

## Servicing

The side door reaches the diverter and both hoppers. Coin jams are the most common
failure on a machine like this, not the water side, so keep that path clear and dry.

Load 100 coins into each hopper as the starting float before operation. Below 25 pieces of
₱1 or 5 pieces of ₱5, the machine disables itself until refilled.

## Licence and ownership

Source code, control logic, circuit design, HMI project files, and fabrication procedures
are Background Intellectual Property of eMathrix Technologies and are not licensed for
redistribution, resale, or reverse engineering. See the service agreement.
