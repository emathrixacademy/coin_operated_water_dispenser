# Firmware audit — functions, logic, and completion status

> **SUPERSEDED IN PART, 2026-08-30.** This document was written before
> `docs/SPECIFICATION.md` became authoritative. It remains accurate as a
> description of module behaviour, but its defect list has since been acted on:
> see `docs/reconciliation.md` for the code-versus-spec comparison and
> `docs/remaining.md` for the work inventory. The four defects called out in §6
> here (items 9–11 and the `coin_hopper_plan()` blocker) have been fixed.

Project EMX-2026-WATERVENDO-01, eMathrix Technologies.
Audited at commit `965b0f0` (Milestone 2) plus uncommitted working-tree changes.
Date of audit: 2026-08-30.

This document describes what each module does, what is actually implemented, and
what is still a stub. It is written for a reviewer who has not read the code.

---

## 1. Verification performed for this audit

| Check | Result |
|---|---|
| `pio run -e release` | **SUCCESS.** Links clean. RAM 455 B / 8192 (5.6%), Flash 6950 B / 253952 (2.7%) |
| `pio test -e native` | **DID NOT RUN.** No `gcc`/`g++` on the build machine. This is a missing host toolchain, not a test failure — the tests have never been executed on this machine |
| Dynamic allocation | None found. No `String`, `new`, `malloc`, `std::vector` anywhere in `src/` or `include/` |
| `delay()` outside `setup()` | None found |
| Float in the money/volume path | None found. All money is `int32_t` centavos, all volume `int32_t` millilitres |
| Interrupt count | Two, as specified: `PIN_COIN_PULSE` (D2) and `PIN_FLOW_PULSE` (D3). Both ISRs only bump a `volatile` counter |
| Magic numbers in logic files | None found. All constants resolve to `config.h` |

The low flash figure (2.7%) is itself a finding: it reflects how much of the
firmware is still empty stubs.

---

## 2. Overall state — the headline

**The individual money-handling modules are largely built and are the strongest
part of the codebase. The machine that would use them does not exist yet.**

`src/main.cpp` calls every module's `begin()` and `update()` correctly, but its
state machine is an empty `switch` with every case falling through to `break`.
There are no transitions. As a direct consequence:

- `billing_add_coin()` has no caller. Coins are resolved but never credited.
- `coin_diverter_route()` has no caller. No coin is ever routed.
- `coin_hopper_dispense()` has no caller. No change is ever paid.
- `faults_raise()` has no caller. No fault is ever raised.
- `persist_txn_open()` / `persist_daily_add()` have no callers. Nothing is ever
  recorded for a transaction.
- `coin_acceptor_uninhibit()` has no caller, and `coin_acceptor_begin()` boots
  with `s_fault_inhibit = true`.

**Flashing the current release build to hardware produces a machine that
inhibits its coin acceptor at boot and never re-enables it.** It takes no money,
which is the correct failure direction, but it does nothing at all.

Milestone status inferred from the `TODO` markers in the source:

| Milestone | Scope | Status |
|---|---|---|
| M1 | Project skeleton, scenario document | Complete |
| M2 | `config.h`, module skeletons | Complete |
| M3 | Coin path (acceptor, diverter, hopper), persistence | **Substantially complete** — appears to have been done ahead of order, and is uncommitted working-tree work |
| M4 | Flow, bottle, water level, dispense | **Not started** — stubs only |
| M5 | State machine, faults, boot reconciliation, hopper change plan | **Not started** |
| M6 | Nextion HMI | **Not started** — stubs only |
| M7 | Test pass against `docs/scenarios.md` | Not started; result log is empty |

---

## 3. Module-by-module

### 3.1 `billing.*` — WORKING

Pure integer arithmetic, deliberately free of any Arduino dependency so it can
be linked into host-side tests. This is the module the whole design exists to
protect, and it holds up.

