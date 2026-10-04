# Decisions — EMX-2026-WATERVENDO-01

The standing record of every ruling on this project.

**A decision made once must never need asking again.** If a question here comes
up in review, in a code comment, or in a future milestone, the answer is in this
file and the discussion is closed. Reopening one is a deliberate act, not
something that happens by forgetting.

Append new rulings as they land. Never delete one — if a decision is reversed,
strike it through and record the reversal underneath with its reasoning, so the
history of why the machine behaves as it does stays readable.

Authority: `CLAUDE.md` for coding constraints, `SPECIFICATION.md` for behaviour,
this file for settled questions. Where they conflict, raise it rather than
choosing.

---

## Hardware

### D-8 · Profit chamber IR beam position

**Sits BELOW the fill line, at roughly 80% of usable depth.**

At the true fill line, `COIN STORAGE FULL` means the chamber is *already* full:
the machine stops earning until someone drives out to it. A margin turns a hard
stop into a scheduled collection.

Also add a **"collection due" note on System Status**, driven off the same beam.
Free, since the beam is already read.

### D-9 · Buzzer type

**Active buzzer, not passive.**

All five §6.3 patterns are timing, not pitch. A passive buzzer needs `tone()`,
which claims an AVR timer, and Servo already holds Timer5. Trading a timer for
pitches nobody specified is a bad deal.

### D-11 · Hopper sourcing

**Proceed with the cheaper unit. A dispense-count output is a HARD ACCEPTANCE
CONDITION, not a preference.**

Buy **one**, run Case 11 against it, then decide on the pair. Budget the
external IR sensor and its bracket either way — retrofitting one into an
assembled cabinet is the expensive version.

The firmware must not assume a specific hopper's outlet signalling. An external
IR sensor presents identically to `coin_hopper.cpp` and the module interface
does not change.

### RTC · DS3231

**DS3231, not DS1307.** Temperature-compensated, battery-backed. Verify the
module is not a DS3231M knockoff.

An implausible readback is treated as **failure** — show clock-not-set rather
than stamping a wrong date.

### Pump and compressor switching · SSR

Both solid-state. The mechanical-contact-life argument is real but secondary:
the deciding reason is that contact arcing couples into the high-impedance pulse
input on D2 and reads as phantom coin pulses. A phantom coin is free water for
the user and inventory drift for the operator, and it only appears under load,
which makes it miserable to diagnose after the fact.

Pulse lines D2 and D3 are shielded, grounded at the **controller end only**,
with an RC filter and external pull-up at the controller. Values and reasoning
in `wiring.md`.

---

## Behaviour

### Flow-stall ordering

**`DISPENSING → SETTLING → PAYING_CHANGE → FAULT`. Settle first, lock second.**

Promoted to **§9 invariant 8**: a fault is never raised while money is owed to a
user who is still standing there.

Sole exception: **CHANGE JAM**, where the machine physically cannot pay, so
locking is the honest outcome and deferring it would only let the machine take
more money.

### LOW CHANGE · transient, checked once at the gate

Transient, not service-latched. It clears the moment an operator loads coins and
confirms in Admin, because the condition itself has gone.

Evaluated **before accepting the first coin of a transaction**, against worst
case for the credit ceiling. **Never re-evaluated mid-transaction** — invariant 8
from the other direction. Once coins are in, the machine finishes what it
started.

### PROFIT_UNKNOWN · third chamber counter

Folding unknown coins into either denomination corrupts the reconciliation the
`p10`/`p20` split exists for. Leaving them uncounted makes a physical collection
mismatch the record with no explanation. A third counter is honest and makes the
discrepancy legible to whoever opens the chamber.

### Unrecognised coins

An in-range pulse train matching no denomination is credited at the **minimum**
denomination and routed to the profit chamber, counted in `profit_unknown`.
Discarding it takes the user's coin and gives them nothing.

### M-4 · Grace countdown on the PAUSED screen

**Approved — show it.**

§5.5 requires it and it costs one text field. Without it the pause screen is
indistinguishable from a hang, and a user who does not know they have ten
seconds either walks away from a transaction they could have saved or stands
there pressing things.

---

## Constants

### Hopper outlet debounce · 25 ms, not 5 ms

At 10 coins/sec the real interval is 100 ms. Bounce counted as a coin means
change recorded but never paid — it steals from the user **and** corrupts
inventory in the same event, which is the worst pair of consequences available
in this machine.

