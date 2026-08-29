# Scenario cases — release gate

Project EMX-2026-WATERVENDO-01, eMathrix Technologies.

Every case here must pass before the machine ships. This document is the acceptance
test suite named in `CLAUDE.md`. Cases 1–10 come from the client's document. Cases
11–15 were added because the machine needs them and the client did not write them.
Cases 16–19 are proposed additions awaiting sign-off — see the note at the end.

## How to use this document

Each case gives a trigger, the expected machine behaviour, the expected screen, the
expected coin outcome, and a hand-verification procedure. Record the result in the
Result column of the summary table at the bottom during the Milestone 7 test pass.

Two rules govern how a case is allowed to pass:

- **Coin path cases must be verified with real coins.** Cases 1, 5, 6, 9, 11, 13, 14
  and 16 touch the coin path. Injecting simulated pulses on the acceptor line does
  not count as a pass for these. Use real ₱1, ₱5, ₱10 and ₱20 coins, both the old and
  the current series of each denomination, and include deliberately wet coins.
- **Money outcomes are counted by hand.** Do not read the payout off the screen and
  call it verified. Count the coins that physically land in the tray and reconcile
  them against the hopper inventory shown on the Coin Inventory page.

Billing constants assumed throughout: ₱1 = 100 mL, ceiling ₱20 per transaction
(2000 mL), partial refunds round **down** to the nearest 100 mL.

---

## Case 1 — Normal transaction, all systems OK

**Trigger.** Machine idle on Standby with water available, hoppers stocked, profit
chamber not full. User inserts ₱20, selects 2000 mL, places a bottle, and the pour
runs to completion.

**Expected behaviour.** Acceptor is enabled at idle. Each coin is identified by pulse
count, the diverter moves to the matching chamber, and the acceptor is disabled for
`COIN_LOCKOUT_MS` while the servo settles. Credit accumulates to ₱20 and the acceptor
is disabled at the ceiling. User selects a volume within the credit; the selection sets
the target in millilitres and its price is deducted from credit. Bottle detected, valve
opens, flow pulses accumulate to the target, valve closes at the target. Transaction
committed to EEPROM.

**Expected screens.** Standby → Select Volume → Insert Bottle → Dispensing → Thank You.

**Expected coin outcome.** ₱20 coin routed to the locked profit chamber. Credit ₱20,
target 2000 mL costs ₱20, remaining credit ₱0, change due ₱0. No hopper movement.
Profit total for the day increases by ₱20.

**Verification by hand.**
1. Note the Coin Inventory page counts for ₱1 and ₱5 before starting.
2. Insert one ₱20 coin. Confirm the Select Volume page shows ₱20.00 inserted and that
   the 2000 mL option is selectable.
3. Select 2000 mL. Place a bottle with at least 2.5 L capacity.
4. Catch the pour in a graduated cylinder instead of a bottle if you want the volume
   check to be exact. Expect 2000 mL ±5%; outside that, stop and recalibrate
   `ML_PER_PULSE` per `calibration.md` — do not adjust billing to compensate.
5. Confirm Thank You shows 2000 mL dispensed, ₱20.00 inserted, ₱0.00 change.
6. Confirm the ₱1 and ₱5 hopper counts are unchanged from step 1.
7. Open the profit chamber and confirm the ₱20 coin is in it, not in a hopper.

---

## Case 2 — No water, low level lockout

**Trigger.** Gallon bay float reads empty while the machine is idle.

**Expected behaviour.** Machine enters the out-of-water lockout. **The coin acceptor is
disabled first**, before the screen changes — the machine must never take money it
cannot honour. The pump is inhibited. The lockout clears automatically when the float
reads not-empty and the cold tank has usable water.

**Expected screen.** `OUT OF WATER — PLEASE REFILL`.

**Expected coin outcome.** No coins accepted. Inserted coins are physically rejected by
the acceptor and fall to the return tray.

**Verification by hand.**
1. With the machine idle, lift the gallon bay float to the empty position (or remove
   the bottles).
2. Confirm the screen changes to the out-of-water message within one second.
3. Insert a ₱1 coin. It must fall through to the return tray. Confirm the credit on
   screen stays at ₱0.00 and the daily totals do not move.
4. Restore the float. Confirm the machine returns to Standby without a power cycle.

---

## Case 3 — Water present but refilling, mid level, pump runs during use

**Trigger.** Cold tank falls below the mid float during or between transactions, with
the gallon bay float reading not-empty.

