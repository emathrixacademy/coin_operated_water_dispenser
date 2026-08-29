# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project

Firmware for a coin-operated water refill vending machine. Standalone machine, no
network, no cloud. Arduino Mega 2560 is the controller. A Nextion HMI runs the UI on
its own processor.

Client project EMX-2026-WATERVENDO-01, eMathrix Technologies. Academic research
prototype for a TUP-Cavite thesis team.

## Hard rules

These are not preferences. Do not change them without asking.

- **Arduino C++ on PlatformIO.** Not Arduino IDE, not MicroPython, not an RTOS.
- **Arduino Mega 2560 only.** Do not port to ESP32 or Pi. The pin count and the 5V
  logic are load-bearing.
- **No dynamic allocation.** No `String`, no `new`, no `malloc`, no `std::vector`.
  Fixed buffers and `char[]` only. The Mega has 8KB of SRAM and this machine holds
  money.
- **No blocking calls in the main loop.** No `delay()` outside setup. Everything is
  non-blocking with `millis()` timers. A blocked loop is a missed coin pulse.
- **No graphics work on the Mega.** The Nextion draws everything. The Mega only sends
  short serial commands.
- **No floating point for money.** Money is integer centavos or whole pesos. Volume is
  integer millilitres.

## Architecture

Single non-blocking state machine in `loop()`. Every subsystem is a module that exposes
`begin()`, `update()`, and query functions. Nothing owns the loop.

```
src/
  main.cpp            state machine, top-level transitions only
  config.h            pin map, timing constants, calibration constants
  coin_acceptor.*     pulse capture, coin value resolution
  coin_diverter.*     servo routing, per-coin lockout window
  coin_hopper.*       change payout, outlet counting, jam retry
  flow.*              flow pulse capture, volume accumulation
  bottle.*            proximity sensor debounce, present/absent
  water_level.*       three floats, pump control, lockout states
  dispense.*          valve control, cutoff, partial-stop handling
  billing.*           coin-to-volume allocation, refund computation
  persist.*           EEPROM read/write of coin inventory and open transaction
  hmi.*               Nextion serial commands, screen state
  faults.*            lockout conditions and alert routing
```

Interrupts are used only for the coin acceptor pulse pin and the flow sensor pulse pin.
ISRs set `volatile` counters and nothing else. All interpretation happens in `update()`.

## Billing logic — do not invert this

**Coins determine volume. Volume never determines coins.**

The machine reads coins first, computes the volume entitlement, opens the valve, and
uses the flow sensor purely as a cutoff. ₱1 = 100 mL. Maximum ₱20 per transaction, so
2000 mL maximum.

If you find yourself writing code that computes an amount owed from a measured volume,
stop. That is the bug this design exists to prevent. The flow sensor has 2–5% tolerance
and billing from its reading makes the change computation drift, which loses coins from
the hoppers on every transaction.

**Partial refunds round down to the nearest 100 mL.** If a user stops at a measured
305 mL, treat it as 300 mL dispensed, refund 200 mL worth. Rounding always favours the
machine, never the user. This is deliberate. Never round to nearest, never round up.

Refund coins come from the coin math, not from the sensor reading. The ₱1 hopper covers
₱5 dissipation so it is not drained first.

## Coin handling

One acceptor identifies all four denominations by pulse count. A servo diverter routes
each coin after identification: ₱1 and ₱5 to their hoppers, ₱10 and ₱20 to the locked
profit chamber.

The diverter must be in position before the coin arrives. After each accepted coin the
acceptor is disabled for `COIN_LOCKOUT_MS` (default 900 ms) while the servo settles.
Do not shorten this to make the machine feel faster. A coin landing mid-travel jams the
chute.

Each hopper has an outlet counting sensor. A payout is not complete because the hopper
was told to run — it is complete when the sensor counts the coins out. If the expected
count is not reached within `HOPPER_TIMEOUT_MS`, retry up to `HOPPER_RETRY_MAX` times,
then lock the machine with `CHANGE JAM — SERVICE REQUIRED`. Never assume a payout
succeeded.

## Water level

Two floats in the cold tank control the pump: mid turns it on, high turns it off. One
float in the gallon bay detects empty and locks the machine.

The gallon-bay float is a safety lockout, not pump control. Running the pump dry
destroys it. If the gallon float reads empty, the pump must not run regardless of what
the cold tank floats say.

## Power loss

Coin inventory, daily totals, and any open transaction are written to EEPROM at the end
of each transaction and at each inventory change. On boot, the machine restores from
EEPROM and resumes an interrupted transaction with its remaining balance shown.

Write per transaction, never per loop iteration. EEPROM is rated for roughly 100,000
writes per cell. Use `EEPROM.update()`, not `EEPROM.write()`. Wear-level the daily
counters across a small ring of cells.

## Nextion HMI

The Mega sends short commands over `Serial2` and reads touch events. It never renders.

```cpp
hmi_setText("t_amount", "20.00");
hmi_setPage(PAGE_DISPENSING);
```

Screens: Home, Statistics, System Status, Coin Inventory, plus an Admin page for change
loading and inventory correction. Alert states: out of water, low change, coin storage
full, change jam.

Every command is terminated with three `0xFF` bytes. If you are adding a screen, the
`.HMI` project file changes too, not just the firmware.

## Fault states that lock the machine

| Condition | Display |
|---|---|
| Gallon bay empty | OUT OF WATER — PLEASE REFILL |
| ₱1 below 25 pcs or ₱5 below 5 pcs | LOW CHANGE — SERVICE REQUIRED |
| Profit chamber full | COIN STORAGE FULL |
| Hopper payout failed after retries | CHANGE JAM — SERVICE REQUIRED |

In every locked state the coin acceptor is disabled first. Never accept money the
machine cannot honour.

## Transaction timing

| Event | Timing |
|---|---|
| Bottle wait, no warning | 0–15 s |
| First buzzer | 15 s |
| Second buzzer | 18 s |
| Cancel and refund | 20 s |
| Bottle removed mid-dispense, grace to replace | 10 s |

Bottle removed and replaced within the grace window resumes dispensing. Not replaced
ends the transaction, and any change still due is paid out on confirm.

## Testing

Every one of the ten scenario cases in `docs/scenarios.md` needs a test before this ships.
Coin path and change payout are tested with real coins, both old and current series,
including wet coins. Simulated pulses do not count as a passing test for the coin path.

Flow calibration is per-unit. `ML_PER_PULSE` in `config.h` is measured on the actual
sensor with a graduated cylinder, not taken from the datasheet.

## Style

- Two-space indent, `snake_case` functions, `UPPER_SNAKE` constants.
- Constants live in `config.h`. No magic numbers in logic files.
- Comment why, not what. Especially on any timing constant — say what breaks if it changes.
- Serial debug output behind `#ifdef DEBUG`. Never left enabled in a build that handles money.

## Out of scope

Do not add: WiFi, Bluetooth, cloud sync, remote monitoring, mobile app, GPS, card
payment, or an SD card logger. Any of these is a new scope item under the service
agreement and must be quoted before it is built.