### EEPROM wear · both hot regions are wear-levelled rings · WO-001-A

**Approved and implemented.** Layout version bumped 1 → 2.

#### The per-coin write is deliberate and is not to be optimised away

The open-transaction record is written **after every coin**. That is the
expensive choice and it is the right one: a power cut between the last coin and
the selection would otherwise take the user's money with **no record of it at
all**. On Philippine mains, in a school, that is not hypothetical — it is a
Tuesday.

Trading EEPROM lifetime for that guarantee is correct. The ring is what makes
the trade affordable. Anyone later tempted to write "only on material change"
should read scenarios.md Case 12 first and then not do it.

> **Superseded in part by WO-006 (2026-10-04), layout version 3.** The
> open-transaction ring is now **64 slots of 28 bytes** and the worst case is
> about 50 writes per transaction, not 24. The figures below are kept as the
> record of how version 2 was sized. Current arithmetic is under "Open-transaction
> record · layout version 3".

#### Sizing — worst case, not typical

Demand assumption **100 transactions/day**. Not paranoid: a school with cheap
cold water, no competition on site, demand concentrated at lunch. Size for the
day it works, not the average day.

AVR endurance is ~100,000 writes **per cell**.

**Open transaction — 32 slots.** Worst case 24 writes/transaction (open + 20
coins for ₱20 paid entirely in ₱1 + selection + settle + close):

| | writes/day | life |
|---|---|---|
| worst, 24 w/txn | 2,400 | 3,200,000 / 2,400 = **1,333 days ≈ 3.7 years** |
| realistic, 10 w/txn | 1,000 | 3,200 days ≈ 8.8 years |
| typical, 7 w/txn | 700 | 4,571 days ≈ 12.5 years |

**In-flight coin marker — 64 slots.** Found while implementing the above and
**far worse**: `coin_diverter_route()` marks and `coin_diverter_update()` clears,
so it is **two writes per coin** at what was a single address.

| | at one address (before) | 64 slots (now) |
|---|---|---|
| worst, 40 w/txn | 100,000/4,000 = **25 days** | 1,600 days ≈ 4.4 years |
| realistic, 12 w/txn | 83 days | 5,333 days ≈ 14.6 years |

Twice the slots of the transaction ring because it is written twice as often
and each slot is a quarter the size.

Both fixed under **one** layout version bump rather than two migrations.

**Daily counters** already adequate at 8 slots: one write per transaction,
8 × 100,000 / 100 per day ≈ 21 years.

#### Measured layout

`transaction_t` 24 B → 32 B/slot × 32 = 1024 B (`0x400`–`0x800`).
in-flight slot 12 B × 64 = 768 B (`0x800`–`0xB00`).
**2816 of 4096 bytes used, 1280 free.** Compiler-verified, not estimated.

#### Correcting the earlier figure

An earlier draft proposed 8 slots and reported "≈6 years at worst case". That
number was computed against the **typical** row at 50/day (6.26 years) and
mislabelled. The actual worst case for 8 slots is 333 days at 100/day. Sizing
against the row that does not matter is how a ring ships good for eleven months.

#### Attached requirements

- Ring wear is exposed read-only in Admin via `persist_txn_ring_writes()` /
  `persist_inflight_ring_writes()`. The **write count** is the honest figure,
  not the slot index — divide by (slots × 100,000) for fraction of life used.
- Slot advance and wrap are logged in the boot trace under `DEBUG`, so
  "did the ring wrap or did a CRC fail" is answerable without instrumenting a
  unit.

### HOPPER_START_FLOAT · removed

The change float is an operator action confirmed in Admin against a physical
count, not a firmware constant. A default here would only ever be wrong, and
wrong in the direction of claiming change the machine does not hold.

### Adopted from the spec

Bottle debounce 80 ms · gallon-bay float 500 ms (its own constant, harder than
the tank floats — it is a safety interlock and is refilled by hand) · chamber
beam 500 ms · confirm button 50 ms · **HMI 9600 baud**.

### `billing_worst_case_change()` · the full ₱20 ceiling

Not the ceiling less one sellable step. §2.2 reaches `PAYING_CHANGE` with full
credit and no pour by two paths — "finish without pour" and the bottle-wait
timeout — so a ₱20 refund is reachable, and a guard sized at ₱19 would let the
machine accept a transaction it cannot refund by exactly one peso.

