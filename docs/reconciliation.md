# Reconciliation — existing code against docs/SPECIFICATION.md

Project EMX-2026-WATERVENDO-01. Audited 2026-08-30 at commit `965b0f0` plus
uncommitted working-tree work.

Scope: every line of `src/`, `include/`, `platformio.ini` and `test/` compared
against the spec. Nothing was changed while producing this report.

Four categories, as requested. Item IDs are stable so they can be referenced in
review.

---

## A. Code that CONTRADICTS the spec — defects

### A-1 · HMI baud rate is wrong by an order of magnitude

Spec §1.4: `Nextion HMI | Serial2, D16/D17, 9600 baud`.

`config.h:159` — `#define HMI_BAUD 115200`.

The Nextion will not respond at all if the firmware and the display panel
disagree. One of the two documents is wrong; the display's own configuration
decides which. **Flag for decision, not a silent edit** — 9600 baud is slow for
the 250 ms progress refresh in `HMI_DISPENSE_REFRESH_MS`, and if the panel is
already flashed at 115200 the spec should be corrected instead.

### A-2 · Four debounce constants disagree with §1.2

| Signal | Spec §1.2 | `config.h` | Status |
|---|---|---|---|
| Hopper outlet count | 5 ms | `HOPPER_COUNT_DEBOUNCE_MS 15` | Contradiction |
| Float, cold tank mid/high | 250 ms | `FLOAT_DEBOUNCE_MS 250` | Agrees |
| Float, gallon bay | 500 ms | `FLOAT_DEBOUNCE_MS 250` | Contradiction — no separate constant exists |
| Bottle proximity | 80 ms | `BOTTLE_DEBOUNCE_MS 50` | Contradiction |
| Profit chamber full | 500 ms | *no constant* | Missing |
| Confirm button | 50 ms | *no constant, no pin* | Missing |

See also **D-1** — the 5 ms figure looks like a spec regression, not a code
defect.

### A-3 · Unmapped in-range pulse counts are discarded, not credited

Spec §3.1 pseudocode:

> `coin = decode(n)          // unmapped n → treat as ₱1 value, route to profit`
> An unmapped-but-in-range count credits the user the minimum and routes to profit.

`coin_acceptor.cpp:107-114` does the opposite — `resolve()` returns
`COIN_INVALID` for any count that is not 1, 2, 3 or 4, and `update()` increments
`s_discarded` and returns, crediting nothing. A 5-, 6-, 7- or 8-pulse train
currently takes the user's coin and gives them nothing.

This is a real money defect against the spec and against §0's "in the user's
favour on credit" rule.

### A-4 · Power-loss reconciliation does not increment the profit counter

Spec §3.3:

> credit the user the coin's value
> **increment the PROFIT counter, not the hopper counter**

`persist.cpp:294-297` deliberately does neither, with the comment:

> *"the profit chamber count is not incremented either. The coin's physical
> location is genuinely unknown, and inventing a location in the profit count
> would be a different flavour of the same lie."*

The spec has now decided this. The code's reasoning is defensible but it is no
longer the client's decision, so the code is the defect. **Fixing per spec.**

### A-5 · `billing_worst_case_change()` understates by ₱1 — money-safety defect

`billing.cpp:141-147` returns `MAX_TRANSACTION_CENTAVOS − price_of(100 mL)` =
**₱19**, on the reasoning that the user must buy at least one 100 mL step.

The spec's own transition table makes a full ₱20 refund reachable without any
pour at all — §2.2:

> `SELECTING | finish without pour | PAYING_CHANGE`
> `AWAITING_BOTTLE | BOTTLE_WAIT_CANCEL_MS elapsed | PAYING_CHANGE (full credit)`

Spec §3.4 says `can_cover` is checked *"against the worst case for the credit
ceiling"*. The credit ceiling is ₱20. The guard therefore lets the machine accept
a transaction it cannot refund by exactly one peso, which is the precise failure
`can_cover` exists to prevent. **Fixing to ₱20.** Note this makes the machine
lock on LOW CHANGE slightly more often, which is the correct direction.

`test/test_billing/test_billing.cpp:216-219` asserts the old ₱19 value and must
be updated with the fix.

### A-6 · `profit_p20` is never written *(known defect — spec confirms)*