| Function | Behaviour |
|---|---|
| `coin_value(coin)` | Denomination → centavos. Invalid/none → 0 |
| `billing_add_coin(coin)` | Adds to `inserted` and `credit`. Silently refuses any coin that would exceed `MAX_TRANSACTION_CENTAVOS` (₱20) |
| `billing_credit()` / `billing_inserted()` | Remaining spendable credit; total inserted this transaction |
| `billing_at_ceiling()` | True at ₱20 inserted |
| `billing_max_selectable_ml()` | `(credit / 100) * 100` mL — integer truncation, so a partial peso buys nothing |
| `billing_price_of(target_ml)` | **Volume in, price out.** A price-list lookup on a *selection*, never on a measurement |
| `billing_can_select(ml)` | Rejects ≤ 0, > 2000 mL, non-multiples of 100 mL, and anything above credit |
| `billing_select(ml)` | **Deducts the price before the valve opens.** Sets `target_ml`, zeroes `dispensed_ml` |
| `billing_round_down(ml)` | `(ml / 100) * 100`. Always down. Negative → 0 |
| `billing_settle_partial(ml)` | Rounds **first**, caps the charge at `target_ml`, returns `paid − owed` to credit |
| `billing_settle_complete(ml)` | Ignores the measured argument and credits `target_ml` to totals, so daily volume cannot drift against daily profit by the sensor tolerance |
| `billing_worst_case_change()` | ₱20 − price of 100 mL = ₱19. Intended as a pre-transaction hopper guard |
| `billing_load` / `billing_store` | Copies the whole `transaction_t` for persistence |

**Direction of causation verified.** There is no function anywhere in
`billing.h` that takes a measured volume and returns an amount owed. The single
point where a measurement touches money is `billing_settle_partial()`, and the
rounding happens before the pricing.

Audit note: `transaction_t.dispensed_ml` is written by billing but never read by
it — the live delivered figure lives in `dispense`. Harmless today, but it is a
second copy of the same number and the state machine must not let the two
disagree.

### 3.2 `eeprom_record.*` — WORKING

Record framing: `[magic lo][magic hi][layout version][crc8] payload…`.
`record_crc8()` is Dallas/Maxim CRC-8 (poly 0x8C reflected).
`record_unpack()` checks **magic, then version, then CRC**, in that order, and
leaves the caller's payload untouched on any failure.

This is what stops a virgin EEPROM cell (all `0xFF`) reading as 65535 coins in
each hopper. Correct and testable off-target.

### 3.3 `persist.*` — WORKING, with two findings

Five EEPROM regions: in-flight coin, inventory, open transaction, wear-levelled
daily ring (8 slots), history ring (20 entries). Region boundaries are enforced
by `static_assert` at compile time, including a check that the history ring fits
inside the Mega's 4 KB.

- `persist_begin()` — loads each region; on a failed unpack, zeroes the
  inventory, commits it, and sets `persist_was_initialised()` so the Admin page
  can tell a technician the inventory was reset rather than restored. Zero is the
  correct failure direction: it locks the machine on LOW CHANGE until a human
  loads real counts.
- `persist_update()` — deliberately empty. Persistence is event-driven; a timer
  here is what would burn the EEPROM out.
- `persist_inventory_add(dest, delta)` — **clamps** to `[0, HOPPER_CAPACITY]`
  rather than wrapping, then commits. Prevents an underflow re-creating the
  65535-coin bug from the arithmetic side.
- `persist_inventory_set(dest, count)` — admin correction, bounded, and writes an
  `EVT_ADMIN_EDIT` history entry recording before and after.
- In-flight coin mark/clear/read — the mechanism behind the case-13 recovery.
- `persist_reconcile_unrouted_coin(coin)` — implements the decided policy:
  writes an `EVT_COIN_UNROUTED` entry and clears the marker. Neither hopper nor
  the profit count is incremented, because the coin's location is genuinely
  unknown. **Fails toward understating stock**, as specified.
- Daily ring — commit advances the slot each write and bumps a sequence number;
  boot takes the valid slot with the highest sequence.
- History ring — `persist_history_get(0)` is the most recent; a slot whose
  sequence does not match the expected one returns `nullptr` rather than stale
  data.

**Finding P-1 (defect).** `persist_inventory_add()` and
`persist_inventory_set()` both map `DEST_PROFIT` to `s_inventory.profit_p10`.
`inventory_t::profit_p20` is declared in `types.h` and **is never written or read
anywhere in the firmware**. Every ₱10 and every ₱20 accumulates into one counter,
so the profit chamber's peso value cannot be derived from the inventory — a ₱10
and a ₱20 are indistinguishable in the books. This is not currently visible
because nothing reads the profit count, but it will produce a wrong figure on the
Coin Inventory screen. Either split the destination enum so the diverter reports
which of the two it routed, or delete `profit_p20` and document the count as
"coins in chamber, denomination not tracked".