---

## Documentation and process

### M-2 · "1 mL = ₱1.00" mockup header

**No action needed.** The error is header text in the `.HMI` project only, which
is M6 work and does not exist yet. Firmware stays `ML_PER_PESO = 100`. The
client will correct their paper.

### REPO · fast-forward main, no PR

Solo repo; review happens outside GitHub.

---

## User interface

### Back arrow · never abandons credit

Two behaviours, by screen:

**From INSERT BOTTLE (`AWAITING_BOTTLE`)** — cancels the selection and returns
to `SELECTING`. Nothing has dispensed, so credit is restored **in full with no
rounding applied**. New §2.2 row: `AWAITING_BOTTLE | back pressed | SELECTING`.

**From SELECT VOLUME (`SELECTING`)** — means "I'm done, give me my money."
Routes to `PAYING_CHANGE`. This is the existing §2.2 row "SELECTING | finish
without pour | PAYING_CHANGE"; the arrow is simply its trigger.

**Label the SELECT VOLUME arrow with text, not just a glyph.** An unlabelled
arrow that pays out change is a surprise. "Finish & get change" or equivalent.

### Nav bar during a transaction · dimmed, not hidden, not silently ignored

~~Dimmed and non-responsive from `SELECTING` through `PAYING_CHANGE`.~~

**Extended by R-3 (WO-003, 2026-10-04):** live in `STANDBY` only. It dims from
the first accepted coin, so through `ACCEPTING` as well. See R-3 below.

Silently ignoring a tap makes the user think the screen has frozen and press
harder. Dimming tells them it is deliberately unavailable. Hiding it makes the
layout jump between screens.

### Panel · Nextion Basic NX4832T035, 3.5", 480×320, resistive

Not the 4.3". That panel is 480×272 — *less* vertical room, and the 4×5 volume
grid plus header, footer and nav bar needs the height. 480×320 gives roughly
110×50 px per grid cell: tight but workable.

Basic tier, not Enhanced. Enhanced adds GPIO and an onboard RTC this design does
not use — the DS3231 sits on the Mega's I²C bus where the firmware controls it.

**Resistive touch is correct here and is not a compromise.** This machine lives
in a wet environment and users will have wet hands. Capacitive degrades badly
with water on the panel; resistive does not care.

### `.HMI` authoring · split between firmware and a person

The binary cannot be authored programmatically. The deliverable is the complete
firmware side, `docs/hmi_spec.md` specifying every page and object down to
position, font, colour and the exact serial command, and a transcription
checklist. **Scheduled as a task belonging to a person**, sized in
`remaining.md`, not left as a gap to surface at M6.

### Missing screens · all five approved for design

Four fault screens (each stating the condition in plain language and what to do
— a user facing LOW CHANGE needs to know the machine is not broken, it just
cannot make change) · PAUSED with the grace countdown and volume already
dispensed · Admin history and change edit · clock-not-set · **change collection
prompt**.

On the last: Thank You currently says "Please take your bottle" while ₱15 in
coins sits in the tray unmentioned. Add it, and make it **the more prominent of
the two**. A forgotten bottle is the user's problem; forgotten coins become
yours.

### Two mockup issues · built as drawn, recommendation recorded

- DISPENSING shows SELECTED VOLUME and TARGET as separate boxes holding the same
  number. Redundant.
- THANK YOU labels change as "BALANCE", but "balance" means unspent credit
  mid-transaction. Two meanings, one word. Recommend "CHANGE DISPENSED" on that
  screen only.

---

## Rulings of 4 October 2026 — WO-003, WO-005, WO-006

### R-1 · Volume options appear live as coins drop

The client's document settles it: *"lalabas yung lahat ng option depende sa
hinulog."* The grid is visible from the first coin and all twenty options are
drawn. Options at or below the available credit are live; options above it are
**dimmed but legible**, never hidden. A student who can see that ₱20 buys
2,000 mL puts in more money; hiding the unaffordable options removes the only
upsell the machine has.

### R-2 · CONFIRM is context-dependent, and it finishes a transaction

One physical button, meaning "I am done with this step":

| State | CONFIRM does |
|---|---|
| ACCEPTING | Closes coin entry. Commits the highlighted volume if there is one; otherwise moves to SELECTING with the acceptor inhibited |
| SELECTING | Commits the chosen volume, advances to AWAITING_BOTTLE |
| PAUSED | Ends the transaction early, settles the partial pour |
| COMPLETE | Finishes, routes to PAYING_CHANGE |