`inventory_t::profit_p20` (`types.h:111`) is declared and **never read or written
anywhere in the firmware**. `persist_inventory_add()` and
`persist_inventory_set()` both map `DEST_PROFIT` to `profit_p10`, so ₱10 and ₱20
accumulate into one counter.

Spec §7.1 removes any doubt:

> `profit_p10` and `profit_p20` are separate counters. Without the split the
> chamber's peso value cannot be derived from its count, and reconciling a
> physical collection against the recorded total becomes impossible.

**Status: confirmed defect, fix mandated by spec. Fixing.** Requires splitting
`coin_dest_t`, which is why it was not a one-line change.

### A-7 · Persistent faults are not persisted *(known defect — spec confirms)*

`faults_is_persistent()` classifies four faults as surviving a reboot. There is
**no EEPROM region for faults** in `config.h`, and nothing reads or writes one.
Power-cycling clears every fault.

Spec §6.1 and §7.1 both require it, and §6.1 states the consequence directly:

> A persistent fault that clears on power cycle is worse than not claiming
> persistence at all: the operator learns that the fix is a reboot, the coins
> stay jammed, and the machine returns to accepting money it cannot pay out.

**Status: confirmed defect, fix mandated. Fixing.**

### A-8 · `COIN_OVERMAX_FAULT_MAX` is never compared *(known defect — spec confirms)*

`config.h:201` defines it as 5, `coin_acceptor_overmax_streak()` exposes the
counter, and **nothing anywhere compares the two**. A stuck acceptor output line
swallows coins silently and forever.

Spec §3.1 gives the exact pseudocode:

> `if overmax_streak >= COIN_OVERMAX_FAULT_MAX: faults_raise(FAULT_ACCEPTOR)`

**Status: confirmed defect, fix mandated. Fixing.**

### A-9 · `coin_hopper_plan()` returns FAIL unconditionally *(known blocker — spec resolves)*

`coin_hopper.cpp:225-237` is a stub returning `false`, with a comment reserving
the strategy for client review. Because `can_cover()` is defined purely in terms
of `plan()`, it also always returns false, so **no change can ever be planned or
paid**.

Spec §3.4 now resolves it: largest-coin-first with `HOPPER_RESERVE_P5 = 10`.

**Status: unblocked. Implementing §3.4 verbatim.** Two notes:

- §3.4's `plan(amount_pesos, …)` takes whole pesos; the existing signature takes
  `money_t` centavos. Keeping the centavos signature per `config.h`'s standing
  instruction not to collapse to whole pesos, converting internally, and
  returning FAIL on any non-whole-peso amount (no sub-peso coin exists to pay
  it). Raising rather than assuming — see **D-5**.
- `HOPPER_RESERVE_P5` does not exist in `config.h` and is being added.

### A-10 · State enum does not match §2.1

`types.h:64-77` defines 12 states; the spec defines 13. Differences:

| Spec §2.1 | `types.h` |
|---|---|
| `SELECTING` | `STATE_SELECT_VOLUME` |
| `AWAITING_BOTTLE` | `STATE_WAIT_BOTTLE` |
| `PAUSED` | `STATE_BOTTLE_REMOVED` |
| **`SETTLING`** | **absent** |
| `COMPLETE` | `STATE_POUR_COMPLETE` |
| `FAULT` | `STATE_LOCKED` |

The naming differences are cosmetic. The missing `SETTLING` state is not — the
spec makes it a distinct state with its own transitions (`DISPENSING → SETTLING`
on target reached, `PAUSED → SETTLING` on grace expiry, `SETTLING → COMPLETE` on
tail window). This is M5 work but the enum is wrong today.

### A-11 · Overpay on payout is not handled

Spec §3.5:

> `if count > n:` ← overpay, do not "correct" it
> `log, adjust inventory by actual, continue`

`coin_hopper.cpp` adjusts inventory by actual (correct) but **writes no log
entry**. There is no `EVT_` tag for an overpay in `event_tag_t` either.

---

## B. Code that EXCEEDS the spec — adopt or remove

None of these are wrong. Each is a decision the client has not made.

