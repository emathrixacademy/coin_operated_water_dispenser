# src/

Firmware implementation. At Milestone 2 every module exists with its `begin()`,
`update()` and query functions stubbed. **No logic yet** — the point is that the module
boundaries and the header contracts are reviewable before implementation.

Headers live in [include/](../include/); implementations here.

| File | Implements at |
|---|---|
| `main.cpp` | M5 — state machine, top-level transitions only |
| `coin_acceptor.cpp` | M3 — pulse capture, coin value resolution |
| `coin_diverter.cpp` | M3 — servo routing, per-coin lockout window |
| `coin_hopper.cpp` | M3 — change payout, outlet counting, jam retry |
| `persist.cpp` | M3 — EEPROM inventory, open transaction, history |
| `flow.cpp` | M4 — flow pulse capture, volume accumulation |
| `bottle.cpp` | M4 — proximity debounce, present/absent |
| `water_level.cpp` | M4 — three floats, pump control, lockout |
| `dispense.cpp` | M4 — valve control, cutoff, partial-stop handling |
| `billing.cpp` | M5 — coin-to-volume allocation, refund computation |
| `faults.cpp` | M5 — lockout conditions and alert routing |
| `hmi.cpp` | M6 — Nextion serial commands, screen state |

Stubs carry `TODO(Mn)` markers naming the milestone that fills them in, plus the
implementation notes worth not rediscovering — why the hopper needs a settle delay
before declaring a count final, why the flow remainder is carried between calls, why
the pump output has a single choke point.

## Invariants the stubs already encode

A few things are written into the skeleton rather than left for later, because they are
the ones that are expensive to retrofit:

- **`pump_write()` in `water_level.cpp` is the single choke point for the pump output**,
  and the gallon-bay inhibit lives inside it. No future edit can add a path that
  energises the pump without passing the check.
- **`faults_raise()` inhibits the acceptor before it returns**, ahead of any screen
  change. Never accept money the machine cannot honour.
- **`coin_destination()` defaults to the profit chamber** for anything unrecognised,
  keeping odd coins out of the change float.
- **`billing_round_down()` is implemented and only rounds down.** It is pure integer
  arithmetic and getting its direction wrong is the expensive mistake in the file.
- **`billing.h` deliberately contains no function** that takes a millilitre reading and
  returns an amount owed. Coins determine volume; volume never determines coins.
