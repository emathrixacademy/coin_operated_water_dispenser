# Remaining work — from here to a machine that passes docs/scenarios.md

Project EMX-2026-WATERVENDO-01. Compiled 2026-08-30 against
`docs/SPECIFICATION.md`, `docs/scenarios.md` and the code at commit `965b0f0`
plus uncommitted work.

**Category** is one of: `code` · `hardware decision` · `client decision` ·
`physical calibration` · `verification`.

**Size** is engineering effort only, and excludes waiting on a decision or on
hardware arriving. Sizes assume the person already knows this codebase.

Within each milestone, items are ordered so that blockers come before what they
block.

---

## Standing constraints on everything below

- **No hardware exists yet.** Every per-unit calibration in M8 is unmeasurable
  until an assembled machine is on the bench. Nothing in M8 can be scheduled.
  This is now the *only* remaining blanket blocker.
- ~~The client's mockup PDF has not been seen.~~ **RESOLVED 2026-08-30.**
  `poppler` installed; `WATER-VENDO.pdf` (2 pages, 8 screens) rendered and read.
  Findings are in §M — three of them change work already scheduled, and one is
  a new hardware requirement.
- ~~The native tests have never been executed here.~~ **RESOLVED 2026-08-30.**
  Host toolchain installed; **75/75 passing** across four suites. See
  `test/README.md` for the toolchain setup and the 248-character path trap that
  makes the winget install fail in a way that looks nothing like a path problem.

---

## R — Remediation — COMPLETE (2026-08-30)

Defects from `docs/reconciliation.md`. All closed; kept for the audit trail.

| # | Item | Status |
|---|---|---|
| R-1 | Split `coin_dest_t`; wire `profit_p20` (A-6, §7.1) | **Done** |
| R-2 | Profit-chamber capacity ceiling separate from `HOPPER_CAPACITY` (A-12) | **Done** — `PROFIT_CHAMBER_CAPACITY` |
| R-3 | Fault-state EEPROM region; restore persistent faults on boot (A-7) | **Done** — `EEPROM_ADDR_FAULTS` |
| R-4 | Fault priority ordering as a flags mask, not last-write-wins (§6.2) | **Done** — extracted to `fault_mask.*`, 16 tests |
| R-5 | Compare `overmax_streak` against `COIN_OVERMAX_FAULT_MAX` (A-8) | **Done** |
| R-6 | Decode unmapped in-range pulses as ₱1 to profit, not discard (A-3) | **Done** — `COIN_UNKNOWN` |
| R-7 | Unrouted-coin reconciliation increments profit (A-4, §3.3) | **Done** — to `profit_unknown` |
| R-8 | `coin_hopper_plan()` / `can_cover()` per §3.4 (A-9) | **Done** — `HOPPER_RESERVE_P5 = 10` |
| R-9 | `billing_worst_case_change()` → full ₱20 ceiling (A-5) | **Done** |
| R-10 | Host toolchain; run `pio test -e native` | **Done** — 75/75 |
| R-11 | Extract plan arithmetic; unit test it | **Done** — `change_plan.*`, 19 tests |

Plus, from the rulings of 2026-08-30:

| # | Item | Status |
|---|---|---|
| R-12 | Flow-stall ordering: settle and pay before locking. New §9 invariant 8, `faults_latch()` / `faults_release_latched()` | **Done** |
| R-13 | Hopper outlet debounce 5 ms → 25 ms | **Done** |
| R-14 | Third chamber counter `profit_unknown` (§7.1 updated) | **Done** |
| R-15 | Remove `HOPPER_START_FLOAT`; adopt the other nine §B items | **Done** |
| R-16 | Spec constants adopted: bottle 80 ms, gallon float 500 ms, chamber 500 ms, confirm 50 ms, HMI 9600 baud | **Done** |
| R-17 | State enum renamed to §2.1 names; `SETTLING` added | **Done** |
| R-18 | §1.5 closed as SSR for pump and compressor; pulse-line shielding and RC filtering specified in `wiring.md` | **Done** |
| R-19 | Pins allocated: compressor D36, level LEDs D37–D39, confirm button D40 | **Done** |

---

## D — Decisions needed before the code that depends on them

