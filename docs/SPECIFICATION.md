SPECIFICATION — Water Refill Vending Machine

Project EMX-2026-WATERVENDO-01. Complete functional, logical, and algorithmic specification for firmware and hardware.

This document is authoritative for behaviour. CLAUDE.md remains authoritative for coding constraints. Where this document and the client's mockup PDF disagree on screen layout, the PDF wins. Where they disagree on money handling, this document wins.

0. The one rule everything else serves

Coins determine volume. Volume never determines money.

Credit is fixed before the valve opens. The flow sensor is a stopper, not a cashier. Every algorithm below is arranged so that no code path can derive an amount owed from a measured volume.

The second rule, which resolves every ambiguity the first one doesn't: when the machine must guess, it guesses against itself. Unrecognised coin goes to profit, not to the hoppers. Dispensed volume rounds down, never to nearest. Unknown routing after power loss assumes profit chamber. An uncertain hopper count is treated as a failure, not a success. The machine never over-claims what it holds.

1. Hardware map
1.1 Interrupt allocation

Only two, both edge-counting, both ISRs setting a volatile counter and returning.

Pin	Int	Source	Edge
D2	INT0	Coin acceptor pulse	FALLING
D3	INT1	Flow sensor pulse	FALLING

Everything else is polled in update(). Hopper outlet coins leave at 5–10/sec, well inside polling range. This is why the no-delay() rule is load-bearing rather than stylistic: a blocked loop drops a counted coin, and a dropped count is a false jam.

1.2 Digital inputs

All INPUT_PULLUP, active LOW. A broken wire reads inactive rather than as a phantom coin or a phantom bottle.

Signal	Debounce	Notes
Hopper ₱1 outlet count	25 ms	Counts coins actually leaving
Hopper ₱5 outlet count	25 ms	Counts coins actually leaving
Float, cold tank mid	250 ms	Pump ON threshold
Float, cold tank high	250 ms	Pump OFF threshold
Float, gallon bay	500 ms	Empty lockout, pump interlock
Bottle proximity	80 ms	Long enough to ignore hand shadow
Profit chamber full (IR beam)	500 ms	Beam broken = full

Float debounce is long because water sloshes. Bottle debounce is short because the removal grace period depends on prompt detection, but not so short that a passing hand triggers it.

The hopper outlet figure was 5 ms in an earlier draft. That was an error. At the 5–10 coins/sec of §1.1 the real interval between coins is 100–200 ms, so 25 ms leaves a 4× margin against contact bounce while still being an order of magnitude clear of a genuine coin. Take the margin: a bounce counted as a coin records change that was never paid AND corrupts the inventory in the same event, which is the worst pair of consequences available in this machine.

1.3 Digital outputs
Signal	Type	Notes
Acceptor inhibit	Active HIGH	Asserted at boot, before any fault display
Coin diverter servo	PWM	Three positions, see 3.2
Hopper ₱1 motor	Relay	
Hopper ₱5 motor	Relay	
Water pump	SSR (DC)	Through pump_write() only. See 1.5
Solenoid valve	Relay	
Compressor	SSR (AC)	Thermostat control, see 5.4. See 1.5
Buzzer	Active HIGH	Timeout and fault stages
Level LEDs ×3	Active HIGH	Low, mid, high
1.4 Other
Signal	Bus
Nextion HMI	Serial2, D16/D17, 9600 baud
Temperature DS18B20	OneWire, dedicated digital pin
Real-time clock DS3231	I²C, D20 SDA / D21 SCL, battery-backed
Confirm button	Digital in, INPUT_PULLUP, 50 ms debounce

1.4.1 The clock is a DS3231, and this is not interchangeable

DS3231 specifically. Not a DS1307, and not the Mega's own millis() timekeeping.

The daily profit total is a number the owner counts money against. A "daily" total that silently means "since the last power cut" is worse than no total at all, because the operator cannot tell which one they are reading and will trust it either way. That is what the battery is for, and it is why this is a required part rather than a convenience.