**Finding P-2 (design gap).** `faults_is_persistent()` declares that a change
jam, flow stall, pump overrun and acceptor fault must survive a reboot, but there
is **no EEPROM region for faults** in `config.h` and nothing writes one. As
written, power-cycling the machine clears every fault — which is exactly the
behaviour `faults.h` says must not happen.

### 3.4 `coin_acceptor.*` — WORKING

- ISR bumps `s_pulses` and stamps `s_last_pulse_ms`. Nothing else.
- `update()` waits for the pulse line to be idle for `COIN_PULSE_GAP_MS` (200 ms)
  before resolving the burst, so a 4-pulse ₱20 is not read as two coins.
- **Reject, never queue.** While inhibited, `update()` drains the pulse counter
  on every pass and returns. Stray pulses can never accumulate into a phantom
  coin or merge with the next real coin's train. This is scenario case 14.
- Two independent inhibit sources (`window` and `fault`), tracked separately
  rather than counted, with a single `apply_inhibit()` choke point on the pin.
  Releasing the diverter window can never clear a fault lockout.
- Trains longer than `COIN_PULSE_MAX` (8) are discarded and bump
  `s_overmax_streak`; the streak resets only on a train that resolves to a real
  denomination.
- Boots with the fault inhibit **asserted**.

**Finding A-1 (incomplete).** `COIN_OVERMAX_FAULT_MAX` (5) is defined and
`coin_acceptor_overmax_streak()` is exposed, but **nothing compares them**. A
stuck acceptor output line will silently swallow coins forever with no fault
shown. Deferred to M5 by design, but it is the specific failure the constant was
written for and must not be forgotten.

### 3.5 `coin_diverter.*` — WORKING

Two-state machine, `DIV_IDLE` / `DIV_MOVING`.

`coin_diverter_route()` does things in this order, and the order is the whole
module:

1. Assert the acceptor window inhibit — **before** the servo moves.
2. Write the in-flight coin marker to EEPROM.
3. Command the servo.
4. Stamp the start time.

`coin_diverter_update()` does nothing until `COIN_LOCKOUT_MS` (900 ms) has
elapsed, then increments the inventory, clears the in-flight marker, and releases
the window inhibit. **Inventory increments after physical commit and nowhere
else**, which is what makes the case-13 reconciliation possible.

`coin_destination()` sends ₱1 and ₱5 to their hoppers and *everything else*,
including unrecognised values, to the profit chamber — failing toward
understating hopper stock.

### 3.6 `coin_hopper.*` — MOSTLY WORKING, one function deliberately blank

Four phases: `HOP_IDLE` → `HOP_RUNNING` → `HOP_SETTLING` → `HOP_DONE`.

- `poll_outlet()` — polled, not interrupt-driven (both interrupts are spent
  elsewhere). Debounced at `HOPPER_COUNT_DEBOUNCE_MS` (15 ms); counts falling
  edges only.
- `HOP_SETTLING` — after the motor cuts, waits `HOPPER_SETTLE_MS` (300 ms) while
  still polling, so a coin already in the throat is counted. Without this the
  machine would retry a payout that succeeded and overpay.
- Retry pays **the shortfall only** (`s_counted` is cumulative across attempts,
  `s_commanded` is the total), up to `HOPPER_RETRY_MAX` (3).
- `finish()` — decrements inventory by coins **counted**, never commanded. On a
  jam it writes an `EVT_CHANGE_JAM` history entry recording commanded vs counted,
  so the technician knows how much of the user's change reached the tray.

**Finding H-1 (blocking, deliberate).** `coin_hopper_plan()` is a stub that
always returns `false`, with a comment stating that the ₱1-covers-₱5-dissipation
strategy and the sustained-drain arithmetic for the ₱20-in/500 mL-out pattern
(scenario case 18) are **reserved for client review before a policy is chosen**.
Because `coin_hopper_can_cover()` is implemented purely in terms of `plan()`, it
also always returns `false`. Until this decision is made, no change can be
planned or paid. This is the single largest functional blocker in the coin path,
and it is blocked on a decision, not on code.