Nothing here is engineering. Each one blocks work listed later.

Resolved 2026-08-30: D-1 (9600 baud), D-2 (PDF read), D-3 (25 ms), D-4 (settle
then lock), D-6 (third counter), D-7 (SSR), D-10 (physical button, pinned D40),
D-12 (spec and wiring redrafted). Still open:

| # | Item | Blocks | Category | Size |
|---|---|---|---|---|
| M-1 | **RTC or no RTC** — see §M below. The mockup's date/time fields cannot be built without one, and it is the only finding that adds a part to the BOM | M6 Statistics + Thank You, BOM | client + hardware decision | — |
| D-5 | **LOW CHANGE — transient or serviceable?** §6.1 calls it transient, but the counts cannot rise while the machine is locked and the acceptor inhibited, so only an Admin reload clears it. Flagged in a comment in `faults_update()` and deliberately not implemented | M5 fault logic | client decision | — |
| D-8 | **Profit chamber IR beam at the true fill line, or below it** for a service margin (§1.5) | M5 storage-full logic | hardware decision | — |
| D-9 | **Buzzer active or passive** (§1.5). All five §6.3 patterns are timing rather than pitch, so an active buzzer suffices unless distinct pitches are wanted. Recommend active | M5 buzzer | hardware decision | — |
| D-11 | **Hopper sourcing** (§10). If the candidate unit ships without a dispense-count output, an external IR sensor supplies it. The firmware interface does not change either way, but the wiring and BOM do | M7 case 11, BOM | client decision | — |
| M-2 | Confirm the "1 mL = ₱1.00" header is a typo for "100 mL" | M6 | client decision | 5 min |
| M-4 | Is a visible bottle-removal countdown wanted on the waiting screen? §5.5 requires one; the mockup does not draw it | M6 | client decision | — |

---

## M — Findings from the client mockup (`WATER-VENDO.pdf`, read 2026-08-30)

Eight screens across two pages: Welcome, Select Volume, Insert Bottle,
Dispensing, Thank You, System Status, Coin Inventory, Daily Statistics. A
four-tab nav bar — HOME / STATISTICS / STATUS / INVENTORY — appears on every
screen.

**Most of it matches the spec.** The volume grid is 20 options from 100 mL to
2000 mL in 100 mL steps, which is exactly what `billing_can_select()` already
enforces. Coin Inventory shows "TOTAL CHANGE AVAILABLE ₱285.00" against 115 ₱1
and 34 ₱5, which is the correct derived total and confirms the screen wants
`1·p1 + 5·p5` rather than a stored figure. The profit chamber does not appear on
any user-facing screen, so `profit_p10` / `profit_p20` / `profit_unknown` are
Admin-only as assumed. Dispensing shows selected volume, water dispensed,
balance, target and a percentage progress bar — all already available.

Four findings:

| # | Finding | Category | Size |
|---|---|---|---|
| M-1 | **The mockup requires a real-time clock, and there is no RTC in this machine.** Thank You shows "DATE & TIME · May 11, 2025 · 10:30 AM"; Daily Statistics shows "DATE: 05/30/2026". The Mega has no clock, `history_entry_t.timestamp` is documented as *seconds since boot-epoch*, and "daily" totals have no day boundary to reset on without one. **This cannot be built as drawn.** Three options: add a DS3231 (~₱150, one I²C pair, batteries last years); drop the date/time fields; or show elapsed-since-power-on and relabel. Recommend the DS3231 — a "daily" total that silently means "since last power cut" is worse than no total, and the operator reading the Statistics screen has no way to tell which they are looking at | **client + hardware decision** | 4 h to fit once decided |
| M-2 | Select Volume header reads **"1 mL = ₱1.00"**. The options beneath it read 100 mL = ₱1.00, 200 mL = ₱2.00 … 2000 mL = ₱20.00, so the rate is 100 mL per peso and the header is a typo. Confirm, then correct it in the `.HMI` project | client decision | 5 min |
| M-3 | System Status shows **"4.8 °C"** — one decimal. `water_temperature_c()` returned `int8_t` whole degrees and could not render it | code | **Done** — now `water_temperature_tenths_c()`, integer tenths, no float |
| M-4 | The user text says the pause screen is a "waiting text" that then resumes the dispensing display. That matches PAUSED → DISPENSING, but no bottle-removal countdown is drawn, while §5.5 requires a visible one. Confirm whether the countdown is wanted on that screen | client decision | — |