| ID | What was built | Recommendation |
|---|---|---|
| B-1 | **Per-record CRC8 framing** (`eeprom_record.*`). Spec §7.1 says "One checksum covers the whole state block; the history ring is checksummed separately" — i.e. two checksums. The code gives every region its own magic, layout version and CRC8. | **Adopt.** Strictly stronger: a corrupt open-transaction record cannot invalidate inventory. Spec §7.1 should be amended to describe per-record framing. |
| B-2 | **`EEPROM_LAYOUT_VERSION` check** inside `record_unpack()`. Spec mentions a "schema version" in the header region but does not require it to gate reads. | **Adopt.** Stops a firmware upgrade misreading an old record shape. |
| B-3 | **`HOPPER_CAPACITY` clamp** in `persist_inventory_add()`. Spec does not mention clamping. | **Adopt.** Stops an underflow re-creating the 65535-coin bug from the arithmetic side. But see **A-12** below. |
| B-4 | **`persist_was_initialised()`** flag surfaced for Admin. | **Adopt.** §7.3 requires the machine to react to a fresh EEPROM; a technician needs to know it was reset rather than restored. |
| B-5 | **`billing_settle_complete()` credits `target_ml`, not the measured volume**, to the daily totals. Spec §4.5 is silent. | **Adopt.** Without it the daily volume total disagrees with the daily profit total by the flow-sensor tolerance on every single transaction. |
| B-6 | **`s_txn.inserted`** tracked separately from credit, for the summary screen. Spec §7.1's open-transaction region lists "Credit, target, dispensed, state" only. | **Adopt.** Needed for the Thank You summary; costs 4 bytes. |
| B-7 | **`coin_acceptor_discarded_count()`** diagnostic counter. Not in spec. | **Adopt** as a System Status field, or remove. Cheap and useful for chasing D2 noise. |
| B-8 | **`billing_at_ceiling()`**, **`LOOP_WARN_US`**, **two build environments (debug/release)**, **`-Wall -Wextra`**. | **Adopt.** All serve CLAUDE.md constraints. |
| B-9 | **`EVT_ADMIN_EDIT` before/after logging** in `persist_inventory_set()`. Spec §8 requires a confirm and an immediate commit but not a log entry. | **Adopt.** It is the one write path that can desync EEPROM from physical contents. |
| A-12 | **`HOPPER_CAPACITY` (500) is applied to the profit counters too.** The profit chamber is not a hopper and holds far more than 500 coins; the count would silently stop rising. | **Defect, arising from B-3.** Adding a separate profit-chamber ceiling. |
| B-10 | **`HOPPER_START_FLOAT` (100)** defined, never used. | **Remove**, or wire it to an Admin "load standard float" shortcut. Commissioning counts are entered by hand per §8. |

---

## C. Spec describes it, code does not have it — not yet built

Enumerated, not fixed. Full sizing is in `docs/remaining.md`.

**Hardware absent from the pin map entirely** (`config.h` has no pin for any of these):

| C-1 | Compressor relay | §1.3, §5.4 |
| C-2 | Level LEDs ×3 (low, mid, high) | §1.3 |
| C-3 | Confirm button | §1.4, and the `ACCEPTING → SELECTING` transition in §2.2 depends on it |

**Declared in the pin map but never read or written by any code:**

| C-4 | `PIN_CHAMBER_FULL` (D29) — `STORAGE FULL` can never raise | §6.1 |
| C-5 | `PIN_BUZZER` (D35) — no buzzer stage exists | §6.3 |
| C-6 | `PIN_TEMP_ONEWIRE` (D23) — OneWire/DallasTemperature are in `platformio.ini` but **no source file includes them** | §5.4 |

**Logic not built:**

