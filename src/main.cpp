#include <Arduino.h>

#include "config.h"
#include "types.h"

#include "coin_acceptor.h"
#include "coin_diverter.h"
#include "coin_hopper.h"
#include "flow.h"
#include "bottle.h"
#include "water_level.h"
#include "dispense.h"
#include "billing.h"
#include "persist.h"
#include "hmi.h"
#include "faults.h"
#include "rtc.h"

// Single non-blocking state machine.
// Project EMX-2026-WATERVENDO-01, eMathrix Technologies.
//
// Milestone 5 Part A: states, transitions and the transition choke point.
// The money path is wired in Part B; every place it belongs is marked
// TODO(M5-B). Screens are Milestone 6 and hmi stays stubbed.
//
// RULES THIS FILE ENFORCES:
//
//   - No delay() outside setup(). A blocked loop is a missed coin pulse.
//   - No module owns the loop. Every module gets its update() call every pass,
//     unconditionally, whatever state the machine is in. A module that stops
//     being polled stops debouncing, stops timing out, and stops noticing that
//     its hopper jammed.
//   - Transitions live here and nowhere else. A module reports what happened;
//     it does not decide what the machine does next.
//   - NOTHING assigns s_state except transition_to(). That function is the
//     single choke point for SPEC 2.3 -- see the block comment on it.
//   - No dynamic allocation anywhere. Fixed buffers and char[] only.

// ---------------------------------------------------------------------------
// Triggers -- why a transition happened
// ---------------------------------------------------------------------------
//
// Carried purely so the DEBUG log says what caused a move rather than only that
// one occurred. "PAYING_CHANGE -> FAULT" is two entirely different faults
// depending on the trigger, and telling them apart from a serial log is the
// difference between a five-minute diagnosis and an afternoon.