**Expected behaviour.** Pump turns on at the mid float and keeps running while the
machine continues to serve. Dispensing is not blocked by the pump running. The pump
stops at the high float.

**Expected screen.** Whatever screen the transaction is on — pump activity does not
interrupt the user. System Status shows the cooling and level state.

**Expected coin outcome.** Unaffected. Normal transaction accounting.

**Verification by hand.**
1. Draw the cold tank down below the mid float.
2. Confirm the pump starts and that System Status reflects the filling state.
3. Run a full ₱5 / 500 mL transaction while the pump is running.
4. Confirm the pour completes normally and the measured volume is within tolerance —
   a running pump must not disturb the flow reading enough to fail Case 1's check.

---

## Case 4 — Full tank, high level, pump off

**Trigger.** Cold tank reaches the high float.

**Expected behaviour.** Pump stops immediately at the high float and does not restart
until the level falls back to the mid float. No hunting or rapid cycling at the
threshold.

**Expected screen.** System Status shows a full tank and the pump off.

**Expected coin outcome.** Unaffected.

**Verification by hand.**
1. Let the pump run until the high float is reached.
2. Confirm the pump stops within one second.
3. Watch for two minutes. The pump must not restart or chatter while the level sits at
   the high float. Any cycling here means the float debounce needs work.
4. Draw water until the level drops past mid. Confirm the pump restarts.

---

## Case 5 — Low change inventory lockout

**Trigger.** ₱1 hopper count falls below `HOPPER_LOW_P1` (25 pcs) or ₱5 hopper count
falls below `HOPPER_LOW_P5` (5 pcs).

**Expected behaviour.** Machine enters the low-change lockout. Acceptor disabled first.
A transaction already in progress is allowed to finish and pay out its change; the
lockout applies to starting a new one. Clears when an admin reloads the hoppers and
commits the corrected inventory.

**Expected screen.** `LOW CHANGE — SERVICE REQUIRED`.

**Expected coin outcome.** No new coins accepted. Any change already owed from an
in-flight transaction is still paid.

**Verification by hand.**
1. Using the Admin page, set the ₱1 hopper to 24 pieces and commit.
2. Confirm the lockout screen appears and the acceptor rejects a ₱1 coin to the tray.
3. Restore the ₱1 count to 100 and commit. Confirm the machine returns to Standby.
4. Repeat the whole sequence for the ₱5 hopper at 4 pieces.
5. Physically verify the hopper contents match what the Admin page claims before
   leaving the machine in service — see the desync warning in Case 17.

---

## Case 6 — Coin storage full lockout

**Trigger.** Profit chamber full sensor asserts.

**Expected behaviour.** Machine enters the storage-full lockout with the acceptor
disabled first. Clears when the chamber is emptied by the operator.

**Expected screen.** `COIN STORAGE FULL`.

**Expected coin outcome.** No coins accepted. This is deliberate — a full chamber means
a ₱10 or ₱20 coin has nowhere to go, and accepting one would jam the diverter path.

**Verification by hand.**
1. Assert the chamber full sensor (fill it, or trip the sensor by hand).
2. Confirm the lockout screen and that a ₱10 coin is rejected to the tray.
3. Clear the sensor. Confirm return to Standby.

---

## Case 7 — No bottle placed after confirm, timeout and cancel

**Trigger.** User inserts coins and selects a volume, then never places a bottle.

**Expected behaviour.** Bottle wait runs silently from 0 to 15 s. First buzzer at
`BOTTLE_WAIT_WARN1_MS` (15 s). Second buzzer at `BOTTLE_WAIT_WARN2_MS` (18 s). At
`BOTTLE_WAIT_CANCEL_MS` (20 s) the transaction cancels and the full inserted amount is
refunded. Placing a bottle at any point before 20 s proceeds normally to Dispensing.

**Expected screen.** Insert Bottle throughout, then Thank You showing the full refund.

**Expected coin outcome.** Full refund of inserted credit from the hoppers. No water
dispensed, so nothing is retained. Insert ₱5, wait out the timeout, get ₱5 back — as
one ₱5 coin or five ₱1 coins depending on the payout policy in effect.

**Verification by hand.**
1. Note both hopper counts.
2. Insert ₱5, select any volume, and place nothing.
3. Time the buzzers with a stopwatch. Expect them at 15 s and 18 s, cancel at 20 s.
   Tolerance ±0.5 s.
4. Count the coins that land in the tray. Must total exactly ₱5.
5. Confirm the hopper counts on the Coin Inventory page dropped by exactly what was
   paid out, and that the physical coin count matches.