**The screen must label it.** The user never has to guess what CONFIRM will do.

### R-1 / R-2 · How the state machine carries them

Both states are kept. In ACCEPTING the grid is live and **a tap highlights a
volume without committing it**. CONFIRM, or reaching `MAX_TRANSACTION_PESOS`,
closes coin entry and commits the highlighted volume if there is one.

- CONFIRM in ACCEPTING with **nothing highlighted** moves to SELECTING with the
  acceptor inhibited. A button that appears dead teaches the user the machine
  is broken.
- **The highlight survives ACCEPTING → SELECTING.** A user who picked 500 mL and
  then fed another coin must not lose the choice.

§2.2 gains rows for CONFIRM in SELECTING, PAUSED and COMPLETE.

### R-3 · The nav bar is live in STANDBY only

It dims from the first accepted coin, through ACCEPTING and on to
PAYING_CHANGE. Once a user's money is in the machine, the machine's job is to
finish the transaction, not let them wander into Statistics and forget ₱20
inside. Dimmed, not hidden, not silently ignored.

### R-4 · LOW CHANGE is two things with two names

§6.1 and §6.1.1 only contradicted each other while they shared a name.

| Name | Condition | Effect |
|---|---|---|
| **LOW CHANGE WARNING** | ₱1 below 25 **or** ₱5 below 5 | Operator-facing, on System Status and Admin. **Machine keeps trading** |
| **LOW CHANGE LOCKOUT** | Cannot cover worst-case change for the ₱20 ceiling | Fault. Acceptor inhibited at the STANDBY → ACCEPTING gate |

Rename both in spec and code. A warning lets an operator schedule a visit; a
lockout gets one dispatched — the same reasoning as the coin-box beam at 80%.
The code already implements the lockout; only the warning is new.

### R-5 · Coin lockout is per gate movement, not per coin

A coin needs a lockout only if a gate must actually move. Consecutive coins to
the same destination wait for nothing.

- Track each gate's current position. Lock out only for the gates that must
  change, and only for that travel.
- `COIN_LOCKOUT_MS` default drops to **400 pending measurement**, and becomes a
  per-unit measured value recorded at calibration with the gate angles.
  Measured on the real flaps and stops, never taken from the servo datasheet.
- A coin returned because the acceptor was inhibited gets a brief **"one
  moment"** indicator. A returned coin with no explanation reads as a broken
  machine.

Never shorten the lockout below measured travel plus settle.

### R-6 · DEBUG-only bench mode, built before the hardware exists

Minimum: raw pulse count and gap per coin; commanded versus counted coins with
edge times per hopper attempt; a raw flow pulse counter; step-each-output for
the gates (servos have no position feedback); the specific cause of a clock
failure; blank EEPROM distinguished from a failed checksum.

### R-7 · Spec corrections to make in the Rev B pass

§1.3 and §3.2 three-position servo · §1.5 three items long since decided (D-8,
D-9, confirm button) · §1.2 and `wiring.md` coin-box beam at 80%, not the fill
line · §7.1 `dispensed`, superseded by the record below.

### P-2 · The saved transaction owns the credit

§3.3 treated the routing intent and the credit as one fact. They are two.

**The open-transaction record owns the credit. The routing intent owns only
where the coin physically went.** On boot the machine does **not** re-credit:
it resolves the routing by incrementing `profit_unknown` and writing the tagged
history event, and nothing else. (`accept_pending_coin()` writes the
transaction straight after marking the coin, so the restored credit already
includes it; crediting again doubles it.)

### Open-transaction record · layout version 3

Root cause of P-2, P-4 and P-5: the old record held how much credit existed but
not what had already been poured or paid.

| Field | Type | Meaning |
|---|---|---|
| `credit` | int32 centavos | Money the user still has in the machine |
| `inserted` | int32 centavos | For Thank You, history and daily profit |
| `target_ml` | uint16 | Committed for the current pour, 0 if none |
| `banked_ml` | uint16 | Poured in completed segments **of the current pour** |
| `segment_ml` | uint16 | Poured in the segment in progress, as last checkpointed |
| `total_ml` | uint16 | **Billed** volume of **earlier** pours |
| `phase` | uint8 | Resume code, below |
| `leg_hopper`, `leg_count` | uint8 × 2 | Payout leg in progress |