| C-7 | The entire state machine. `main.cpp:82-98` is an empty `switch`; every case breaks. No transition in §2.2 exists. Consequence: `billing_add_coin()`, `coin_diverter_route()`, `coin_hopper_dispense()`, `faults_raise()`, `persist_txn_open()`, `persist_daily_add()` and `coin_acceptor_uninhibit()` **have no callers anywhere**. The release build boots with the acceptor inhibited and never re-enables it. | §2 |
| C-8 | Boot reconciliation in `setup()` — the three `BOOT` transitions of §2.2 | §2.2, §3.3 |
| C-9 | Flow mL accumulation with remainder carry; `flow_ml()` returns 0 | §5.1 |
| C-10 | Flow stall detection; `flow_is_stalled()` returns false | §5.2 |
| C-11 | Bottle debounce and edges; all three queries return false | §5.5 |
| C-12 | Float debounce, pump hysteresis, `PUMP_MIN_OFF_MS`, `PUMP_MAX_RUN_MS`. Note §5.3 puts min-off and max-run **inside `pump_write()`**; the code's `pump_write()` currently contains only the gallon interlock | §5.3 |
| C-13 | Cooling thermostat, hysteresis, compressor minimum off-time | §5.4 |
| C-14 | Dispense pour control, cutoff, pause/resume, settle tail | §4.3, §5.5 |
| C-15 | Fault priority ordering (§6.2). `faults_raise()` is last-write-wins, so LOW CHANGE can mask CHANGE JAM | §6.2 |
| C-16 | `faults_update()` and `faults_clear()` are both empty — no transient auto-clear, and a serviceable fault cannot be cleared at all | §6.1 |
| C-17 | Buzzer stages — all five patterns | §6.3 |
| C-18 | §7.3 "raise LOW CHANGE on a virgin EEPROM". `persist_begin()` zeroes and flags, but nothing raises the fault | §7.3 |
| C-19 | Entire Admin mode — all five functions | §8 |
| C-20 | Entire HMI transmit and receive path. `hmi_setPage()` records a variable and sends nothing; `hmi_event_available()` returns false forever, so even a finished state machine would receive no input. The `hmi/watervendo.HMI` project file does not exist | §2, §8 |

---

## D. Spec is ambiguous or self-contradictory — decide before coding

### D-1 · Hopper outlet debounce: 5 ms contradicts the spec's own coin rate

Spec §1.2 sets hopper outlet debounce at **5 ms**. Spec §1.1 says:

> Hopper outlet coins leave at 5–10/sec, well inside polling range.

At 5–10 coins/sec, consecutive coins are 100–200 ms apart. A 5 ms window admits
relay and contact bounce that a 15 ms window rejects, and §1.1's own reasoning
supports the longer figure. Counting bounce as coins makes the machine believe
it paid change it did not pay — a direct §0 violation.

**Recommend keeping 15 ms and correcting §1.2.** Not changing it either way
without a decision.

### D-2 · Flow stall: pay change first, or lock first?

Spec §5.2 gives a sequence that pays the user out **before** raising the fault:

> close valve / bill the volume actually dispensed, rounded down / refund the
> remainder to credit / **pay change** / `faults_raise(FAULT_FLOW_STALL)`

Spec §2.2 gives a transition that goes straight to the locked state:

> `DISPENSING | no pulses for FLOW_STALL_TIMEOUT_MS | FAULT (flow stall)`

These cannot both be right. §2.3 says the acceptor is inhibited in FAULT, and
`faults_raise()` inhibits before returning — so entering FAULT first would strand
the user's change behind a lockout. §6's own preamble says a transaction in
progress is allowed to finish and pay out where it physically can.

**Recommend §5.2's ordering** (settle and pay, then lock) and adding an
intermediate transition `DISPENSING → SETTLING → PAYING_CHANGE → FAULT`. Needs a
decision.

### D-3 · LOW CHANGE is classed transient but cannot self-clear

Spec §6.1 lists LOW CHANGE under **Transient — auto-clear when the condition
clears**. But the ₱1 and ₱5 counts only rise when coins enter the hoppers, and
coins only enter while the acceptor is enabled — which it is not, because the
machine is locked. The only escape is an Admin reload, which is the definition of
a serviceable fault. CLAUDE.md's own table says "admin reload".

**Recommend reclassifying LOW CHANGE as serviceable** (auto-clears only after an
Admin inventory edit raises the counts), or stating explicitly that the
re-evaluation runs against the post-Admin counts.

### D-4 · Which profit counter takes an unrecognised coin?

Spec §3.1 says an unmapped in-range count is credited as ₱1 and routed to profit.
Spec §7.1 requires `profit_p10` and `profit_p20` to be separate so the chamber's
peso value can be derived. An unrecognised coin is neither, and putting it in
either counter corrupts exactly the reconciliation the split exists for.

**Interim implementation:** route it to the profit chamber physically, increment
**neither** counter, per §0 ("the machine never over-claims what it holds") and
§9's understate rule. Flagged here because the spec is silent, not because the
code is guessing quietly. A third counter, `profit_unknown`, would be the honest
alternative and is cheap — needs a decision.

### D-5 · `plan()` takes pesos; the money path is centavos