6. Repeat, placing the bottle at roughly 19 s, and confirm the pour proceeds normally.

---

## Case 8 — Bottle removed mid-dispense, grace period and resume or end

**Trigger.** Bottle proximity sensor reads absent while the valve is open and the
target volume has not been reached.

**Expected behaviour.** Valve closes immediately. Grace timer starts for
`BOTTLE_REMOVED_GRACE_MS` (10 s). Bottle replaced within the grace window resumes the
pour **from the volume already dispensed**, not from zero. Not replaced within the
window ends the transaction, and any change still due is paid out on confirm.

**Expected screen.** Waiting, showing the grace countdown, then back to Dispensing on
resume, or Thank You if the window expires.

**Expected coin outcome on abandonment.** The dispensed volume is rounded **down** to
the nearest `REFUND_ROUND_ML` (100 mL) and charged; the remainder is refunded. Stop at
a measured 305 mL and the machine charges for 300 mL. From ₱20 inserted with 2000 mL
selected, that is ₱3 retained and ₱17 refunded. Rounding always favours the machine.

**Verification by hand.**
1. Insert ₱20, select 2000 mL, start the pour with a graduated cylinder in place.
2. Lift the cylinder away at roughly 300 mL. Confirm the valve shuts at once — no
   dribble past a few millilitres.
3. Confirm the Waiting screen appears with a visible countdown.
4. Replace the cylinder at ~5 s. Confirm the pour resumes and the displayed dispensed
   volume continues from where it stopped rather than restarting at zero.
5. Let the pour run to 2000 mL and confirm normal completion.
6. Repeat, this time letting the grace window expire. Read the cylinder. Confirm the
   machine charges the rounded-down 100 mL multiple at or below the measured volume,
   never above it, and pays the balance as coins.
7. Count the refund by hand against the arithmetic.

---

## Case 9 — Change due, payout and claim

**Trigger.** Transaction finishes with credit remaining and the user chooses to finish
and take the change rather than dispense again.

**Expected behaviour.** Remaining credit is paid out as coins from the hoppers. The
₱1 hopper covers ₱5 dissipation, so it is not drained first. Every payout is confirmed
by the outlet counting sensor — the machine does not assume a coin left because the
hopper was told to run.

**Expected screen.** Thank You, showing volume dispensed, amount inserted, change due,
and the prompt to take the bottle and the change.

**Expected coin outcome.** Coins physically in the tray equal the change due, and the
hopper inventory in EEPROM decrements by exactly the counted coins.

**Verification by hand.**
1. Note both hopper counts.
2. Insert ₱20, select 500 mL (₱5), and complete the pour.
3. Choose finish. Expect ₱15 change.
4. Count the coins in the tray. Must total exactly ₱15.
5. Confirm the Coin Inventory page decremented by exactly the denominations counted.
6. Repeat five times and reconcile the running total. A drift of even one coin per
   transaction is a failure — that is the drift the coins-first billing rule exists to
   prevent, and it means something is deriving a price from the flow sensor.

---

## Case 10 — Balance exhausted, dispensing stops, system resets for next user

**Trigger.** Credit reaches ₱0 with the pour complete and no change due.

**Expected behaviour.** Valve closed, transaction committed to EEPROM, machine returns
to Standby ready for the next user. No stale credit, no stale volume, no leftover
target carried into the next transaction.

**Expected screen.** Thank You, then automatic return to Standby.

**Expected coin outcome.** No change due, no hopper movement.

**Verification by hand.**
1. Run Case 1 to completion.
2. Confirm the machine returns to Standby without a touch.
3. Immediately insert ₱1 and confirm the credit reads ₱1.00, not ₱21.00. A carried-over
   balance here is a state machine reset bug.
4. Power cycle and confirm Standby comes up clean with no open transaction.

---

## Case 11 — Hopper undercount, jam, retry, lockout

*Not in the client's document. Added because a payout that is assumed rather than
counted is how a machine silently loses money.*

**Trigger.** Hopper is commanded to pay N coins. The outlet counting sensor reports
fewer than N within `HOPPER_TIMEOUT_MS` (5000 ms).

**Expected behaviour.** The payout is **not** treated as complete. The hopper retries,
paying only the shortfall, up to `HOPPER_RETRY_MAX` (3) attempts. If the count is still
short after the final retry, the machine locks. The acceptor is disabled first. The
amount actually paid out is recorded in EEPROM so the shortfall is known to the
servicing technician and the inventory is not corrupted.