20 bytes of payload, **28 per slot**, layout version 2 → 3, **no migration**
(no units in the field).

`banked_ml` and `total_ml` are different numbers and stay apart: billing rounds
down **once per pour, on `banked_ml + segment_ml`**. Rounding per segment would
favour the machine once per pause, which is one time too many.

**Framing asymmetry.** A layout-version mismatch rejects every record, so the
inventory reads zero and the machine locks until the float is loaded (§7.3). A
single failed transaction slot falls back and does **not** touch the inventory —
per-record framing exists precisely so one bad record cannot invalidate the
others.

#### D-1 · Checkpoint every 100 mL

`segment_ml` is written each time `banked_ml + segment_ml` crosses a
`REFUND_ROUND_ML` boundary. Billing charges whole 100 mL steps, so a power cut
then costs the user nothing beyond normal rounding. Coarser takes money from
them; finer spends EEPROM life for nothing. The checkpoint write can delay the
valve cut-off by about 2 mL, well inside the accepted flow-sensor tolerance.

#### D-2 · Ring to 64 slots

About 50 writes per transaction worst case (20 coins + 20 checkpoints + open,
select, pauses, settle, four leg writes, close).

64 × 100,000 / (50 × 100 per day) = **1,280 days ≈ 3.5 years**. At 32 slots it
would be 1.75.

Transaction ring `0x400`–`0xB00` (1024–2816), in-flight ring `0xB00`–`0xE00`
(2816–3584), **512 bytes free**. Headroom for a field nobody has thought of yet
is worth more than another year of wear margin.

#### D-3 · Phase is a resume code, not the raw state

| Phase | Covers | On boot |
|---|---|---|
| NONE | no transaction | STANDBY |
| CREDIT | ACCEPTING, SELECTING, COMPLETE | COMPLETE with the credit |
| POUR | AWAITING_BOTTLE … SETTLING | settle from the checkpoint, refund the rest, COMPLETE |
| PAYING | PAYING_CHANGE | PAYING_CHANGE for what is still owed |

A stored raw state number breaks silently when the state list is reordered.

**The valve is never reopened on boot.** Now §9 invariant 9. A machine that
resumes pouring at power-up with no bottle present is P-1 wearing a different
hat.

#### D-4 · A cut mid-payout re-pays the whole leg

Leg start and leg end are written; coins within a leg are not (a ~90 ms write
while the outlet sensor is being polled risks a missed count, and a missed
count is a false jam). After a cut mid-leg, boot **deducts the whole commanded
leg from inventory and pays it again**. Understating inventory locks early
rather than promising change that is not there; overpaying by at most one leg,
in a roughly three-second window, is the machine guessing against itself.

A distinctly tagged history event (`EVT_LEG_REPAID`) records it, so an operator
whose physical count is off can see why.

#### D-5 · CHANGE JAM closes the transaction

Otherwise clearing a jam and rebooting offers the previous user's credit to
whoever is standing there — a theft route that appears every time the machine
is serviced. The unpaid amount is settled by the operator by hand.

**Unsettled jam amounts are listed on the Admin screen**, not only in history.

#### R-8 · A slot fallback that would resurrect an older state

If the slot after the newest readable one is **corrupt and not blank**, a newer
write was lost — torn by the power cut, or a worn cell — and the readable one is
an older state. If it says "open", the transaction is **treated as closed** and
a tagged history entry (`EVT_TXN_SLOT_LOST`) records the credit not resumed.

The asymmetry decides it: resuming offers money to a stranger, every time it
happens; closing a genuine open transaction costs one user once, and that user
is standing at the machine able to complain to an operator.

### R-9 · COMPLETE times out after 60 seconds

Then auto-finish and pay out. Sixty rather than twenty because the user is
handling a full bottle and a cap. A countdown shows for the last fifteen
seconds. `COMPLETE_TIMEOUT_MS = 60000`; shorten it and a user who paid loses
their change to the next person in line.

### Sequence · WO-005 §3

1 open-transaction record · 2 rulings, spec corrections, Rev B · 3 the seven
defects · 4 simulator rework plus `hmi_spec.md` (written from scratch) · 5 Part
C · 6 Part D and the 21 cases · 7 `hmi.cpp` · 8 bench mode. Stop for review
after each.

