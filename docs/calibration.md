# Calibration

Project EMX-2026-WATERVENDO-01, eMathrix Technologies.

Two things are calibrated per unit: the flow sensor and the coin acceptor. Neither can
be copied from another machine and neither can be taken from a datasheet. Both must be
redone if the sensor, the acceptor, or the plumbing is replaced in service.

---

## Flow calibration

### Why this is per-unit

Flow sensors of this class carry 2–5 % tolerance, and the effective figure shifts with
the plumbing it sits in — line length, back pressure, and the head between the cold
tank and the valve all move it. The datasheet number is a starting point for sizing the
part, not a value to ship.

This is also why the billing rule is coins-first. The flow sensor is a cutoff, not a
cashier. Calibrating it well makes the pour accurate; it does not make it safe to bill
from. Nothing in the firmware may compute an amount owed from a measured volume no
matter how good this calibration is.

### Calibrate at the actual dispensing flow rate

**This is the step most likely to be got wrong, and getting it wrong produces a number
that is off by more than the tolerance the calibration exists to control.**

The sensor's pulses-per-litre is not a constant. On the YF-S201 class of sensor the
ratio shifts by roughly **5–8 % between low and high flow**. A calibration run done by
opening a tap wide and dumping water through the sensor fast measures the sensor at a
flow rate the machine will never actually use, and yields a figure that is wrong in
service by more than the 2–5 % tolerance being corrected for.

This machine is a favourable case: one flow rate, through one valve, on a
gravity-and-pump-fed line. A single figure is genuinely valid here — but only if it was
measured at that rate.

So: **calibrate through the machine's own solenoid valve, under the machine's own head,
with the pump in its normal state.** Do not calibrate on the bench with a hose. Do not
open a bypass. If the plumbing changes in service — a different valve, a re-routed line,
a change in tank height — the calibration is void and must be redone.

### What you are measuring

`ML_PER_PULSE` in `include/config.h` — millilitres of water per flow sensor pulse,
as installed in this machine.

Volume is integer millilitres throughout the firmware and money is integer centavos.
There is no floating point anywhere in the money or volume path. If the measured
pulses-per-litre does not divide cleanly, carry the residual as an integer remainder
rather than reaching for a `float`.

### Procedure

You need a 2000 mL graduated cylinder, a debug build, and a full cold tank.

1. Flash the **debug** build. Pulse counts are reported over Serial at 115200 baud.
   The release build does not print this.
2. Fill the cold tank above the mid float and let the pump settle. Calibrating on a
   nearly empty tank gives a different figure than the machine will see in service.
3. Purge the line. Run at least 500 mL through the valve and discard it, so you are
   measuring water and not air.
4. Place the graduated cylinder under the outlet.
5. Command a pour of a known large pulse count — use the calibration routine in the
   debug build rather than a timed valve opening. A large count reduces the effect of
   the one-pulse quantisation error at the start and end of the pour.

   The routine drives the machine's own valve at the normal dispensing rate. That is
   the point of using it rather than a hose — see the flow rate warning above.
6. Read the cylinder at eye level, at the bottom of the meniscus.
7. `ML_PER_PULSE = millilitres measured / pulses counted`.
8. **Repeat five times.** Discard any run that differs from the others by more than
   2 %, and investigate rather than averaging it away — a wandering figure usually
   means air in the line or an unstable head, not a bad sensor.
9. Set the value in `config.h` and rebuild.

### Verification

Do not accept the calibration on the strength of the calibration run itself.

1. Run a real ₱20 / 2000 mL transaction per Case 1 in `scenarios.md`, catching the pour
   in the cylinder.
2. Expect 2000 mL within ±5 %.
3. Repeat at a small volume — ₱1 / 100 mL. Small pours expose start-of-pour
   quantisation that a 2000 mL run hides. If 100 mL is out of tolerance while 2000 mL
   is fine, the valve open/close transient is the problem, not `ML_PER_PULSE`.
4. Repeat with the tank at a low level and again just after the pump has filled it. A
   figure that moves with the head means the machine will drift in service.