**Expected screen.** `CHANGE JAM — SERVICE REQUIRED`.

**Expected coin outcome.** Inventory reflects coins **counted out**, not coins
commanded. Under no circumstance may the machine decrement inventory for a coin the
sensor did not see leave.

**Verification by hand.**
1. Induce a genuine jam. Block the ₱1 hopper outlet with a shim, or run the hopper
   nearly empty so it cannot deliver the full count.
2. Trigger a payout of 5 coins.
3. Time the retries. Expect up to three attempts, each bounded by 5 s.
4. Confirm the lockout screen appears after the last retry.
5. Count the coins that did reach the tray. Confirm the Coin Inventory page decremented
   by exactly that number and no more.
6. Clear the jam, correct the inventory on the Admin page, and confirm recovery.

---

## Case 12 — Power loss mid-transaction, EEPROM recovery on boot

*Not in the client's document. Added because the machine holds a user's money at the
moment the power goes.*

**Trigger.** Mains power is cut while a transaction has credit on it — mid-pour, or
between the pour and the change payout.

**Expected behaviour.** On boot the machine reads the EEPROM record, validates the
magic number and checksum, and restores the open transaction with its remaining
balance shown. The user can complete or take their change. If the record is absent or
fails its checksum, the machine initialises clean rather than acting on garbage.

**Expected screen.** The resumed transaction's balance, not Standby.

**Expected coin outcome.** Credit is preserved across the power cut. Money already
inserted is not lost, and money already paid out is not paid twice.

**Verification by hand.**
1. Insert ₱20, select 2000 mL, start the pour.
2. Pull mains power at roughly 1000 mL.
3. Restore power. Confirm the machine boots to the interrupted transaction showing the
   correct remaining balance, not to Standby.
4. Complete or cancel. Confirm the coin outcome matches what the arithmetic says is
   owed given the volume actually dispensed before the cut.
5. Repeat with the cut placed **after** the pour but **before** the change payout.
   Confirm the change is paid exactly once.
6. Repeat with the cut placed **during** a hopper payout. Confirm no coin is paid
   twice and that any shortfall is detected.

---

## Case 13 — Power loss between coin insert and diverter action

*Not in the client's document. Added because there is a window where a coin has been
counted but not yet physically routed.*

**Trigger.** Power is cut in the window between the acceptor identifying a coin and the
diverter finishing its travel — the `COIN_LOCKOUT_MS` window.

**Expected behaviour.** On boot the machine reconciles. A coin credited but not
confirmed routed is handled by a single documented policy, applied consistently. The
coin is physically somewhere in the chute and the machine cannot see it, so the
inventory must not silently claim it landed in a hopper.

**Expected screen.** Resumed transaction, with a service note if reconciliation found
an unrouted coin.

**Expected coin outcome.** Credit to the user is preserved. Hopper inventory is **not**
incremented for a coin whose routing was never confirmed.

**Verification by hand.**
1. Insert a ₱5 coin and cut power within the lockout window — during the servo travel.
2. Restore power. Confirm the user's credit still shows ₱5.
3. Open the side door. Note where the coin physically ended up.
4. Confirm the ₱5 hopper count did **not** increment unless the coin genuinely reached
   the hopper.
5. Repeat ten times, cutting power at varied points in the window, and reconcile the
   physical coin positions against the inventory each time.

> **Open question for review.** The reconciliation policy needs your decision before
> Milestone 3. Options: credit the user and mark the coin unrouted pending a service
> check; or credit the user and assume the profit chamber, which is the safe direction
> for the float since it never over-claims hopper stock. I recommend the second, with
> the event written to the history ring buffer. Confirm before it goes into code.

---

## Case 14 — Coin inserted during the diverter lockout window

*Not in the client's document. Added because this is the most likely way to double-count
money or to route a coin to the wrong chamber.*

**Trigger.** A second coin is inserted while the acceptor is disabled for
`COIN_LOCKOUT_MS` and the servo is still travelling.

**Expected behaviour.** The coin is rejected cleanly to the return tray. It is never
credited, never counted twice, and never routed as the previous coin's denomination.
The `COIN_LOCKOUT_MS` value is not to be shortened to make the machine feel faster — a
coin landing mid-travel jams the chute.

**Expected screen.** Credit unchanged. No flicker or phantom increment on Select Volume.

**Expected coin outcome.** Only the first coin is credited and routed.

**Verification by hand.**
1. Insert a ₱1 coin, then immediately push a ₱20 coin in behind it, as fast as the slot
   physically allows.