**Finding H-2 (minor).** The comment above `s_last_level` / `s_last_edge_ms` says
"per hopper", but there is one shared pair of statics, not one per hopper. This
is safe as written because only one payout runs at a time and `start_attempt()`
re-reads the level for the active hopper — but a mixed ₱1 + ₱5 payout must be
sequenced by the caller, never overlapped. `coin_hopper_dispense()` correctly
refuses a second command while busy.

### 3.7 `faults.*` — PARTIAL

Working: `faults_raise()` calls `coin_acceptor_inhibit()` **before** it does
anything else, as required. `faults_message()` returns PROGMEM strings for all
seven fault types. `faults_is_persistent()` classifies correctly.

Not working:
- `faults_update()` is empty — no auto-clear re-evaluation for water or storage.
- `faults_clear()` is empty — a serviceable fault cannot be cleared at all.
- `faults_raise()` has no priority ordering; the last raise wins, so a low-change
  fault could mask a change jam.
- No caller anywhere raises a fault (see §2).
- No persistence (see finding P-2).

### 3.8 `flow.*` — STUB

Implemented: the ISR, `flow_begin()` (pin + interrupt attach), `flow_reset()` and
`flow_pulses()` — both of which correctly guard the multi-byte `volatile` read
with `noInterrupts()`.

Not implemented: `flow_update()` is empty; `flow_ml()` returns 0 unconditionally;
`flow_is_stalled()` returns false unconditionally; `flow_set_valve_open()` does
nothing. The remainder-carry mL conversion and stall detection are both M4.

`FLOW_PULSES_PER_LITRE` is **450, the datasheet placeholder**, explicitly marked
as not-yet-calibrated. It must be measured per unit with a graduated cylinder at
the actual dispensing flow rate before the machine handles money.

### 3.9 `bottle.*` — STUB

`bottle_begin()` sets the pin. `bottle_update()` is empty. `bottle_present()`,
`bottle_just_placed()` and `bottle_just_removed()` all return `false`
unconditionally. **As it stands the machine can never detect a bottle**, so the
entire dispense path is unreachable even if the state machine existed.

### 3.10 `water_level.*` — STUB, with the safety interlock already in place

The one thing that *is* implemented is the important one: `pump_write()` is a
single choke point for the pump output that reads `PIN_FLOAT_GALLON` and refuses
to energise the pump when the gallon bay is empty, regardless of the caller's
intent. `water_level_begin()` calls it with `false`. No future edit can add a
path that runs the pump dry without going around this function.

Everything else is a stub: `water_level_update()` is empty, and all seven query
functions return constants (`false`, or `TEMP_INVALID_C`). No float debouncing,
no pump control, no `PUMP_MIN_OFF_MS` enforcement, no `PUMP_MAX_RUN_MS` ceiling,
no DS18B20 reading — the OneWire and DallasTemperature libraries are declared in
`platformio.ini` but **no source file includes or uses them**.

### 3.11 `dispense.*` — STUB

`valve_write()` and `dispense_begin()` exist and close the valve at boot. Every
other function is a `TODO(M4)`; `dispense_status()` returns `DISPENSE_IDLE`,
`dispense_delivered()` and `dispense_target()` return 0.

### 3.12 `hmi.*` — STUB

`hmi_begin()` opens `Serial2` at 115200 and sends three `0xFF` bytes to flush any
partial command left in the display's parser. `hmi_setPage()` records the page in
a local variable but sends nothing. Every other function is a `TODO(M6)` with its
arguments cast to `void`. **No command is ever transmitted to the Nextion and no
touch event is ever decoded.** `hmi_event_available()` returns `false` forever,
so even a finished state machine would receive no user input.

The `hmi/watervendo.HMI` Nextion project file is also outstanding.

---

## 4. Hardware declared but not wired to any code