The DS3231 is temperature-compensated and holds a couple of minutes a year. A DS1307 drifts that much in a month, and it will be sitting in a cabinet with a compressor cycling next to it. Buy the module with the CR2032 included, and check it is not a DS3231M — the M part is a lower-grade oscillator sold in the same footprint and is much worse.

Two behaviours are required of it, and both are firmware, not hardware:

A FAILED CLOCK REPORTS FAILURE, IT NEVER GUESSES. If the oscillator-stop flag is set, the I²C read fails, or the date reads back implausible — outside 2026–2099, an impossible calendar date, hour 24, the device in 12-hour mode — the machine treats the clock as failed. It shows a clock-not-set state, and every history entry written in that condition carries an invalid-timestamp marker rather than a plausible-looking wrong date. A receipt stamped with a date that is confidently wrong is worse than one that admits it does not know.

THE DAY BOUNDARY IS EXPLICIT. See 7.4.
1.5 Remaining hardware decisions

DECIDED — compressor and pump switching: SSR, both.

A mechanical relay's contact life on a compressor's inrush is measured in cycles and this one cycles all day, but the deciding reason is electrical rather than mechanical: contact arcing on a mechanical relay couples into a high-impedance pulse input and reads as phantom coin pulses. A phantom coin is free water for the user and inventory drift for the operator, and it is miserable to diagnose after the fact because it only appears under load. The pump is switched the same way for the same reason — it is the other noisy inductive load sharing a ground with the acceptor.

This is cheap to prevent at build time. See §1.6 and docs/wiring.md for the SSR types and the pulse-input filtering that goes with them.

STILL OPEN — report rather than choosing silently:

Whether the profit chamber IR beam sits at the true fill line or below it, giving a service margin before the chamber is physically full.
Buzzer type, active or passive. Note that every pattern in §6.3 is a timing pattern rather than a pitch, so an active buzzer driven by a non-blocking millis() pattern generator produces all five. A passive buzzer additionally needs a tone pin, and tone() on AVR claims a timer. Recommend active unless distinct pitches are wanted.
Whether the confirm button of §1.4 is duplicated as an on-screen target in the mockup. The physical button is specified and pinned regardless; this only decides whether the HMI carries an equivalent.

1.6 Noise control on the pulse inputs

The two interrupt inputs of §1.1 are the only signals on this machine where a single corrupted edge is a money error, and they share a chassis with the noisiest inductive loads on it.

Both lines are run in shielded cable, with the shield grounded at the CONTROLLER END ONLY. Grounding both ends makes the shield a ground loop and injects the very current it was there to exclude.
Both inputs carry an RC low-pass at the controller end, plus an external pull-up.
Both keep INPUT_PULLUP enabled in firmware regardless, so a disconnected line still reads inactive rather than floating.

Component values and the reasoning behind each are in docs/wiring.md. The filter corner is chosen to pass the slowest legitimate signal by a wide margin while rejecting the microsecond-scale spikes that switching produces.
2. State machine

One non-blocking machine in loop(). Every transition below is explicit; there are no implicit fallthroughs.