The `.HMI` transcription is the longest person-blocked item and sits on the
critical path, so what unblocks it moves first. **October is not achievable and
is not to be optimised for.** Build it right; the date is managed with the
client.

---

## Rulings of WO-007 and WO-008 — animation feasibility and the independent test

### Animations on the panel · four reworks approved

Anything drawn over a Nextion component is wiped when that component redraws.
So: **the surface ripple is dropped**; **the percentage moves beside the
bottle**; **the stream is baked into the empty-bottle image as four frames**;
**the landing hop on a change coin goes**. The bottle fill is a vertical
progress bar revealing a full-bottle image over an empty one, driven by the one
percentage the Mega already sends.

Budget: about 0.5 MB of animation, under 7 MB in total, on a 16 MB panel.
Nobody draws frames by hand; they are exported from the simulator.

**The Mega never sends a frame.** It sends a page, one percentage, and one
"coins paid" count. The panel's own timer does the rest.

**Unconfirmed panel facts are marked unconfirmed in `hmi_spec.md` itself:** that
a Basic panel cannot move a component at runtime, that pictures have no
transparency, and the timer minimum and per-page count.

### The ₱ glyph · blocks the money fields of `hmi_spec.md`

A person with Nextion Editor tests it. The fallback ladder, in priority order:

1. A UTF-8 font renders ₱. Nothing changes.
2. The generator will not emit ₱ but accepts a substituted glyph: draw the peso
   on an unused ASCII codepoint and have the Mega send that character.
3. A small ₱ image beside each amount. Costs a picture component per money
   field, against the 3,584-byte RAM budget.
4. Plain `P20.00`. Last resort.

`hmi_spec.md` gives the Mega-side string format for each rung.

### Scan sweep · runs while waiting for a bottle

There is no three-second checking window; the firmware pours 80 ms after the
sensor reads present. The sweep runs on INSERT BOTTLE as a "looking for your
bottle" cue, filling the interval that actually exists.

### Volume grid · to be evaluated, not yet ruled

Tile labels are static and can live in the page background image at no RAM
cost. Affordability is monotonic (tiles 1 to N live, the rest dimmed), so a few
crop or picture components may replace forty text fields. To be verified
against what a Basic panel offers, with the page RAM count reported.

### F8 · Every accepted coin is credited

**The machine never takes money it does not credit.** A coin that would take
credit past the ceiling was routed to the coin box and not credited: the
machine took money and recorded that it had not.

The acceptor identifies a coin only after it has arrived and Rev B has no
reject chute, so crediting is the only honest option. `MAX_TRANSACTION_PESOS`
**inhibits the acceptor at or above the ceiling but does not clamp credit.**
Credit can exceed ₱20 by at most one coin: the real maximum is **₱39**.

> **Open point raised with project management, 2026-10-04.** The work order
> gives the new worst-case change as ₱38 (₱39 less a ₱1 minimum purchase). The
> standing ruling on `billing_worst_case_change()` above is that the worst case
> is the **full** credit, because "finish without pour" and the bottle-wait
> timeout both refund everything. By that ruling the figure is **₱39**, and the
> current figure is ₱20, not ₱19. Not implemented until confirmed.

**Confirmed, WO-009: ₱39.** Maximum credit and maximum change are the same
number, because a user can cancel without pouring and is refunded everything.

Measured effect on the lockout gate: see `remaining.md`, "F8 lockout analysis".

### F9 · Pay what can be paid, then fault for the remainder

A user owed ₱19 who gets ₱15 and a stated ₱4 owed is far better served than one
who gets nothing. The fault text states the amount still owed.

**The fault is not CHANGE JAM.** The hopper is not jammed, it is short. A
distinct fault, with operator text that says *load coins*, not *clear the
hopper*: an operator sent to clear a hopper that is not jammed finds nothing and
concludes the machine lies.

> **Open point raised with project management, 2026-10-04.** In the reported
> case four ₱5 coins sat in the hopper and ₱0 was paid, because all four were
> inside the reserve of ten. Should a partial payout spend the ₱5 reserve? The
> machine is about to lock either way, so holding coins back protects nothing.
> Proposed: yes. Not implemented until confirmed.

**Confirmed, WO-009, with the precise rule:** if the amount owed can be paid in
full while respecting the ₱5 reserve, respect it. If it cannot, bypass the
reserve and pay as much as possible. The reserve protects future change
quality, which is a preference; a user at the machine owed money is a concrete
harm.