2. Confirm the credit reads ₱1.00 and not ₱21.00 or ₱2.00.
3. Confirm the ₱20 coin lands in the return tray, not in the ₱1 hopper.
4. Repeat twenty times with mixed denominations, including wet coins, which travel
   differently and are the realistic failure case.
5. Confirm the profit chamber and hopper contents match the credited coins exactly.

---

## Case 15 — Gallon bay empty while cold tank still has water

*Not in the client's document. Added because this is the single most likely way to
destroy the machine in service.*

**Trigger.** Gallon bay float reads empty while the cold tank floats still report
water available — the tank has stock but there is nothing to pump from.

**Expected behaviour.** **The pump must not run, regardless of what the cold tank
floats say.** The gallon bay float is a safety lockout, not pump control. Dry running
destroys the diaphragm pump. The machine also enters the out-of-water lockout and
disables the acceptor, because it cannot guarantee it can serve the next user.

**Expected screen.** `OUT OF WATER — PLEASE REFILL`.

**Expected coin outcome.** No coins accepted.

**Verification by hand.**
1. Fill the cold tank above the mid float so the tank alone would call for no pump.
2. Draw the cold tank down past the mid float so the pump *would* normally start.
3. With the gallon bay float held in the empty position, confirm the pump does **not**
   start. Listen at the pump and confirm with a clamp meter on the pump line — do not
   trust the screen alone for this one.
4. Hold this state for two minutes and confirm the pump never pulses, not even briefly
   at the float transition.
5. Restore the gallon float. Confirm the pump starts normally.

---

## Proposed additional cases — awaiting sign-off

These are not yet part of the gate. They cover real failure modes but each carries a
decision I do not want to make unilaterally. Confirm which to adopt.

**Case 16 — Unknown or invalid coin pulse train.** A slug, a foreign coin, or a
corrupted pulse burst that matches no denomination. Proposal: discard silently, credit
nothing, log the event. Needs your call on whether repeated invalid coins should raise
a tamper alert.

**Case 17 — Admin inventory edit desyncs EEPROM from physical stock.** The Admin change
edit is the one write path that can make the machine believe it holds change it does
not have, which leads straight to Case 11. Proposal: explicit confirm step, immediate
EEPROM commit, and a service note in the history ring buffer recording the before and
after counts.

**Case 18 — Worst-case change drain on the ₱5 hopper.** Flagged in the build prompt and
restated here so it is not lost: a user inserting ₱20 and selecting 500 mL needs ₱15
back. I will report the drain arithmetic and a proposed change policy at Milestone 5
before writing any payout strategy. No behaviour change without your approval.

**Case 19 — Flow sensor stalls with the valve open.** Valve commanded open, no flow
pulses arriving — a blocked line, a failed sensor, or a closed upstream tap. Without a
timeout the machine waits forever holding the user's money. Proposal: a stall timeout
that closes the valve, refunds on the rounded-down volume actually delivered, and
raises a service condition. Needs a timeout value from you.

---

## Result log — Milestone 7 test pass

Fill during the integration pass. A case is not passed until the hand-verification
steps have been walked physically, with real coins where the case requires them.

| # | Case | Real coins required | Result | Tester | Date | Notes |
|---|---|---|---|---|---|---|
| 1 | Normal transaction | Yes | | | | |
| 2 | No water lockout | No | | | | |
| 3 | Mid level, pump during use | No | | | | |
| 4 | High level, pump off | No | | | | |
| 5 | Low change lockout | Yes | | | | |
| 6 | Coin storage full | Yes | | | | |
| 7 | No bottle, timeout cancel | Yes | | | | |
| 8 | Bottle removed mid-pour | Yes | | | | |
| 9 | Change due and payout | Yes | | | | |
| 10 | Balance exhausted, reset | No | | | | |
| 11 | Hopper undercount jam | Yes | | | | |
| 12 | Power loss mid-transaction | Yes | | | | |
| 13 | Power loss before diverter | Yes | | | | |
| 14 | Coin during lockout window | Yes | | | | |
| 15 | Gallon empty, tank full | No | | | | |

Coin series coverage for the coin path cases — tick each when tested:

| Denomination | Old series | Current series | Wet |
|---|---|---|---|
| ₱1 | | | |
| ₱5 | | | |
| ₱10 | | | |
| ₱20 | | | |

The ₱20 is thinner and lighter than the rest, so sample it more times than the others
in acceptor learning mode and test it more times here.