2.1 States
BOOT              → power-on, EEPROM restore, self-check
STANDBY           → idle, welcome screen, acceptor enabled
ACCEPTING         → coins going in, credit accumulating
SELECTING         → user choosing target volume within credit
AWAITING_BOTTLE   → confirm pressed, waiting for bottle
DISPENSING        → valve open, flow counting to target
PAUSED            → bottle removed mid-pour, grace running
SETTLING          → valve closed, flow tail draining
COMPLETE          → pour done, offering more-or-finish
PAYING_CHANGE     → hopper payout in progress
THANK_YOU         → summary, change claim
FAULT             → locked, acceptor inhibited
ADMIN             → service mode
2.2 Transitions
From	Event	To
BOOT	restore OK, no faults	STANDBY
BOOT	restore finds open txn	COMPLETE (resume with remaining credit)
BOOT	persistent fault in EEPROM	FAULT
STANDBY	first coin accepted	ACCEPTING
STANDBY	admin gesture	ADMIN
ACCEPTING	coin accepted	ACCEPTING (credit updated)
ACCEPTING	credit at MAX_TRANSACTION_PESOS	SELECTING (acceptor inhibited)
ACCEPTING	confirm pressed	SELECTING
SELECTING	target chosen	AWAITING_BOTTLE
SELECTING	finish without pour	PAYING_CHANGE
AWAITING_BOTTLE	bottle detected	DISPENSING
AWAITING_BOTTLE	back pressed	SELECTING (selection cancelled, credit restored in full)
AWAITING_BOTTLE	BOTTLE_WAIT_CANCEL_MS elapsed	PAYING_CHANGE (full credit)
FAULT	admin gesture	ADMIN (arriving does NOT clear the fault)
DISPENSING	target reached	SETTLING
DISPENSING	bottle removed	PAUSED
DISPENSING	no pulses for FLOW_STALL_TIMEOUT_MS	SETTLING (stall latched, see 5.2)
PAUSED	bottle replaced within grace	DISPENSING
PAUSED	grace expired	SETTLING
SETTLING	tail window elapsed, stall latched	PAYING_CHANGE (then FAULT)
SETTLING	tail window elapsed	COMPLETE
COMPLETE	user chooses dispense more, credit remains	SELECTING
COMPLETE	user chooses finish, or credit is zero	PAYING_CHANGE
PAYING_CHANGE	payout confirmed by outlet counts	THANK_YOU
PAYING_CHANGE	payout short after retries	FAULT (change jam)
PAYING_CHANGE	nothing due	THANK_YOU
THANK_YOU	timeout or bottle removed	STANDBY
FAULT	condition cleared, transient fault	STANDBY
FAULT	admin clear, persistent fault	STANDBY
ADMIN	exit	STANDBY
2.3 States where the acceptor is enabled

ACCEPTING and STANDBY only. Everywhere else it is inhibited. Any state entered from a fault inhibits before the screen changes.

3. Coin path
3.1 Identification

The acceptor is programmed in learning mode to emit a distinct pulse count per denomination:

Coin	Pulses
₱1	1
₱5	2
₱10	3
₱20	4

A pulse train is terminated when no further pulse arrives within COIN_PULSE_GAP_MS. Too short and one ₱20 reads as two coins; too long and rapid insertion merges into one.

on pulse (ISR):        pulse_count++, last_pulse_ms = millis()
in update():
  if pulse_count > 0 and (millis() - last_pulse_ms) >= COIN_PULSE_GAP_MS:
      n = pulse_count; pulse_count = 0
      if n > COIN_PULSE_MAX:
          overmax_streak++
          if overmax_streak >= COIN_OVERMAX_FAULT_MAX:
              faults_raise(FAULT_ACCEPTOR)
          discard
      else:
          overmax_streak = 0
          coin = decode(n)          // unmapped n → treat as ₱1 value, route to profit
          route_and_credit(coin)

An unmapped-but-in-range count credits the user the minimum and routes to profit. Fails against the machine on routing, in the user's favour on credit, which is the correct direction for both.

3.2 Diverter sequence

Order is fixed and must not be reordered:

1. inhibit acceptor
2. identify coin
3. write routing intent to EEPROM        ← before the servo moves
4. move servo to destination angle
5. wait COIN_LOCKOUT_MS for travel and settle
6. commit: increment the destination counter, clear intent
7. uninhibit acceptor

Step 3 before step 4 is what makes power-loss reconciliation possible. If power drops between 3 and 6, the boot path sees an uncommitted intent.

Three servo positions: ₱1 hopper, ₱5 hopper, profit chamber. Angles are per-unit and live in config.h as starting values pending mechanical setup.

