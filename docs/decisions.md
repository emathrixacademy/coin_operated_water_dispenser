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

Dimmed and non-responsive from `SELECTING` through `PAYING_CHANGE`.

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

## Still open

Nothing is currently blocked on a decision. The remaining blockers are physical:
no assembled hardware exists, so every per-unit calibration in `remaining.md` §M8
is unmeasurable.