enum trigger_t : uint8_t {
  TRIG_BOOT_CLEAN = 0,
  TRIG_BOOT_OPEN_TXN,
  TRIG_BOOT_PERSISTENT_FAULT,
  TRIG_FIRST_COIN,
  TRIG_ADMIN_GESTURE,
  TRIG_CREDIT_AT_CEILING,
  TRIG_CONFIRM_PRESSED,
  TRIG_TARGET_CHOSEN,
  TRIG_FINISH_NO_POUR,
  TRIG_BOTTLE_DETECTED,
  TRIG_BOTTLE_WAIT_TIMEOUT,
  TRIG_TARGET_REACHED,
  TRIG_BOTTLE_REMOVED,
  TRIG_FLOW_STALL,
  TRIG_BOTTLE_REPLACED,
  TRIG_GRACE_EXPIRED,
  TRIG_TAIL_ELAPSED,
  TRIG_DISPENSE_MORE,
  TRIG_FINISH,
  TRIG_CREDIT_ZERO,
  TRIG_PAYOUT_CONFIRMED,
  TRIG_PAYOUT_SHORT,
  TRIG_NOTHING_DUE,
  TRIG_LATCHED_FAULT_RELEASED,
  TRIG_FAULT_RAISED_WHILE_IDLE,
  TRIG_THANKYOU_DONE,
  TRIG_FAULT_CLEARED,
  TRIG_ADMIN_EXIT
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static state_t s_state = STATE_BOOT;

// When the current state was entered. Every timeout in the machine is measured
// against this rather than against a per-state timer, so a state cannot leave a
// stale timer running behind it.
static uint32_t s_state_since = 0;

static uint32_t state_elapsed() {
  return (uint32_t)(millis() - s_state_since);
}

// ---------------------------------------------------------------------------
// The legal transition table -- SPEC 2.2
// ---------------------------------------------------------------------------
//
// DEBUG builds only. A transition that is not in this table is a PROGRAMMING
// ERROR, not a runtime condition, so it is caught on the bench rather than
// carried into a release build as a check that costs flash on every pass.
//
// Self-transitions (ACCEPTING -> ACCEPTING on each further coin) are NOT
// listed and never call transition_to(). Re-entering a state would re-run its
// on_enter(), which for ACCEPTING would reset the transaction it is in the
// middle of. Credit accumulates inside update() instead.

#ifdef DEBUG
struct transition_t {
  uint8_t from;
  uint8_t to;
};

static const transition_t LEGAL[] PROGMEM = {
  { STATE_BOOT,            STATE_STANDBY },
  { STATE_BOOT,            STATE_COMPLETE },        // open txn resumed
  { STATE_BOOT,            STATE_FAULT },           // persistent fault in EEPROM

  { STATE_STANDBY,         STATE_ACCEPTING },
  { STATE_STANDBY,         STATE_ADMIN },
  { STATE_STANDBY,         STATE_FAULT },           // incl. Case 20 path (d)

  { STATE_ACCEPTING,       STATE_SELECTING },

  { STATE_SELECTING,       STATE_AWAITING_BOTTLE },
  { STATE_SELECTING,       STATE_PAYING_CHANGE },   // finish without pour

  { STATE_AWAITING_BOTTLE, STATE_DISPENSING },
  { STATE_AWAITING_BOTTLE, STATE_PAYING_CHANGE },   // timeout, full credit

  { STATE_DISPENSING,      STATE_SETTLING },        // target reached OR stall
  { STATE_DISPENSING,      STATE_PAUSED },

  { STATE_PAUSED,          STATE_DISPENSING },
  { STATE_PAUSED,          STATE_SETTLING },

  { STATE_SETTLING,        STATE_COMPLETE },
  { STATE_SETTLING,        STATE_PAYING_CHANGE },   // stall latched

  { STATE_COMPLETE,        STATE_SELECTING },
  { STATE_COMPLETE,        STATE_PAYING_CHANGE },

  { STATE_PAYING_CHANGE,   STATE_THANK_YOU },
  { STATE_PAYING_CHANGE,   STATE_FAULT },           // jam, or a released latch

  { STATE_THANK_YOU,       STATE_STANDBY },

  { STATE_FAULT,           STATE_STANDBY },
  { STATE_FAULT,           STATE_ADMIN },           // see the note below

  { STATE_ADMIN,           STATE_STANDBY }
};
#define LEGAL_COUNT (sizeof(LEGAL) / sizeof(LEGAL[0]))

// NOTE ON FAULT -> ADMIN, raised rather than assumed:
//
// SPEC 2.2 lists "FAULT | admin clear, persistent fault | STANDBY" but gives no
// transition INTO admin from a fault, while SPEC 8 puts "clear persistent
// fault" behind the Admin page. The operator therefore has to be able to reach
// Admin from a locked machine or a persistent fault could never be cleared at
// all. Implemented as FAULT -> ADMIN -> STANDBY. Flagged in the Part A report;
// SPEC 2.2 should gain the row.

static bool transition_is_legal(state_t from, state_t to) {
  for (uint8_t i = 0; i < LEGAL_COUNT; i++) {
    const uint8_t f = pgm_read_byte(&LEGAL[i].from);
    const uint8_t t = pgm_read_byte(&LEGAL[i].to);
    if (f == (uint8_t)from && t == (uint8_t)to) return true;
  }
  return false;
}

static const char *state_name(state_t s) {
  switch (s) {
    case STATE_BOOT:            return "BOOT";
    case STATE_STANDBY:         return "STANDBY";
    case STATE_ACCEPTING:       return "ACCEPTING";
    case STATE_SELECTING:       return "SELECTING";
    case STATE_AWAITING_BOTTLE: return "AWAITING_BOTTLE";
    case STATE_DISPENSING:      return "DISPENSING";
    case STATE_PAUSED:          return "PAUSED";
    case STATE_SETTLING:        return "SETTLING";
    case STATE_COMPLETE:        return "COMPLETE";
    case STATE_PAYING_CHANGE:   return "PAYING_CHANGE";
    case STATE_THANK_YOU:       return "THANK_YOU";
    case STATE_FAULT:           return "FAULT";
    case STATE_ADMIN:           return "ADMIN";
    default:                    return "?";
  }
}

static const char *trigger_name(trigger_t t) {
  switch (t) {
    case TRIG_BOOT_CLEAN:              return "restore ok";
    case TRIG_BOOT_OPEN_TXN:           return "open txn restored";
    case TRIG_BOOT_PERSISTENT_FAULT:   return "persistent fault in eeprom";
    case TRIG_FIRST_COIN:              return "first coin accepted";
    case TRIG_ADMIN_GESTURE:           return "admin gesture";
    case TRIG_CREDIT_AT_CEILING:       return "credit at ceiling";
    case TRIG_CONFIRM_PRESSED:         return "confirm pressed";
    case TRIG_TARGET_CHOSEN:           return "target chosen";
    case TRIG_FINISH_NO_POUR:          return "finish without pour";
    case TRIG_BOTTLE_DETECTED:         return "bottle detected";
    case TRIG_BOTTLE_WAIT_TIMEOUT:     return "bottle wait timeout";
    case TRIG_TARGET_REACHED:          return "target reached";
    case TRIG_BOTTLE_REMOVED:          return "bottle removed";
    case TRIG_FLOW_STALL:              return "flow stall (latched)";
    case TRIG_BOTTLE_REPLACED:         return "bottle replaced in grace";
    case TRIG_GRACE_EXPIRED:           return "grace expired";
    case TRIG_TAIL_ELAPSED:            return "settle tail elapsed";
    case TRIG_DISPENSE_MORE:           return "user chose dispense more";
    case TRIG_FINISH:                  return "user chose finish";
    case TRIG_CREDIT_ZERO:             return "credit exhausted";
    case TRIG_PAYOUT_CONFIRMED:        return "payout counted out";
    case TRIG_PAYOUT_SHORT:            return "payout short after retries";
    case TRIG_NOTHING_DUE:             return "no change due";
    case TRIG_LATCHED_FAULT_RELEASED:  return "latched fault released";
    case TRIG_FAULT_RAISED_WHILE_IDLE: return "fault raised while idle";
    case TRIG_THANKYOU_DONE:           return "thank you dismissed";
    case TRIG_FAULT_CLEARED:           return "fault cleared";
    case TRIG_ADMIN_EXIT:              return "admin exit";
    default:                           return "?";
  }
}
#endif  // DEBUG

// ---------------------------------------------------------------------------
// Per-state hooks
// ---------------------------------------------------------------------------

static void on_enter(state_t s);
static void on_exit(state_t s);
static void state_update(state_t s);

// =========================================================================
// THE TRANSITION CHOKE POINT.
//
// Nothing else in this firmware assigns s_state. Everything a transition has to
// do -- the acceptor rule, the exit hook, the timer, the log, the enter hook --
// happens here, in this order, once.
//
// SPEC 2.3 IS ENFORCED HERE AND NOWHERE ELSE. The acceptor is enabled in
// STANDBY and ACCEPTING and inhibited in every other state. Scattering
// inhibit/uninhibit calls through the individual states is how a machine ends
// up accepting a coin in PAYING_CHANGE: it only takes one state that forgot,
// and the states that forget are the ones added later by someone who did not
// know the rule existed.
//
// Deriving the acceptor's state from the DESTINATION rather than tracking it
// means a new state added tomorrow is inhibited by default. It has to be named
// in accepting_state() to take money, which is the correct direction for the
// default to fail in.
// =========================================================================

static bool accepting_state(state_t s) {
  return s == STATE_STANDBY || s == STATE_ACCEPTING;
}

static void transition_to(state_t next, trigger_t why) {
  (void)why;   // used only by the DEBUG log below

  if (next == s_state) return;   // self-transitions do not re-enter

#ifdef DEBUG
  if (!transition_is_legal(s_state, next)) {
    // A transition outside SPEC 2.2 is a programming error, not something the
    // machine should try to carry on through. Stop loudly on the bench so it
    // is fixed here rather than discovered in a cabinet full of coins.
    Serial.print(F("[FSM] ILLEGAL TRANSITION "));
    Serial.print(state_name(s_state));
    Serial.print(F(" -> "));
    Serial.print(state_name(next));
    Serial.print(F(" ("));
    Serial.print(trigger_name(why));
    Serial.println(F(") -- HALTED"));
    Serial.flush();

    // Inhibit before halting. Even a halted machine must not take money.
    coin_acceptor_inhibit();
    for (;;) { }
  }

  Serial.print(F("[FSM] "));
  Serial.print(state_name(s_state));
  Serial.print(F(" -> "));
  Serial.print(state_name(next));
  Serial.print(F("  ("));
  Serial.print(trigger_name(why));
  Serial.println(F(")"));
#endif

  on_exit(s_state);

  s_state = next;
  s_state_since = millis();

  // The acceptor rule, applied from the destination. Before on_enter(), so a
  // state's entry code can never observe the acceptor in the wrong condition.
  if (accepting_state(next)) {
    coin_acceptor_uninhibit();
  } else {
    coin_acceptor_inhibit();
  }

  on_enter(next);
}

// ---------------------------------------------------------------------------
// State entry / exit
// ---------------------------------------------------------------------------

static void on_enter(state_t s) {
  switch (s) {
    case STATE_STANDBY:
      // TODO(M5-B): billing_reset() and persist_txn_close() once the money path
      // is wired. Resetting here rather than on exit from THANK_YOU means an
      // abandoned transaction is also cleared down.
      // TODO(M5-C): faults_release_latched() -- Case 20 path (d), a fault
      // latched while nothing was owed must lock the machine immediately.
      break;

    case STATE_ACCEPTING:
      // TODO(M5-B): open the EEPROM transaction record.
      break;

    case STATE_AWAITING_BOTTLE:
      // TODO(M5-C): buzzer ladder at BOTTLE_WAIT_WARN1_MS / WARN2_MS.
      break;

    case STATE_DISPENSING:
      // TODO(M5-B): dispense_start(target) on first entry, dispense_resume()
      // when arriving from PAUSED. These are NOT the same call -- resume must
      // continue toward the original target from the volume already delivered.
      break;

    case STATE_PAUSED:
      // TODO(M5-B): dispense_pause().
      break;

    case STATE_PAYING_CHANGE:
      // TODO(M5-B): change_plan() then the hopper payout.
      break;

    case STATE_FAULT:
      // The acceptor is already inhibited -- faults_raise() did it before it
      // returned, and transition_to() did it again from the destination. Both
      // are deliberate: neither is allowed to be the only one.
      // TODO(M6): hmi_showFault(faults_active()).
      break;

    case STATE_BOOT:
    case STATE_SELECTING:
    case STATE_SETTLING:
    case STATE_COMPLETE:
    case STATE_THANK_YOU:
    case STATE_ADMIN:
    default:
      break;
  }
}

static void on_exit(state_t s) {
  switch (s) {
    case STATE_DISPENSING:
      // Nothing here yet. The valve is closed by the destination state rather
      // than on exit, because SETTLING deliberately keeps counting the tail.
      break;

    case STATE_THANK_YOU:
      // TODO(M5-B): commit the transaction to EEPROM and the history ring,
      // add the daily totals, then billing_reset().
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Per-state update
// ---------------------------------------------------------------------------

static void state_update(state_t s) {
  switch (s) {

    // -------------------------------------------------------------------
    case STATE_BOOT:
      // Reconciliation runs in setup(); by the time loop() sees BOOT the
      // decision has been made. Kept as a real state so the transition out of
      // it is logged like every other.
      transition_to(STATE_STANDBY, TRIG_BOOT_CLEAN);
      break;

    // -------------------------------------------------------------------
    case STATE_STANDBY:
      if (faults_is_locked()) {
        transition_to(STATE_FAULT, TRIG_FAULT_RAISED_WHILE_IDLE);
        break;
      }
      // TODO(M5-C): admin gesture -> STATE_ADMIN.
      if (coin_acceptor_available()) {
        // TODO(M5-B): the LOW CHANGE gate runs HERE, before the coin is
        // credited -- SPEC 6.1.1. If the hoppers cannot cover
        // billing_worst_case_change(), raise LOW CHANGE and stay put.
        transition_to(STATE_ACCEPTING, TRIG_FIRST_COIN);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_ACCEPTING:
      // TODO(M5-B): consume the coin, billing_add_coin(), route it through the
      // diverter. Further coins are handled here rather than by re-entering
      // this state.
      if (billing_at_ceiling()) {
        transition_to(STATE_SELECTING, TRIG_CREDIT_AT_CEILING);
        break;
      }
      // TODO(M5-C): confirm button -> SELECTING.
      break;

    // -------------------------------------------------------------------
    case STATE_SELECTING:
      // TODO(M5-B): read the HMI selection, billing_can_select(),
      // billing_select(), then AWAITING_BOTTLE.
      // TODO(M5-B): "finish without pour" -> PAYING_CHANGE with full credit.
      break;

    // -------------------------------------------------------------------
    case STATE_AWAITING_BOTTLE:
      if (bottle_present()) {
        transition_to(STATE_DISPENSING, TRIG_BOTTLE_DETECTED);
        break;
      }
      // Silent to 15 s, buzzer, buzzer, then cancel and refund in full. The
      // user gets two audible warnings before losing the transaction.
      if (state_elapsed() >= BOTTLE_WAIT_CANCEL_MS) {
        transition_to(STATE_PAYING_CHANGE, TRIG_BOTTLE_WAIT_TIMEOUT);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_DISPENSING:
      // Stall is checked before the target: a stalled pour will never reach
      // its target, and the water is already off by the time dispense reports
      // it. The fault is LATCHED, not raised -- SPEC 9 invariant 8. The user
      // is owed money and is standing there.
      if (dispense_status() == DISPENSE_STALLED) {
        faults_latch(FAULT_FLOW_STALL);
        transition_to(STATE_SETTLING, TRIG_FLOW_STALL);
        break;
      }
      if (bottle_just_removed()) {
        transition_to(STATE_PAUSED, TRIG_BOTTLE_REMOVED);
        break;
      }
      if (dispense_status() == DISPENSE_COMPLETE) {
        transition_to(STATE_SETTLING, TRIG_TARGET_REACHED);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_PAUSED:
      if (bottle_just_placed()) {
        transition_to(STATE_DISPENSING, TRIG_BOTTLE_REPLACED);
        break;
      }
      if (state_elapsed() >= BOTTLE_REMOVED_GRACE_MS) {
        transition_to(STATE_SETTLING, TRIG_GRACE_EXPIRED);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_SETTLING:
      // The valve is shut and the in-flight tail is still being counted toward
      // what the user received. dispense_update() ends the window itself.
      if (state_elapsed() < VALVE_CLOSE_SETTLE_MS) break;

      // TODO(M5-B): settle the pour against billing here -- partial or
      // complete -- BEFORE leaving, so both exits below settle identically.

      if (faults_has_latched()) {
        // Settle first, lock second. Straight to the payout; the fault is
        // released once the money is physically out.
        transition_to(STATE_PAYING_CHANGE, TRIG_TAIL_ELAPSED);
        break;
      }
      transition_to(STATE_COMPLETE, TRIG_TAIL_ELAPSED);
      break;

    // -------------------------------------------------------------------
    case STATE_COMPLETE:
      if (billing_credit() <= 0) {
        transition_to(STATE_PAYING_CHANGE, TRIG_CREDIT_ZERO);
        break;
      }
      // TODO(M5-B): HMI choice -- dispense more -> SELECTING, finish ->
      // PAYING_CHANGE.
      break;

    // -------------------------------------------------------------------
    case STATE_PAYING_CHANGE: {
      // TODO(M5-B): drive change_plan() and the two hopper legs. For Part A
      // the payout is treated as instantly complete so the rest of the cycle
      // can be walked end to end.
      const bool payout_done = !coin_hopper_is_busy();
      if (!payout_done) break;

      if (coin_hopper_status() == PAYOUT_JAMMED) {
        // The machine has physically demonstrated it cannot pay. This is the
        // ONE fault that locks immediately rather than deferring -- there is
        // no payout left to protect and waiting would only take more money.
        faults_raise(FAULT_CHANGE_JAM);
        transition_to(STATE_FAULT, TRIG_PAYOUT_SHORT);
        break;
      }

      if (faults_has_latched()) {
        // The money is out. NOW the machine may lock.
        faults_release_latched();
        transition_to(STATE_FAULT, TRIG_LATCHED_FAULT_RELEASED);
        break;
      }

      transition_to(STATE_THANK_YOU,
                    billing_change_due() > 0 ? TRIG_PAYOUT_CONFIRMED
                                             : TRIG_NOTHING_DUE);
      break;
    }

    // -------------------------------------------------------------------
    case STATE_THANK_YOU:
      // Held long enough to read the change due and collect it. Bottle removal
      // dismisses it early -- the user has what they came for.
      if (state_elapsed() >= HMI_THANKYOU_HOLD_MS || bottle_just_removed()) {
        transition_to(STATE_STANDBY, TRIG_THANKYOU_DONE);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_FAULT:
      // TODO(M5-C): transient faults clear themselves in faults_update();
      // persistent ones need an Admin clear, which is why FAULT -> ADMIN
      // exists.
      if (!faults_is_locked()) {
        transition_to(STATE_STANDBY, TRIG_FAULT_CLEARED);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_ADMIN:
      // TODO(M5-C): the five Admin functions of SPEC 8.
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------

// Boot trace. DEBUG only.
//
// Every begin() below touches a peripheral, and a peripheral that is missing,
// miswired or held in reset can hang its driver. Without a marker per step, a
// machine that stops during setup() is indistinguishable from one that never
// powered on -- same blank screen, same silence. This turns "it's dead" into
// "it stopped after the diverter", which is the difference between a guess and
// a measurement during bring-up.
#ifdef DEBUG
static void boot_step(const __FlashStringHelper *what) {
  Serial.print(F("[boot] "));
  Serial.println(what);
  Serial.flush();   // so the marker is out BEFORE the call that might hang
}
#else
#define boot_step(x) ((void)0)
#endif

void setup() {
#ifdef DEBUG
  Serial.begin(DEBUG_BAUD);
  Serial.println(F("EMX-2026-WATERVENDO-01 boot [DEBUG BUILD - NOT FOR SERVICE]"));
#endif

  // ---------------------------------------------------------------------
  // Init order below is load-bearing. Do not alphabetise it.
  //
  //   1. persist   -- the inventory, any open transaction and the stored fault
  //                   flags are inputs to every decision the others make.
  //   2. acceptor  -- must own PIN_COIN_INHIBIT (pinMode OUTPUT, asserted)
  //                   BEFORE anything can ask it to inhibit. It boots
  //                   inhibited: taking a coin before the inventory has been
  //                   validated is money the machine may not be able to honour.
  //   3. faults    -- restores persistent faults from EEPROM and inhibits the
  //                   acceptor if any are set, so it needs both of the above.
  // ---------------------------------------------------------------------
  boot_step(F("persist"));       persist_begin();
  boot_step(F("acceptor"));      coin_acceptor_begin();
  boot_step(F("faults"));        faults_begin();

  boot_step(F("rtc"));           rtc_begin();
  boot_step(F("diverter"));      coin_diverter_begin();
  boot_step(F("hopper"));        coin_hopper_begin();
  boot_step(F("flow"));          flow_begin();
  boot_step(F("bottle"));        bottle_begin();
  boot_step(F("water level"));   water_level_begin();
  boot_step(F("dispense"));      dispense_begin();
  boot_step(F("billing"));       billing_begin();
  boot_step(F("hmi"));           hmi_begin();
  boot_step(F("ready"));

  // TODO(M5-C): boot reconciliation.
  //   - An in-flight coin recorded but never confirmed routed goes through
  //     persist_reconcile_unrouted_coin(). Credit the user, assume the profit
  //     chamber, tag the event.
  //   - An open transaction is restored and resumed at COMPLETE with its
  //     remaining balance, rather than dropping to Standby and swallowing the
  //     user's money.
  //   - A persistent fault re-raised by faults_begin() sends the machine to
  //     FAULT before it can take a coin.
  //   - A virgin or corrupt EEPROM raises LOW CHANGE: the machine does not know
  //     what is in its hoppers, and the honest answer to unknown inventory is
  //     to refuse money.

  s_state = STATE_BOOT;
  s_state_since = millis();
}

void loop() {
#ifdef DEBUG
  // Watchdog on the no-blocking-calls rule.
  //
  // Every module below is polled, and two of them -- the hopper outlet counter
  // and the bottle sensor -- depend on being polled faster than their debounce
  // window to work at all. A slow pass is not a performance nit here: it drops
  // a counted coin, and a dropped count is a false jam under a paying user.
  //
  // DEBUG only. This costs a micros() pair and a comparison, and nothing that
  // handles money runs with serial output enabled.
  const uint32_t loop_start_us = micros();
#endif

  // Every module, every pass, unconditionally.
  persist_update();
  rtc_update();
  coin_acceptor_update();
  coin_diverter_update();
  coin_hopper_update();
  flow_update();
  bottle_update();
  water_level_update();
  dispense_update();
  hmi_update();
  faults_update();

  state_update(s_state);

#ifdef DEBUG
  {
    const uint32_t elapsed_us = micros() - loop_start_us;
    if (elapsed_us > LOOP_WARN_US) {
      // Rate-limited: a slow pass is usually slow for many passes in a row, and
      // printing on every one of them would itself become the blocking call.
      static uint32_t last_warn_ms = 0;
      const uint32_t now = millis();
      if ((uint32_t)(now - last_warn_ms) >= 1000) {
        last_warn_ms = now;
        Serial.print(F("[SLOW LOOP] "));
        Serial.print(elapsed_us);
        Serial.println(F(" us"));
      }
    }
  }
#endif
}