A coin arriving during the lockout window is drained, never queued. There is no buffer of pending coins. A queued coin would be routed by a servo that has since moved.

3.3 Power-loss reconciliation

On boot, if an uncommitted routing intent exists:

credit the user the coin's value
increment the PROFIT counter, not the hopper counter
write a distinctly tagged event to the history ring
clear the intent

Assuming profit never over-claims hopper stock. Overstating hopper stock makes the machine promise change it cannot pay, which is a jam and a stranded user. Understating makes it lock early, which is an inconvenience. The tag exists so a service tech reading history sees why a physical count differs, rather than suspecting theft.

3.4 Hopper payout algorithm

Largest-coin-first with a hard ₱5 reserve.

plan(amount_pesos, p1_count, p5_count) → {n1, n5} or FAIL

  p5_spendable = max(0, p5_count - HOPPER_RESERVE_P5)
  n5 = min(amount_pesos / 5, p5_spendable)
  remainder = amount_pesos - (n5 * 5)
  n1 = remainder
  if n1 > p1_count: return FAIL
  return {n1, n5}

HOPPER_RESERVE_P5 = 10. Below the reserve, ₱5 payouts stop and change is made entirely in ₱1. Lowering the reserve trades service uptime for change quality; raising it locks the machine more often.

₱5 is the scarce coin — it arrives slowly and leaves fast in a machine where ₱15 change is a common outcome. ₱1 recirculates heavily and absorbs the pressure. This is the client's own instinct from their document, where ₱1 exists for ₱5 dissipation, made explicit and enforced.

can_cover(amount) is plan(amount) != FAIL. Checked before accepting coins for a transaction, against the worst case for the credit ceiling, not after the pour. The machine never takes money it cannot honour.

3.5 Payout execution
for each denomination with n > 0:
    start hopper motor
    count outlet pulses until n reached or HOPPER_TIMEOUT_MS
    stop motor
    wait HOPPER_SETTLE_MS                      ← coin in the throat still has to fall
    read final count
    if count < n:
        retry the SHORTFALL only, up to HOPPER_RETRY_MAX
    if count > n:                              ← overpay, do not "correct" it
        log, adjust inventory by actual, continue
    if still short after retries:
        faults_raise(FAULT_CHANGE_JAM)          ← persistent

Retrying the shortfall, not the whole amount, is what prevents a settle-phase miscount from turning into a double payout. HOPPER_SETTLE_MS exists because declaring a count short early makes the machine retry a payout that already worked.

Inventory decrements by the counted amount, never the commanded amount.

4. Billing
4.1 Credit accumulation
credit_centavos += coin_value_centavos

Integer centavos throughout, int32_t. Volume is integer millilitres. No float anywhere in this path. Credit is capped at MAX_TRANSACTION_PESOS; the acceptor inhibits on reaching it.

4.2 Volume selection

The client's mockup shows a volume menu after coins are inserted. This is compatible with coins-first billing and is implemented as spending credit, never as measuring water into a price:

available_ml = (credit_centavos / 100) * ML_PER_PESO
options = multiples of ML_PER_PESO up to available_ml
selection sets target_ml
cost_centavos = (target_ml / ML_PER_PESO) * 100     ← from the SELECTION
credit_centavos -= cost_centavos

The price comes from the chosen target, computed before the valve opens. Options above available credit are shown greyed, not hidden, so the user can see what more money buys.

4.3 Dispense cutoff
volume_ml accumulates from flow pulses (see 5.1)
when volume_ml >= target_ml:  close valve → SETTLING

No money is computed here. This is the entire content of the cutoff.

4.4 Partial dispense and refund

When a pour ends before target — bottle removed past grace, or user stop:

billed_ml = round_down(volume_ml, REFUND_ROUND_ML)
unused_ml = target_ml - billed_ml
refund_centavos = (unused_ml / ML_PER_PESO) * 100
credit_centavos += refund_centavos