M-1 is the one to settle early: it is the only finding here that adds a part to
the bill of materials, and the Statistics and Thank You screens cannot be built
until it is answered.

## M3′ — Pin map and config completion

Small, but everything downstream references it.

| # | Item | Blocked by | Category | Size |
|---|---|---|---|---|
| 3′-1 | Allocate pins for the compressor relay, three level LEDs, and the confirm button; update `config.h` **and** `docs/wiring.md` in the same commit | D-7, D-10 | code | 1 h |
| 3′-2 | Add the missing debounce constants: gallon-bay float (500 ms, currently shares the 250 ms tank constant), chamber-full (500 ms), confirm button (50 ms) | D-3 | code | 30 min |
| 3′-3 | Reconcile `BOTTLE_DEBOUNCE_MS` 50 → 80 ms per §1.2 | — | code | 5 min |
| 3′-4 | Add cooling setpoints and hysteresis band, and compressor minimum off-time | D-7 | code | 30 min |
| 3′-5 | Add `SETTLING` to `state_t` and rename the state enum to match §2.1 | — | code | 30 min |
| 3′-6 | Add an overpay event tag to `event_tag_t` (§3.5 requires the log; no tag exists) | — | code | 15 min |
| 3′-7 | Decide `HOPPER_START_FLOAT`: wire it to an Admin shortcut or delete it | — | code | 15 min |

---

## M4 — Sensor and actuator modules — COMPLETE (2026-08-30)

| # | Item | Status |
|---|---|---|
| 4-1 | `flow.*` — mL accumulation with remainder carry (§5.1); guarded volatile read | **Done** |
| 4-2 | `flow.*` — stall detection armed only while the valve is open (§5.2), re-armed on the opening edge | **Done** |
| 4-3 | `bottle.*` — both edges debounced, one-shot flags consumed on read (§5.5) | **Done** |
| 4-4 | `water_level.*` — three floats debounced separately (§1.2) | **Done** |
| 4-5 | `water_level.*` — pump hysteresis; `PUMP_MIN_OFF_MS` and `PUMP_MAX_RUN_MS` inside `pump_write()` where §5.3 puts them, gallon interlock still first and unconditional | **Done** |
| 4-6 | `water_level.*` — DS18B20 request/collect, never blocking | **Done** |
| 4-7 | `water_level.*` — cooling thermostat, hysteresis, compressor minimum off-time and run ceiling (§5.4). Never gates a transaction | **Done** |
| 4-8 | Level LEDs from the debounced floats | **Done** — bar graph |
| 4-9 | `dispense.*` — pour control, cutoff, settle tail counted toward the user (§4.3) | **Done** |
| 4-10 | `dispense.*` — pause/resume toward the original target from volume already delivered | **Done** |
| 4-11 | `dispense.*` — abort reporting delivered mL for `billing_settle_partial()` | **Done** |
| 4-12 | DEBUG loop-timing check against `LOOP_WARN_US` | **Done** — rate-limited to 1/sec |

Note on 4-5 and 4-7: the pump raises `FAULT_PUMP_RUNTIME` through
`faults_latch()`, not `faults_raise()`, per §9 invariant 8. A pump overrun is a
tank problem rather than a till problem, and a user mid-transaction must still
be able to finish and collect their change. **M5 must call
`faults_release_latched()`** — after a payout, and when idle with nothing owed.
Until it does, a latched fault never locks the machine.

---

## M5 — State machine, faults, admin logic

The largest milestone. Nothing here can be meaningfully tested until M4 exists.