### Change drain · a client decision, not a code change (WO-009)

Every ₱10 and ₱20 goes to the coin box; change leaves only the ₱1 and ₱5
hoppers, which refill only from ₱1 and ₱5 inserts. Unless most users pay exact,
the hoppers empty: the mockup float lasts 30 to 60 sales under the non-exact
mixes simulated. **Not to be solved in firmware.** The analysis and three costed
remedies are in `docs/change-economics.md`, for the client to decide.

### Font and icons (WO-009)

The simulator font matches the client mockup, and `hmi_spec.md` specifies the
actual Nextion font generation: family, each size, character set. One icon set:
one stroke weight, one corner treatment, one optical size.

### F19 · An amount owed is never erased without a record

Written to the history ring **at the moment the fault is raised**. It survives a
power cut and an Admin clear. Unsettled amounts are listed on the Admin screen
(WO-006 D-5); the operator cannot settle by hand from a number that was deleted.

### F25 · ACCEPTING and SELECTING time out at 60 seconds

Same as COMPLETE (R-9): on expiry, finish and pay out, with a countdown for the
last fifteen seconds. AWAITING_BOTTLE keeps its 20 seconds and its buzzer
ladder.

### Sequence · WO-008 §5, replacing WO-005 §3

1 triage the independent test report and apply F8, F9, F19, F25 · 2 reworked
animations, remaining icons, scan sweep · 3 `hmi_spec.md`, money-field format
left open until the ₱ test · 4 the seven defects plus whatever triage adds · 5
Rev B and spec corrections · 6 Part C, Part D, `hmi.cpp`, bench mode.

Rev B moved down: the money defects outrank it and nothing is being fabricated
this week.

### Remedy 4 · the standby notice is not optional (WO-011)

If remedy 4 (narrowing the volume choice as change runs low) is ever built, the
**notice before the coin goes in is the remedy and the narrowing is secondary**.
A customer who refuses the narrowed choice takes a refund of the full coin
value in small coins, because their coin is already in the locked box. At 30%
refusal the benefit is gone. **Without the notice the remedy is worse than doing
nothing**, and it must never be dropped as a nice-to-have.

The narrowing rule needs no number: a volume stays available only if giving its
change would still leave the machine able to refund a full transaction.

### Two reasons a volume tile is dimmed (WO-011)

- **Unaffordable** tiles dim as today, with no extra text. The inserted-amount
  bar already explains it.
- **Change-blocked** tiles get a visually distinct treatment (a different dim or
  a small lock mark, within the icon set).
- **One line carries the reason for the whole group:** "Change is low. Larger
  sizes only." Never per tile.

### Fabrication · the cabinet is built to Rev B; the servos may be fitted later (WO-011)

The diverter cannot be retrofitted: it needs the chute geometry, drop angles,
housings inside the frame and service access. So both gate housings, both
chutes and the servo mounting points are fabricated now.

The machine may run initially **with the servos not fitted**. The firmware keeps
the diverter code behind `COIN_ROUTING_MODE`: `RECIRCULATE` as specified, or
`COLLECT_ALL` with no gate movement, no routing intent and no gate lockout.
**Diverter code is not to be deleted.**

> **Reported back before building, 2026-10-04, awaiting a ruling.** With no
> servos every coin of every denomination lands in ONE hopper, mixed. A hopper
> holding mixed coins cannot pay change: it pays by count, not by value. So in
> COLLECT_ALL one hopper is a collection bin whose motor never runs, and all
> change comes from the other, loaded by the operator and never refilled by
> customers. Which hopper Gate B rests toward, and that both flaps are
> mechanically pinned, must be decided on the drawing.

---

## Still open

~~**F8:** worst-case change ₱38 or ₱39. **F9:** whether a partial payout spends
the ₱5 reserve.~~ Both confirmed in WO-009, above.

**With the client:** which remedy for the change drain, `change-economics.md`.

**COLLECT_ALL (WO-011):** how change is given when every coin lands mixed in one
hopper; which side Gate B is pinned to. Reported 2026-10-04.

~~Nothing is currently blocked on a decision.~~ The remaining blockers are physical:
no assembled hardware exists, so every per-unit calibration in `remaining.md` §M8
is unmeasurable.