round_down truncates toward zero, always. A measured 305 mL is billed as 300 mL, refunding 200 mL of a 500 mL target. Never round to nearest. Never round up. The rounding remainder is the machine's margin against flow-sensor tolerance and it must always fall the machine's way.

4.5 Final settlement
change_due_pesos = credit_centavos / 100
if change_due_pesos == 0: → THANK_YOU
else: plan and pay per 3.4/3.5 → THANK_YOU
5. Water path
5.1 Flow accumulation with remainder carry

The pulse ratio does not divide cleanly and there is no float available:

volume_ml = (pulses * ML_PER_PULSE_NUM + carry) / ML_PER_PULSE_DEN
carry     = (pulses * ML_PER_PULSE_NUM + carry) % ML_PER_PULSE_DEN

Carrying the remainder between calls keeps truncation error under a millilitre across a whole pour instead of losing a fraction on every pulse.

FLOW_PULSES_PER_LITRE must be measured per unit at the actual dispensing flow rate. The sensor's ratio shifts 5–8% between low and high flow, so a calibration run done by dumping water through fast produces a number wrong by more than the tolerance this design controls.

5.2 Flow stall

Valve open, no pulses for FLOW_STALL_TIMEOUT_MS (5000 ms):

close valve
latch the stall                          ← do NOT raise the fault yet
bill the volume actually dispensed, rounded down
refund the remainder to credit
pay change                               ← user is paid out in full first
faults_raise(FAULT_FLOW_STALL)          ← persistent, raised LAST

At any real flow rate pulses arrive continuously, so five seconds never trips on a normal pour. All three causes — blocked line, dead sensor, closed upstream tap — need a person, so the machine must not attempt to continue. The same constant covers the never-started case.

The ordering is the point and it is enforced by invariant 8 in §9. An earlier draft of §2.2 sent DISPENSING straight to FAULT on a stall. That was wrong: faults_raise() inhibits the acceptor and the machine locks, which strands the user's change inside a locked machine. The stall is latched through SETTLING and PAYING_CHANGE, and the fault is raised only once the money is out. Settle first, lock second.

5.3 Level control and pump interlock
pump_write(on):
    if gallon_bay_empty: force OFF, return       ← single choke point
    if on and (millis() - last_off) < PUMP_MIN_OFF_MS: return
    if on and run_time > PUMP_MAX_RUN_MS: force OFF, raise fault
    write output

Hysteresis: mid float triggers ON, high float triggers OFF. The gallon-bay float is a safety interlock, not pump control — if it reads empty the pump must not run regardless of what the cold tank floats say. Dry running destroys the pump and is the most likely way to kill this machine in service.

PUMP_MAX_RUN_MS catches the case where the pump runs but the high float never trips: a failed float, a failed pump, or a blocked line.

5.4 Cooling

Thermostat with hysteresis on the DS18B20 reading. Compressor ON above the upper setpoint, OFF below the lower. Minimum off-time must be enforced — restarting a compressor against head pressure is how they fail.

Cooling never blocks a transaction. Warm water is a service quality issue, not a fault.

5.5 Bottle detection

Debounced presence. Removal during DISPENSING closes the valve immediately and enters PAUSED with a visible countdown. Replacement within BOTTLE_REMOVED_GRACE_MS reopens the valve and continues from the volume already dispensed, not from zero.

6. Faults
6.1 Classes

Persistent — survive reboot, cleared only from Admin. Stored in EEPROM with the same checksum as inventory.

Fault	Trigger
CHANGE JAM	Hopper short after retries
FLOW STALL	No pulses while valve open
ACCEPTOR	Over-max pulse streak
PUMP	Max run without high float

Transient — auto-clear when the condition clears.

Fault	Trigger	Display
OUT OF WATER	Gallon float empty	OUT OF WATER — PLEASE REFILL
LOW CHANGE	₱1 < 25 or ₱5 < 5	LOW CHANGE — SERVICE REQUIRED
STORAGE FULL	Chamber beam broken	COIN STORAGE FULL