If a pour cannot be brought inside tolerance, fix the plumbing or the sensor. Do not
adjust the billing rate to compensate — that inverts the design and starts the change
drift the whole architecture exists to prevent.

### Record for this unit

| Run | Pulses | Measured mL | mL/pulse |
|---|---|---|---|
| 1 | | | |
| 2 | | | |
| 3 | | | |
| 4 | | | |
| 5 | | | |

Measured through the machine's own valve at the normal dispensing rate: ☐ confirmed

`FLOW_PULSES_PER_LITRE` committed to `config.h`: ______________
Calibrated by: ______________ Date: ______________

---

## Coin acceptor calibration

### Why both coin series matter

The machine is in service in the Philippines and both the old and the current series of
each denomination are in circulation. A user with an old ₱5 will not accept being told
it is not money. Sampling only the coins in your pocket on the day is the most common
reason a machine rejects valid coins in the field.

The ₱20 needs more samples than the rest. It is thinner and lighter than the other
denominations, which leaves the acceptor less signal to work with and makes it the
denomination most likely to be misread or rejected.

### Procedure

Calibration is done on the acceptor itself in its learning mode, not in firmware. The
acceptor identifies denominations by pulse count and the Mega only reads that output.

1. Put the acceptor into learning mode per its manual — usually a dip switch and a
   button on the unit.
2. Set the pulse count for each denomination:

   | Coin | Pulses |
   |---|---|
   | ₱1 | 1 |
   | ₱5 | 2 |
   | ₱10 | 3 |
   | ₱20 | 4 |

   These must match `config.h`. If you change the acceptor's pulse assignment, change
   `config.h` in the same service visit or the machine will credit the wrong amount.
3. Sample each denomination. Minimum counts:

   | Coin | Old series | Current series | Total minimum |
   |---|---|---|---|
   | ₱1 | 15 | 15 | 30 |
   | ₱5 | 15 | 15 | 30 |
   | ₱10 | 15 | 15 | 30 |
   | ₱20 | 25 | 25 | 50 |

4. Use varied coins, not the same coin repeatedly. Worn, dirty and new coins all
   present differently. Include a few visibly worn ones — those are what the machine
   will actually see.
5. Leave learning mode and save.

### Verification

1. Run 20 of each denomination through the acceptor and confirm every one is credited
   at the correct value. A ₱20 credited as ₱10 is the failure mode to watch for.
2. Repeat with **wet coins**. Wet coins travel differently through the chute and are
   the realistic failure case on a machine that sits next to a water outlet. Damp them,
   do not soak them.
3. Run slugs and foreign coins and confirm they are rejected to the tray with no
   credit.
4. Verify the diverter routing physically: ₱1 and ₱5 must reach their hoppers, ₱10 and
   ₱20 must reach the locked profit chamber. Open the side door and look. Do not trust
   the screen.
5. Run Case 14 from `scenarios.md` — a second coin inserted during the diverter lockout
   window must be rejected cleanly, never double-counted, never routed as the previous
   coin's denomination.

Simulated pulses on D2 do not constitute a passing test for any of this. The coin path
signs off on real coins only.

### Record for this unit

| Coin | Old series sampled | Current series sampled | Verified 20/20 | Wet coins pass |
|---|---|---|---|---|
| ₱1 | | | | |
| ₱5 | | | | |
| ₱10 | | | | |
| ₱20 | | | | |

Calibrated by: ______________ Date: ______________

---

## Hopper float

Not calibration, but it belongs with the commissioning checklist.

Load `HOPPER_START_FLOAT` (100) coins into each hopper before the machine goes into
service, and set the inventory on the Admin page to match. Count them physically — the
inventory in EEPROM is only as good as the number entered here, and an inventory that
claims change the machine does not hold leads straight to a jam lockout under a user.

Below `HOPPER_LOW_P1` (25 pcs of ₱1) or `HOPPER_LOW_P5` (5 pcs of ₱5) the machine
disables itself until refilled.