Spec §3.4's signature is `plan(amount_pesos, p1_count, p5_count)`. Every other
money value in the spec and the code is centavos, and `config.h` carries an
explicit instruction not to collapse to whole pesos because it would make a
future ₱0.50 price point a refactor rather than a constant change.

**Implementing as `plan(centavos, …)`** converting internally and returning FAIL
on a non-whole-peso amount. Raising rather than silently reinterpreting the spec.

### D-6 · §3.2 step 7 "uninhibit acceptor" would clobber a fault lockout

Spec §3.2's fixed sequence ends:

> `7. uninhibit acceptor`

Read literally, a coin that finishes routing during a fault raised mid-travel
would re-enable the acceptor inside a locked machine. The existing code already
prevents this correctly — `coin_acceptor` tracks a window inhibit and a fault
inhibit as two independent flags and asserts the pin if **either** is set, so
`coin_acceptor_window_release()` cannot clear a fault lockout.

**Recommend §3.2 step 7 be reworded** to "release the diverter window inhibit"
so no future reader implements the literal version. No code change needed.

### D-7 · §7.1 stores transaction `state`; §2.2 ignores it

Spec §7.1's open-transaction region holds *"Credit, target, dispensed, **state**"*.
Spec §2.2 resumes an open transaction to `COMPLETE` unconditionally, regardless
of which state it was in. Either the stored state is dead weight, or §2.2 is
meant to be state-dependent. `transaction_t` currently does not store state.

**Recommend dropping `state` from §7.1** — resuming to COMPLETE with the
remaining credit shown is the simpler and safer behaviour, and mid-pour state
cannot be safely resumed anyway.

### D-8 · §1.5 buzzer question may already be answered by §6.3

§1.5 flags active-vs-passive buzzer as open, on the grounds that *"an active
buzzer … cannot make distinct tones for the two warning stages."* But every
pattern in §6.3 — single short, double short, long, triple short — is
**timing**, not pitch. An active buzzer driven by a non-blocking `millis()`
pattern generator produces all five.

**Recommend closing §1.5's buzzer item in favour of an active buzzer**, unless
the client wants distinct pitches. A passive buzzer also needs a PWM/tone pin,
and `tone()` on AVR claims a timer.

### D-9 · 9600 baud against a 250 ms progress refresh

If §1.4's 9600 baud stands, note that `HMI_DISPENSE_REFRESH_MS` is 250 ms and a
refresh sends several `setText`/`setValue` commands. At 9600 baud that is roughly
1 ms per character; a 60-character refresh burst is ~62 ms of transmit time every
250 ms. Workable, but there is no headroom for adding fields. Related to **A-1**.

---

## E. Status of the four known defects, against the spec

| Defect | Spec position | Status |
|---|---|---|
| `profit_p20` never written | §7.1 **requires** the split, with the reason stated | Confirmed defect. **Fixing now.** |
| Fault state not persisted | §6.1 **requires** it; §7.1 allocates a region for it | Confirmed defect. **Fixing now.** |
| `COIN_OVERMAX_FAULT_MAX` never compared | §3.1 gives the exact pseudocode | Confirmed defect. **Fixing now.** |
| `coin_hopper_plan()` blocked | §3.4 **resolves it** — largest-coin-first, `HOPPER_RESERVE_P5 = 10` | Unblocked. **Implementing now.** |

All four are confirmed by the spec rather than overturned by it. One additional
money-safety defect (**A-5**, worst-case change understated by ₱1) was found
while reconciling and is being fixed in the same pass.

---

## F. What could not be verified on this machine

- **The 41 native tests have never been run here.** `pio test -e native` fails
  before compiling: no `gcc`/`g++` on the host. A compiler install is in
  progress; results will be reported separately. Until then, every claim about
  the billing arithmetic and the EEPROM framing in this document rests on
  reading the code, not on executing it.
- **The client's mockup PDF has not been seen.** `poppler` is not installed, so
  the mockup images cannot be rendered. Spec §0 states the PDF wins on screen
  layout — that means **no HMI screen layout in this project has been verified
  against the authority that governs it**, and none should be built until it has.
- **No hardware exists to test against.** Every per-unit calibration (flow ratio,
  servo angles, thermostat setpoints) is unmeasured and unmeasurable today.
- `pio run -e release` **does** build clean: RAM 455 B / 8192 (5.6%), Flash
  6950 B / 253952 (2.7%). The low flash figure reflects how much is still stubs.