| # | Item | Blocked by | Category | Size |
|---|---|---|---|---|
| 5-1 | Boot reconciliation in `setup()`: unrouted-coin recovery, open-transaction resume, persistent-fault re-raise — the three `BOOT` transitions of §2.2 | R-3, R-7 | code | 4 h |
| 5-2 | The state machine itself: 13 states, every transition in §2.2 explicit, no implicit fallthrough | 3′-5, all of M4 | code | 2 days |
| 5-3 | Acceptor enable/disable driven by state — enabled in STANDBY and ACCEPTING only (§2.3). `coin_acceptor_uninhibit()` currently has no caller at all | 5-2 | code | 1 h |
| 5-4 | `can_cover()` guard wired ahead of the first coin of a transaction (§3.4) | R-8, R-9, 5-2 | code | 2 h |
| 5-5 | Payout sequencing across both hoppers — `coin_hopper_dispense()` handles one at a time and refuses a second while busy, so ₱1 and ₱5 legs must be run in sequence | R-8, 5-2 | code | 3 h |
| 5-6 | Overpay logging on payout (§3.5) | 3′-6, 5-5 | code | 1 h |
| 5-7 | `faults_update()` — transient fault re-evaluation for water, storage, low change | R-4, D-5, 4-4 | code | 3 h |
| 5-8 | `faults_clear()` — serviceable clear, requiring the physical cause gone where detectable (§8) | R-4, 5-7 | code | 2 h |
| 5-9 | Chamber-full detection on `PIN_CHAMBER_FULL`, currently never read (§6.1) | D-8, 3′-2 | code | 1 h |
| 5-10 | Raise LOW CHANGE on a virgin EEPROM (§7.3) | 5-7 | code | 30 min |
| 5-11 | Buzzer pattern generator, non-blocking, five patterns (§6.3) | D-9, 3′-1 | code | 3 h |
| 5-12 | Bottle-wait timeout ladder: 15 s / 18 s / 20 s cancel-and-refund (§6.3, scenarios case 7) | 5-11, 5-2 | code | 2 h |
| 5-13 | Confirm-button input and debounce, or its Nextion equivalent | D-10, 3′-1 | code | 1 h |
| 5-14 | Daily counters written per transaction; `persist_daily_add()` currently has no caller | 5-2 | code | 1 h |
| 5-15 | Open-transaction write policy: at end of transaction and each inventory change, never per loop (§7.2). `persist_txn_open()` has no caller | 5-2 | code | 2 h |
| 5-16 | Admin mode: all five functions of §8, each with its confirm step | 5-2, M6 | code | 2 days |
| 5-17 | **Case 18, sustained ₱5 drain** (§10). Report the arithmetic and propose a policy. Explicitly *do not* change the client's stated behaviour — this is analysis, then a recommendation | R-8 | client decision | 1 day analysis |

---

## M6 — Nextion HMI

**Entirely blocked on D-2 (mockup PDF) and D-1 (baud rate).** Building screens
before reading the document that governs their layout means building them twice.

| # | Item | Blocked by | Category | Size |
|---|---|---|---|---|
| 6-1 | Read the mockup PDF; produce a screen-by-screen inventory of components and IDs | D-2 | verification | 3 h |
| 6-2 | Confirm firmware baud against the panel | D-1 | hardware decision | 15 min |
| 6-3 | `hmi.*` transmit path: fixed `char[HMI_TX_BUFFER]`, three-`0xFF` terminator, integer-only amount formatting. No command is currently ever sent | 6-1, 6-2 | code | 4 h |
| 6-4 | `hmi.*` incremental RX parse and touch-event decode. `hmi_event_available()` returns false forever today, so the machine can receive no input | 6-3 | code | 4 h |
| 6-5 | Rate-limited refresh during a pour, `HMI_DISPENSE_REFRESH_MS`. Check the transmit-time budget if 9600 baud stands | 6-3, D-1 | code | 2 h |
| 6-6 | Build `hmi/watervendo.HMI` — the Nextion project file does not exist. Home, Statistics, System Status, Coin Inventory, Admin, plus the transaction screens and fault states | 6-1 | code | 3 days |
| 6-7 | Greyed-not-hidden volume options above available credit (§4.2) | 6-3, 6-6 | code | 2 h |
| 6-8 | Fault display with §6.2 priority, and the visible grace countdown of §5.5 | 6-3, R-4 | code | 2 h |
| 6-9 | Firmware/HMI version handshake so a mismatched pair is detected rather than misbehaving | 6-3, 6-6 | code | 2 h |

---

## M7 — Verification