A persistent fault that clears on power cycle is worse than not claiming persistence at all: the operator learns that the fix is a reboot, the coins stay jammed, and the machine returns to accepting money it cannot pay out.

6.1.1 LOW CHANGE is transient, and is evaluated at exactly one moment

LOW CHANGE is condition-driven, not service-latched. It clears the moment the operator loads coins and confirms the count in Admin — there is no separate clear step, because the condition itself has gone.

It is evaluated BEFORE ACCEPTING THE FIRST COIN OF A TRANSACTION, against the worst case for the credit ceiling per 3.4, and it is NEVER RE-EVALUATED MID-TRANSACTION.

A user who has already put money in must never hit a lockout that strands it. That is invariant 8 from the other direction: settle first, lock second, and once coins are in, the machine finishes the transaction it started. The check belongs at the gate, before the machine has taken anything, because that is the only moment where refusing costs the user nothing.

Checking mid-transaction would also be wrong on its own terms — the ceiling worst case was already covered when the transaction opened, and the hoppers only get emptier by paying out change that was planned against that same check.

6.2 Ordering

Display priority when several are active, most blocking first:

CHANGE JAM > FLOW STALL > PUMP > ACCEPTOR > OUT OF WATER > LOW CHANGE > STORAGE FULL

faults_raise() inhibits the acceptor before it returns, ahead of any screen change. That ordering is the invariant; the screen is cosmetic, the inhibit is not.

6.3 Buzzer stages
Event	Pattern
BOTTLE_WAIT_WARN1_MS (15 s)	Single short
BOTTLE_WAIT_WARN2_MS (18 s)	Double short
BOTTLE_WAIT_CANCEL_MS (20 s)	Long, then cancel
Bottle removed mid-pour	Single short, repeat every 2 s during grace
Fault raised	Triple short
7. Persistence
7.1 EEPROM regions

Mega has 4 KB. One checksum covers the whole state block; the history ring is checksummed separately so a corrupt history cannot invalidate inventory.

Region	Contents
Header	Magic number, schema version, checksum
Inventory	₱1 count, ₱5 count, profit ₱10 count, profit ₱20 count, profit unknown count
Open transaction	Credit, target, dispensed
Routing intent	Pending coin value and destination
Fault state	Persistent fault flags
Daily counters	Volume dispensed, profit, date — wear-levelled ring
History ring	20 entries: timestamp, amount in, volume out, change out, event tag

profit_p10 and profit_p20 are separate counters. Without the split the chamber's peso value cannot be derived from its count, and reconciling a physical collection against the recorded total becomes impossible.

profit_unknown is a third counter, for coins routed to the chamber whose denomination the firmware could not identify (§3.1) and for coins whose routing was interrupted by power loss (§3.3). Folding these into either denomination counter would corrupt exactly the reconciliation the split exists to provide; leaving them uncounted would mean a physical collection never matches the record with nothing to explain the gap. A third counter is honest and makes the discrepancy legible to whoever opens the chamber: the peso value of the chamber is `10·p10 + 20·p20`, with `profit_unknown` coins of unstated value alongside it.

Records are framed individually rather than under one block checksum — each carries its own magic word, schema version and CRC8. A corrupt open-transaction record therefore cannot invalidate the inventory. The history ring is framed per entry for the same reason.

The open transaction does not store a machine state. §2.2 resumes an interrupted transaction to COMPLETE with its remaining credit regardless of where it was interrupted, so a stored state would be dead weight, and a mid-pour state cannot be safely resumed in any case.

7.2 Write policy

EEPROM.update() only, never EEPROM.write(). Writes occur at end of transaction, at each inventory change, and on routing intent. Never per loop iteration. Daily counters are wear-levelled across a ring of cells because they change most often.

7.3 First boot and corruption

Virgin cells read 0xFF. If the magic number is absent or the checksum fails:

initialise all counters to zero
set schema version
write header and checksum
raise LOW CHANGE                    ← inventory is genuinely unknown

Raising LOW CHANGE on a fresh EEPROM is correct. The machine does not know what is in its hoppers, and the honest response to unknown inventory is to refuse money until an operator loads the float and confirms the count in Admin.

7.4 The day boundary

Daily totals reset at midnight. The rollover is handled explicitly, and the ordering is fixed:

on day boundary (RTC date changes):
    write the closing totals to the history ring     ← FIRST
    then zero them

Write-then-zero, never the other way round. Once the totals are zeroed there is nothing anywhere that remembers what they were, so a rollover that zeroed first would lose the whole day on any power cut in between — and the day it loses is the one the operator was about to read.

Both the midnight rollover and the Admin "reset daily totals" go through one internal choke point so neither can grow a path that skips the write. They are tagged distinctly in the history so a technician can tell a day that ended on its own from one a person ended by hand.

If the clock is failed there is no boundary and no rollover. Totals keep accumulating rather than resetting on a guess, and the display shows the clock-not-set state so the figure is not mistaken for a day's takings.

A machine that was switched off over midnight does not fire a rollover on the first reading after boot. There is no open day in RAM to close — the totals in EEPROM already belong to whatever day they were last written on, and the operator reads them from the history.

8. Admin

Entered by a gesture not reachable in normal use.

Function	Constraint
Load change, set ₱1 and ₱5 counts	Explicit confirm step, commits to EEPROM on accept
Clear persistent fault	Requires the physical cause to be cleared first where detectable
View history	20-entry ring, read only
View daily totals	Read only
Reset daily totals	Confirm step

The change-inventory edit is the one write path that can desync EEPROM from physical hopper contents, which is why it needs the confirm and an immediate commit.

Transaction history beyond the 20-entry ring is not buildable on this hardware. If the client wants a longer history it is a new scope item to be quoted, not built.

9. Invariants

These are enforced at single choke points, not by convention. No later edit may add a path that bypasses them.

The pump output is written only by pump_write(), which contains the gallon-bay inhibit.
faults_raise() inhibits the acceptor before returning.
No function accepts a millilitre reading and returns an amount owed. billing.h must not contain one, and the reason is documented in the header.
Hopper inventory decrements by counted coins, never commanded coins.
Rounding in the money path is always toward the machine.
Unrecognised or unroutable coins default to the profit chamber.
Credit is fixed before the valve opens and is never modified by a flow reading except through the round-down refund path.

**8. A fault is never raised while money is owed to a user who is still standing there. Settle first, lock second.**

A detected fault is latched, the transaction is settled, the change is paid, and only then is faults_raise() called. Stranding a user's coins inside a locked machine is worse than whatever the fault was protecting against, and it is the failure they will remember and repeat to other people.

The single exception is CHANGE JAM, where the machine has physically demonstrated it cannot pay. There, locking is the honest outcome — the money is not reachable and pretending otherwise helps nobody. Every other fault waits its turn.

This applies to FLOW STALL (see 5.2), PUMP, ACCEPTOR, OUT OF WATER and STORAGE FULL alike. Any new fault added later inherits it by default; if a new fault genuinely cannot wait, that is a change to this invariant and must be argued for explicitly, not assumed.
10. Deferred

Do not build these now. They are named so they are not lost.

Case 18, sustained ₱5 drain. Milestone 5. Report the arithmetic and propose a policy without changing the client's stated behaviour.
Hopper sourcing. A candidate unit at a much lower price than budgeted is under evaluation. Firmware must not assume a specific hopper's outlet signalling; if the unit ships without a dispense count output, an external IR sensor supplies it and the module interface does not change.
11. Out of scope

WiFi, Bluetooth, cloud sync, remote monitoring, mobile app, GPS, card payment, SD logger. Each is a scope item under the service agreement and must be quoted before it is built. If a requirement appears to need one of these, stop and report rather than building it.