| Pin | Purpose | Status |
|---|---|---|
| `PIN_CHAMBER_FULL` (D29) | IR break-beam, profit chamber full | **Never read.** `FAULT_STORAGE_FULL` can never be raised |
| `PIN_BUZZER` (D35) | Bottle-wait warnings | **Never driven.** The 15 s / 18 s buzzers of scenario case 7 do not exist |
| `PIN_TEMP_ONEWIRE` (D23) | DS18B20 probe | **Never read.** Status-screen temperature unavailable |

`HOPPER_START_FLOAT` (100) is defined but unused — commissioning counts are
expected to be entered through the Admin page, which is not built.

---

## 5. Test coverage

`test/test_billing/` — 26 cases covering rounding (including the documented
305 mL → 300 mL case, boundaries, negatives, and an explicit
never-rounds-to-nearest test), coin values, the ₱20 ceiling, selection pricing,
partial and complete settlement, worst-case change, and a
no-money-created-or-destroyed invariant.

`test/test_eeprom/` — 15 cases covering virgin (`0xFF`) and erased (`0x00`) cell
rejection, round-trips, **every single bit flip in the payload**, corrupt magic,
wrong layout version, CRC determinism and transposition detection, and the
virgin-transaction-does-not-resume case.

Both suites are well aimed at the money math. Two caveats:

1. **They have not been run.** The host has no `gcc`/`g++`, so `pio test -e
   native` errors before compiling. Install a host toolchain (MinGW-w64 or
   equivalent) and re-run before treating these as passing.
2. **Coverage stops at the two Arduino-free files.** There are no tests for
   `coin_acceptor`, `coin_diverter`, `coin_hopper`, `persist` or the state
   machine. That is a consequence of the Arduino dependency, not an oversight,
   but it means the pulse-resolution, lockout-window and payout-retry logic is
   currently verified by reading only.

Per `docs/scenarios.md`, the coin path cannot be signed off by simulation at all:
real coins, both series, including wet ones. The Milestone 7 result log is empty
— **0 of 16 scenario cases have been executed.**

---

## 6. Prioritised gap list

**Blocked on a client decision — resolve first**

1. `coin_hopper_plan()` denomination strategy (case 18 sustained-drain
   arithmetic). Nothing downstream can pay change until this is settled.

**Blocking for any functioning machine**

2. `main.cpp` state machine — all twelve states and their transitions.
3. Boot reconciliation in `setup()`: unrouted coin, open transaction resume,
   persistent fault re-raise.
4. `flow.*` mL accumulation and stall detection (M4).
5. `bottle.*` debounce and edge detection (M4).
6. `water_level.*` float debounce and pump control (M4).
7. `dispense.*` pour control, cutoff, pause/resume (M4).
8. `hmi.*` command transmission and touch decoding, plus the `.HMI` project file
   (M6).

**Correctness defects to fix in place**

9. P-1 — `profit_p20` never written; ₱10 and ₱20 share one counter.
10. P-2 — persistent faults have no EEPROM region and do not survive a reboot.
11. A-1 — `COIN_OVERMAX_FAULT_MAX` never compared; stuck acceptor line raises
    nothing.
12. `faults_clear()` and `faults_update()` empty; no fault priority ordering.
13. Chamber-full, buzzer and temperature pins unused.

**Before the machine handles money**

14. Measure `FLOW_PULSES_PER_LITRE` per unit; 450 is a placeholder.
15. Measure the three `DIVERTER_ANGLE_*` values on the assembled chute; the
    current values are starting points, not correct values.
16. Install a host toolchain and get both native suites green.
17. Execute all 16 scenario cases with real coins and record them in the
    Milestone 7 result log.
18. Confirm the shipped build is `release` (no `-DDEBUG`).

---

## 7. Assessment

The parts that are built are built carefully and to the standard the project
document sets. The billing arithmetic, the coin lockout window, the EEPROM
framing, the counted-not-commanded payout rule and the pump dry-run interlock are
all correct as written, and each carries a comment explaining the failure mode it
exists to prevent. The order-of-operations requirements — inhibit before servo,
mark before move, increment after commit, round before price, acceptor before
screen — hold everywhere they appear.

The gap is that roughly half the firmware is still stubs and none of it is
connected. The machine cannot currently take a coin, detect a bottle, pour water,
pay change, display anything, or raise a fault. It is a well-built set of parts
with no assembly, and no part of it has yet been exercised on hardware or in a
test run.