`docs/scenarios.md` has a result log with 16 rows and **0 filled in**.

| # | Item | Blocked by | Category | Size |
|---|---|---|---|---|
| 7-1 | Get all 41 native tests green | R-10 | verification | — |
| 7-2 | Extend host-side tests to cover the hopper plan and the fault-priority mask | R-11, R-4 | code | 3 h |
| 7-3 | Bench test all 16 scenario cases and fill in the result log | M4, M5, M6, hardware | verification | 3 days |
| 7-4 | **Coin path with real coins — both series, including wet.** `docs/scenarios.md` states simulated pulses do not count as a pass for the coin path | 7-3, hardware | verification | 1 day |
| 7-5 | Power-cut testing at each of the six diverter sequence steps (§3.2), specifically between steps 3 and 6 | 7-3 | verification | 1 day |
| 7-6 | Hopper jam and undercount testing, deliberately induced | 7-3, D-11 | verification | 1 day |
| 7-7 | Soak test: loop-timing budget under simultaneous pump, hopper and compressor load, watching for D2 noise via `coin_acceptor_discarded_count()` | 7-3 | verification | 1 day |
| 7-8 | Confirm the shipped image is the `release` environment with no `-DDEBUG` | 7-3 | verification | 15 min |

---

## M8 — Per-unit calibration and mechanical setup

**None of this can begin until an assembled machine exists.** Every value is
per-unit; none transfers from one machine to the next.

| # | Item | Blocked by | Category | Size |
|---|---|---|---|---|
| 8-1 | Measure `FLOW_PULSES_PER_LITRE` with a graduated cylinder **at the actual dispensing flow rate**. The current 450 is the YF-S201 datasheet figure and is a placeholder. §5.1: the ratio shifts 5–8% between low and high flow, so calibrating by dumping water through fast produces a number wrong by more than the tolerance the design exists to control | hardware | physical calibration | 3 h |
| 8-2 | Measure the three `DIVERTER_ANGLE_*` values on the assembled chute. Current values are starting points; a few degrees off drops coins on the divider wall | hardware | physical calibration | 3 h |
| 8-3 | Teach the acceptor in learning mode and confirm the pulse counts match `COIN_PULSES_*`. If the acceptor is retaught in service, `config.h` changes in the same visit | hardware | physical calibration | 2 h |
| 8-4 | Set float positions physically; confirm mid/high hysteresis gives a sane pump duty cycle | hardware | physical calibration | 3 h |
| 8-5 | Position the profit-chamber IR beam per D-8 | D-8, hardware | physical calibration | 1 h |
| 8-6 | Cooling setpoints and hysteresis measured against the actual tank and compressor | 4-7, hardware | physical calibration | 4 h |
| 8-7 | Count the physical hopper float and enter it in Admin at commissioning | 5-16, hardware | physical calibration | 30 min |
| 8-8 | Confirm `COIN_LOCKOUT_MS` (900 ms) covers the actual servo's worst-case sweep plus settle on the assembled chute | 8-2, hardware | physical calibration | 1 h |
| 8-9 | Verify `HOPPER_TIMEOUT_MS` against the sourced hopper's real dispense rate | D-11, hardware | physical calibration | 1 h |
| 8-10 | Record every measured value in `docs/calibration.md` per unit, with the serial number | 8-1…8-9 | verification | 1 h |

---

## Rough totals

| Milestone | Engineering | Notes |
|---|---|---|
| R — remediation | ~1.5 days | In progress now |
| D — decisions | — | Not engineering; blocks M4, M5, M6 |
| M3′ — pin map | ~0.5 day | Partly blocked on D-7, D-10 |
| M4 — modules | ~4 days | Largely unblocked, can start after R |
| M5 — state machine | ~7 days | Blocked on M4 |
| M6 — HMI | ~5 days | **Fully blocked on D-2** |
| M7 — verification | ~8 days | Blocked on hardware |
| M8 — calibration | ~2.5 days | **Blocked on hardware existing** |

Roughly **four weeks of engineering**, of which about half cannot start until
either a decision lands or hardware is on the bench. The two hardest blockers
are the mockup PDF (all of M6) and the absence of a physical machine (all of M7
and M8).
