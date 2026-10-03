# Water Vendo HMI simulator

A clickable copy of the vending machine's touch screen, so the screen design can be
approved **before** the panel is bought or the Nextion `.HMI` file is drawn.

The screen is shown at its **real size**, 480 × 320 pixels: the Nextion NX4832T035
(3.5"). Nothing is enlarged. If something looks small, it will look that small on the
machine too.

> This is a demonstration. The real machine runs the firmware in `src/`, and where the
> two ever disagree, `src/` is right.

---

## Opening it

1. Open the folder `sim/hmi/`.
2. Double-click **`watervendo-hmi.html`**.

It opens in any browser (Chrome, Edge or Firefox) with no internet, no installation and
no server. A laptop is best: the screen sits on the left and the controls on the right.

The first time it opens, the machine shows **LOW CHANGE — SERVICE REQUIRED**. That is
correct. A new machine does not know how many coins are in its change hoppers, so it
refuses money until an operator loads them. Step 0 of the walkthrough does that.

---

## Walkthrough: insert ₱20, take 500 mL, collect ₱15 change

| Step | What to do | What you should see |
|---|---|---|
| 0 | Leave **₱1 = 115** and **₱5 = 34** in the *Stock* boxes. Press **Set P1 stock**, then **Set P5 stock** | The fault screen clears and **WELCOME** appears |
| 1 | Press **Insert ₱20** | **SELECT VOLUME** appears. ₱20 is the most one transaction accepts, so the machine moves on without waiting for CONFIRM. All 20 options are lit |
| 2 | Tap **500 mL ₱5.00** on the screen | **INSERT BOTTLE**: selected 500 mL, balance ₱15.00 |
| 3 | Press **Place bottle** | **DISPENSING...**: water dispensed counts up to 500 mL and the bar fills. The nav bar is dimmed while water is pouring |
| 4 | Wait about 20 seconds (or set *Speed* to 5×) | **DISPENSING COMPLETE** with two buttons: DISPENSE MORE and FINISH |
| 5 | Tap **FINISH — Get change ₱15.00** | **DISPENSING YOUR CHANGE**: three ₱5 coins are counted out |
| 6 | — | **THANK YOU!**: 500 mL, ₱20.00 inserted, ₱15.00 change dispensed, and a large amber **PLEASE TAKE YOUR CHANGE** box above the bottle reminder |
| 7 | Press **Remove bottle** | Back to **WELCOME**. The readout shows ₱5 stock down from 34 to 31 |

To see the rest of the design, try these:

- **Take the bottle away during a pour.** Press *Remove bottle* while it is dispensing.
  The screen shows a 10-second countdown. Put the bottle back in time and it continues
  from where it stopped. *(See "Known firmware defects" below first: by default the
  simulator reproduces a firmware bug here.)*
- **Walk away after paying.** Insert coins, choose a volume, and do not place a bottle.
  The buzzer sounds at 15 s and 18 s; at 20 s the whole amount is refunded.
- **Stop part-way.** Remove the bottle at around 300 mL and let the countdown run out.
  A measured 305 mL is charged as 300 mL and the rest is refunded. The machine always
  rounds in its own favour, never the user's.
- **Faults.** Set the gallon bay to *empty*, set the chamber full, or set *Next payout*
  to *hopper jams*. Each fault screen says in plain words what is wrong and what the
  operator should do.
- **Power cut.** Insert some coins, then press *Power cut*. The page reloads as if the
  machine lost power, and it comes back with the user's balance restored.
- **Statistics, Status, Inventory.** Use the tabs along the bottom when the machine is
  idle.
- **Admin.** Press *Admin gesture*, or hold the water drop on the WELCOME screen for
  3 seconds.

---

## The controls

**Customer**

| Control | What it does |
|---|---|
| Insert ₱1 / ₱5 / ₱10 / ₱20 | Drops a coin into the acceptor. If the acceptor is switched off at that moment (between coins, during a pour, or while locked), the coin falls straight to the return tray, as the real one does |
| Press CONFIRM | The physical confirm button. Ends coin entry and opens the volume menu |
| Place / Remove bottle | Puts a bottle on, or takes it off, the platform sensor |
| Collect from tray | Empties the change and coin-return tray. The readout shows what was in it |

**Machine**

| Control | What it does |
|---|---|
| Flow: slow / normal / fast / stall | How fast water runs: 10, 25 or 50 mL/s. *Stall* means no flow at all, as with a blocked line |
| Gallon bay: full / empty | The float in the gallon bay. *Empty* locks the machine with OUT OF WATER |
| Cold tank: low / mid / high | The cold tank floats. *Low* starts the pump, *high* stops it |
| Next payout | *Hopper jams*: the next change payout stops after one coin, retries three times, then locks with CHANGE JAM. *Sensor misses one coin*: a coin leaves but is not counted, so the retry pays one coin too many |
| Set P1 stock / Set P5 stock | Enters the hopper counts, as the operator does in Admin. Recorded in the history |
| Set chamber full | Breaks the coin-box beam. Locks the machine with COIN STORAGE FULL |
| Break / Fix the clock | Simulates a failed real-time clock. Dates show **CLOCK NOT SET** instead of a wrong date |
| Admin gesture | Opens the Admin pages (history, change loading, service) |
| Power cut | Reloads the page and keeps what the real machine keeps through a power cut: the EEPROM and the physical state of the cabinet. Everything else is lost |
| Reset everything | Erases the simulated EEPROM, back to a factory-new machine |

**Readout and log.** Under the controls is a live readout of the state, credit,
target, dispensed volume, hopper stock and faults. Under the screen is a log listing
every state change and why it happened, so it is clear why the machine did what it did.

**Simulator.** *Speed* runs time 2× or 5× faster. *Outline fields that do not fit*
marks in red any text field that is clipped. *Run fit audit* checks every page with
worst-case values.

---

## For the project team

Everything below is for eMathrix and whoever maintains this file.

### What is ported and how it is checked

| Part | Source | Fidelity |
|---|---|---|
| `change_plan()` | `src/change_plan.cpp` | Transcribed. Checked input for input against the compiled C |
| Billing | `src/billing.cpp` | Transcribed. Checked against the C over 400 random sequences of 30 operations |
| Fault priority mask | `src/fault_mask.cpp` | Transcribed. Checked for all 256 masks |
| State machine, transition table | `src/main.cpp` | Ported function by function, `LEGAL[]` included |
| Acceptor, diverter, hopper, flow, dispense, bottle, water level, faults | `src/*.cpp` | Ported. Pins are replaced by a simulated cabinet |

```
# from the repo root, host toolchain per test/README.md
g++ -std=gnu++11 -Wall -Wextra -I include -o port_oracle sim/hmi/port_oracle.cpp src/change_plan.cpp src/billing.cpp src/fault_mask.cpp
./port_oracle > oracle.txt
node sim/hmi/verify_port.js oracle.txt      # -> IDENTICAL to src/ (171,401 lines)
```

**Run this after any change to `change_plan.cpp`, `billing.cpp` or `fault_mask.cpp`.**
A mismatch means the JavaScript is wrong. A one-character change planted in
`change_plan()` produces 1,066 mismatching lines.

Opening the page with **`?selftest`** runs 48 scripted scenarios at full speed against
scratch storage and then runs the fit audit. Headless:
`msedge --headless=new --virtual-time-budget=120000 --dump-dom "file:///…/watervendo-hmi.html?selftest"`
and read `#audit-out`. Opening with `?shot=N` freezes the screen on fit-audit case N, for
screenshots.

### Behaviour that does not exist in `src/` yet

Marked `[SPEC, src TODO]` in the file. Each is a reading of `SPECIFICATION.md`, and
each will be replaced by a port of the real code when M5 Part C lands.

| Behaviour | Spec | src today |
|---|---|---|
| BOOT → FAULT / COMPLETE / STANDBY and the in-flight coin reconcile | §2.2, §3.3 | Always BOOT → STANDBY (`TODO(M5-C)`) |
| Transient faults clear when the condition goes | §6.1, §6.1.1 | `faults_update()` is empty |
| OUT OF WATER and STORAGE FULL raised at the STANDBY gate, next to LOW CHANGE | §6.1, invariant 8 | Never raised. Raising them only at the gate is my reading of "settle first, lock second" |
| Coin-box beam read and debounced | §1.2 | Pin never read (5-9) |
| Bottle-wait buzzer ladder, pause beeps, fault triple beep | §6.3 | `TODO(M5-C)` |
| Admin: history, change load with confirm, clear fault, reset daily | §8 | `TODO(M5-C)`. `ADMIN_CLEAR_FAULT` and `ADMIN_RESET_DAILY` are not in `hmi_event_t` |
| Nav tabs switch pages locally on the Nextion | — | `HMI_EVENT_NAV_*` exist, nothing reads them |

### Known firmware defects found while porting

The port reproduces these, because a port that hid them would misrepresent the
firmware. They are listed here and in the WO-002 report.

- **P-1 · One-shot inputs survive state changes.** The confirm press and the bottle
  placed/removed flags are cleared only by their reader, so a flag raised in one state is
  picked up by a later one. Three effects you can see in the simulator:
  1. **Case 8 is broken.** The bottle is placed during INSERT BOTTLE, and that "placed"
     flag is still set when the bottle is lifted mid-pour. PAUSED reads it immediately
     and goes back to DISPENSING **with no bottle**, so water pours onto the drip tray.
  2. A user who lifts the bottle before tapping FINISH never sees the change prompt:
     THANK YOU dismisses itself on the first pass.
  3. A CONFIRM pressed outside ACCEPTING ends the **next** user's coin entry after
     their first coin.

  **The checkbox *Reproduce src one-shot input defects* is ticked by default.** Untick
  it before a client demo of the bottle-removal behaviour. Unticking applies the
  proposed fix: clear all one-shot input flags inside `transition_to()`.
- **P-2 · In-flight coin credited twice by the spec as written.** §3.3 says to credit
  the coin's value on boot, but `accept_pending_coin()` writes the open transaction
  straight after marking the coin, so the restored credit already includes it. The
  simulator credits once. Part C needs a ruling.
- **P-3 · `LEGAL[]` lacks AWAITING_BOTTLE → SELECTING.** A DEBUG build halts when the
  user presses Back on INSERT BOTTLE. The trigger is also logged as "confirm pressed".
  The simulator behaves as a release build and flags the transition in red.
- **P-4 · Power cut mid-pour or mid-payout.** `dispensed_ml` is never persisted, so a
  resume after a cut mid-pour forfeits the whole selection price. A cut during
  PAYING_CHANGE resumes with the full credit after a completed leg has already left the
  hoppers. Both come from reading the code; the self-test does not exercise them.
- **P-5 · CHANGE JAM leaves the transaction open.** After Admin clears the jam, the
  next reboot resumes the jammed transaction and offers its whole credit again, although
  part of it was already paid. Exercised by the self-test.
- **P-6 · Mixed timestamp epochs in history.** Jam, admin-edit and unrouted-coin entries
  are stamped with `millis()/1000` (seconds since boot). Sales are stamped from the RTC.
  Admin shows the former as `boot+Ns`.
- **P-7 · A fault latched while idle waits for the next STANDBY entry.** A pump overrun
  in STANDBY lets the next user start a transaction, and the machine locks after it.
  Separately, `s_pump_overrun` never resets, so the pump fault cannot latch a second
  time before a reboot. From reading the code.

### Modelling simplifications

- The flow sensor is exact. Sensor tolerance is not simulated, so rounding only shows up
  through the timing of a bottle removal.
- The tank level is set by hand. The pump runs and stops on the floats, but it does not
  fill the tank.
- Hoppers pay 8 coins/s after a 200 ms spin-up. Acceptor pulses are 40 ms apart.
- The diverter is modelled only as a destination plus the 900 ms lockout. Fabrication
  drawing Rev B replaces the servo with two cascaded gates; neither mechanism is drawn.
- Each EEPROM ring is modelled as its newest value plus its write count. Slot position,
  CRC framing and cell wear are covered by `test_eeprom` instead.
- No midnight rollover: `src/` does not call `persist_daily_rollover()` yet.
- `millis()` does not wrap.
