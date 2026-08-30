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
#include "change_plan.h"
#include <string.h>
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
static state_t s_prev_state = STATE_BOOT;

// When the current state was entered. Every timeout in the machine is measured
// against this rather than against a per-state timer, so a state cannot leave a
// stale timer running behind it.
static uint32_t s_state_since = 0;

static uint32_t state_elapsed() {
  return (uint32_t)(millis() - s_state_since);
}

// The volume the user selected, in millilitres. Set by billing_select(), which
// has already taken its price out of credit before this is written.
static volume_t s_target_ml = 0;

// The payout in progress. Planned once on entering PAYING_CHANGE and then run
// as two sequential hopper legs -- coin_hopper_dispense() drives one hopper at
// a time and refuses a second while busy.
static uint16_t s_pay_p1 = 0;
static uint16_t s_pay_p5 = 0;
static uint8_t  s_pay_leg = 0;      // 0 = P5 leg, 1 = P1 leg, 2 = done
static money_t  s_change_paid = 0;  // held for the Thank You summary

// ---------------------------------------------------------------------------
// Confirm button
// ---------------------------------------------------------------------------
//
// One momentary input, owned by the state machine. A whole module for a single
// button would be ceremony; the debounce is four lines and lives here next to
// the transitions it drives.

static bool s_confirm_state = false;
static bool s_confirm_candidate = false;
static uint32_t s_confirm_since = 0;
static bool s_confirm_pressed = false;   // one-shot, consumed by reading

static void confirm_update() {
  const bool raw = (digitalRead(PIN_CONFIRM_BTN) == LOW);
  const uint32_t now = millis();

  if (raw != s_confirm_candidate) {
    s_confirm_candidate = raw;
    s_confirm_since = now;
    return;
  }
  if (raw == s_confirm_state) return;
  if ((uint32_t)(now - s_confirm_since) < CONFIRM_DEBOUNCE_MS) return;

  s_confirm_state = raw;
  if (raw) s_confirm_pressed = true;   // press edge only, not release
}

static bool confirm_take() {
  const bool p = s_confirm_pressed;
  s_confirm_pressed = false;
  return p;
}

// ---------------------------------------------------------------------------
// Coin acceptance
// ---------------------------------------------------------------------------

// Credit and route one pending coin, if there is one and the chute is clear.
//
// ORDER: credit first, then route. billing_add_coin() cannot fail in a way that
// loses the coin, whereas coin_diverter_route() asserts the acceptor window and
// writes the in-flight marker -- so crediting afterwards would leave a gap in
// which a power cut costs the user money the machine never recorded.
static void accept_pending_coin() {
  if (!coin_acceptor_available()) return;

  // Never credit a second coin while the servo is still travelling. The
  // diverter refuses an overlapping route anyway; this stops the coin being
  // consumed and silently dropped when it does.
  if (coin_diverter_is_busy()) return;

  const coin_t c = coin_acceptor_take_coin();
  if (c == COIN_NONE) return;

  billing_add_coin(c);
  coin_diverter_route(c);

  // Persist the new credit immediately. A power cut between the coin landing
  // and the transaction ending must not cost the user their money -- SPEC 3.3
  // and scenarios.md case 12.
  transaction_t txn;
  billing_store(&txn);
  persist_txn_update(&txn);
}

// Write the transaction to the history ring and the daily totals.
//
// Called ONLY after the payout has been confirmed by the outlet counts. A
// record written at transaction start and then contradicted by a jam claims
// money left the machine when it did not, which is exactly the discrepancy a
// technician cannot explain later.
static void commit_transaction() {
  history_entry_t e;
  memset(&e, 0, sizeof(e));
  e.timestamp  = rtc_timestamp();   // RTC_TIMESTAMP_INVALID if the clock failed
  e.tag        = EVT_TRANSACTION;
  e.amount_in  = billing_inserted();
  e.volume_out = billing_total_dispensed();
  e.change_out = s_change_paid;
  persist_history_add(&e);

  // Profit is what the user was actually charged: inserted less change paid.
  persist_daily_add(billing_inserted() - s_change_paid,
                    billing_total_dispensed());

  persist_txn_close();
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

  s_prev_state = s_state;
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
      // Clear down for the next user. Done on ENTRY to standby rather than on
      // exit from thank-you, so a transaction abandoned by any route is also
      // cleared rather than leaking credit into the next user's session.
      billing_reset();
      s_target_ml = 0;
      s_change_paid = 0;

      // Case 20 path (d): a fault latched while nothing was owed. There was no
      // payout to hang the release on, so it happens here. Without this the
      // latch sits forever and the safety mechanism is decorative.
      faults_release_latched();
      break;

    case STATE_ACCEPTING: {
      // Open the EEPROM transaction record. From here on a power cut is
      // recoverable -- scenarios.md case 12.
      transaction_t txn;
      billing_store(&txn);
      persist_txn_open(&txn);
      break;
    }

    case STATE_AWAITING_BOTTLE:
      // TODO(M5-C): buzzer ladder at BOTTLE_WAIT_WARN1_MS / WARN2_MS.
      break;

    case STATE_DISPENSING:
      // start() and resume() are NOT interchangeable. resume() continues toward
      // the ORIGINAL target from the volume already delivered; start() would
      // reset the count and hand out a second full measure of free water on
      // every replaced bottle.
      if (s_prev_state == STATE_PAUSED) {
        dispense_resume();
      } else {
        dispense_start(s_target_ml);
      }
      break;

    case STATE_PAUSED:
      // Valve shut immediately, delivered volume held. The grace countdown is
      // this state's elapsed timer, not the module's business.
      dispense_pause();
      break;

    case STATE_PAYING_CHANGE: {
      s_pay_p1 = 0;
      s_pay_p5 = 0;
      s_pay_leg = 2;          // assume nothing to do until a plan says otherwise
      s_change_paid = 0;

      const money_t due = billing_change_due();
      if (due <= 0) break;    // nothing owed; PAYING_CHANGE falls straight through

      change_plan_t plan;
      if (!change_plan(due, coin_hopper_count(HOPPER_P1),
                       coin_hopper_count(HOPPER_P5), &plan)) {
        // Should be unreachable: the LOW CHANGE gate guaranteed coverage for
        // the full ceiling before the first coin was accepted. If it happens
        // anyway the machine cannot pay what it owes, which is a jam by any
        // other name -- and locking is the honest answer.
        faults_raise(FAULT_CHANGE_JAM);
        break;
      }

      s_pay_p1 = plan.p1;
      s_pay_p5 = plan.p5;
      s_pay_leg = 0;
      break;
    }

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
      // Nothing. The transaction was committed in PAYING_CHANGE, once the
      // payout was confirmed by the outlet counts, and billing_reset() happens
      // on entry to STANDBY so that every route out of a transaction clears
      // down rather than only this one.
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

      // ---------------------------------------------------------------
      // THE LOW CHANGE GATE -- SPEC 6.1.1.
      //
      // Checked against the worst case for the FULL CEILING, not against the
      // coin about to be inserted. A user who puts in P1 and then P19 more
      // must not discover mid-transaction that the machine cannot make their
      // change. Passing this gate is the machine's guarantee that whatever
      // the user does next, it can be completed.
      //
      // Evaluated here, in STANDBY, and nowhere else. Re-checking during a
      // transaction is invariant 8 from the other direction: once the money
      // is in, refusing is no longer an option.
      // ---------------------------------------------------------------
      if (!coin_hopper_can_cover(billing_worst_case_change())) {
        faults_raise(FAULT_LOW_CHANGE);
        transition_to(STATE_FAULT, TRIG_FAULT_RAISED_WHILE_IDLE);
        break;
      }

      // TODO(M5-C): admin gesture -> STATE_ADMIN.
      if (coin_acceptor_available()) {
        transition_to(STATE_ACCEPTING, TRIG_FIRST_COIN);
        // The coin itself is credited by ACCEPTING on the next pass. Crediting
        // here would happen before on_enter() opened the EEPROM record.
      }
      break;

    // -------------------------------------------------------------------
    case STATE_ACCEPTING:
      // Further coins are handled here rather than by re-entering the state --
      // re-entry would re-run on_enter() and reopen the transaction record.
      accept_pending_coin();

      if (billing_at_ceiling()) {
        // The acceptor is inhibited by the transition itself: SELECTING is not
        // an accepting state. The machine stops taking money it has capped.
        transition_to(STATE_SELECTING, TRIG_CREDIT_AT_CEILING);
        break;
      }
      if (confirm_take()) {
        transition_to(STATE_SELECTING, TRIG_CONFIRM_PRESSED);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_SELECTING: {
      int32_t payload = 0;
      const hmi_event_t e = hmi_take_event(&payload);

      if (e == HMI_EVENT_SELECT_VOLUME) {
        // billing_can_select() enforces the whole rule set: positive, within
        // the ceiling, a whole 100 mL step, and affordable. The price is then
        // deducted from the SELECTION, before the valve opens, and nothing the
        // flow sensor later reports can change it.
        if (billing_can_select((volume_t)payload)) {
          s_target_ml = billing_select((volume_t)payload);

          transaction_t txn;
          billing_store(&txn);
          persist_txn_update(&txn);

          transition_to(STATE_AWAITING_BOTTLE, TRIG_TARGET_CHOSEN);
        }
        // An unaffordable selection is simply not actioned. The greyed-out
        // option should have prevented it reaching here at all.
        break;
      }

      // The back arrow from SELECT VOLUME means "I'm done, give me my money".
      // Same destination as the explicit finish button.
      if (e == HMI_EVENT_FINISH || e == HMI_EVENT_BACK) {
        transition_to(STATE_PAYING_CHANGE, TRIG_FINISH_NO_POUR);
      }
      break;
    }

    // -------------------------------------------------------------------
    case STATE_AWAITING_BOTTLE: {
      if (bottle_present()) {
        transition_to(STATE_DISPENSING, TRIG_BOTTLE_DETECTED);
        break;
      }

      // Back from INSERT BOTTLE cancels the selection. Nothing has poured, so
      // the whole selection price returns to credit with no rounding.
      int32_t payload = 0;
      if (hmi_take_event(&payload) == HMI_EVENT_BACK) {
        billing_cancel_selection();
        s_target_ml = 0;

        transaction_t txn;
        billing_store(&txn);
        persist_txn_update(&txn);

        transition_to(STATE_SELECTING, TRIG_CONFIRM_PRESSED);
        break;
      }
      // Silent to 15 s, buzzer, buzzer, then cancel and refund in full. The
      // user gets two audible warnings before losing the transaction.
      if (state_elapsed() >= BOTTLE_WAIT_CANCEL_MS) {
        // Cancelled. The selection never poured, so its price goes back to
        // credit in full before the payout is planned -- otherwise the user
        // would be refunded only what they had left after paying for water
        // they never received.
        billing_cancel_selection();
        s_target_ml = 0;
        transition_to(STATE_PAYING_CHANGE, TRIG_BOTTLE_WAIT_TIMEOUT);
      }
      break;
    }

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

      // ---------------------------------------------------------------
      // The pour is settled HERE, before either exit, so both routes out of
      // SETTLING settle identically. Putting it in the destinations would mean
      // two copies of the money arithmetic that could drift apart.
      //
      // This is the ONE place a measured volume touches money, and
      // billing_settle_partial() rounds DOWN before pricing it.
      // ---------------------------------------------------------------
      {
        const volume_t delivered = dispense_delivered();
        if (delivered >= s_target_ml && s_target_ml > 0) {
          billing_settle_complete(delivered);
        } else {
          billing_settle_partial(delivered);
        }
        dispense_clear();
        s_target_ml = 0;

        transaction_t txn;
        billing_store(&txn);
        persist_txn_update(&txn);
      }

      if (faults_has_latched()) {
        // Settle first, lock second. Straight to the payout; the fault is
        // released once the money is physically out.
        transition_to(STATE_PAYING_CHANGE, TRIG_TAIL_ELAPSED);
        break;
      }
      transition_to(STATE_COMPLETE, TRIG_TAIL_ELAPSED);
      break;

    // -------------------------------------------------------------------
    case STATE_COMPLETE: {
      if (billing_credit() <= 0) {
        transition_to(STATE_PAYING_CHANGE, TRIG_CREDIT_ZERO);
        break;
      }
      int32_t payload = 0;
      const hmi_event_t e = hmi_take_event(&payload);
      if (e == HMI_EVENT_DISPENSE_AGAIN) {
        transition_to(STATE_SELECTING, TRIG_DISPENSE_MORE);
      } else if (e == HMI_EVENT_FINISH || e == HMI_EVENT_BACK) {
        transition_to(STATE_PAYING_CHANGE, TRIG_FINISH);
      }
      break;
    }

    // -------------------------------------------------------------------
    case STATE_PAYING_CHANGE: {
      // A change jam raised on entry (the plan could not be made) locks here
      // without running any leg.
      if (faults_is_locked() && faults_active() == FAULT_CHANGE_JAM) {
        transition_to(STATE_FAULT, TRIG_PAYOUT_SHORT);
        break;
      }

      if (coin_hopper_is_busy()) break;

      // Reap a finished leg before starting the next. Inventory has already
      // been decremented by coin_hopper's own finish(), by COUNTED coins --
      // never by commanded ones. If the sensor did not see it leave, the
      // machine still owns it.
      const payout_result_t r = coin_hopper_status();
      if (r == PAYOUT_JAMMED) {
        s_change_paid += (money_t)coin_hopper_counted() *
                         (s_pay_leg == 0 ? 500 : 100);
        coin_hopper_clear();
        // The machine has physically demonstrated it cannot pay. This is the
        // ONE fault that locks immediately rather than deferring -- there is
        // no payout left to protect and waiting would only take more money.
        faults_raise(FAULT_CHANGE_JAM);
        transition_to(STATE_FAULT, TRIG_PAYOUT_SHORT);
        break;
      }
      if (r == PAYOUT_COMPLETE) {
        s_change_paid += (money_t)coin_hopper_counted() *
                         (s_pay_leg == 0 ? 500 : 100);
        coin_hopper_clear();
        s_pay_leg++;
      }

      // Start the next leg. Two hoppers, run in sequence -- coin_hopper drives
      // one at a time and refuses an overlapping command.
      if (s_pay_leg == 0) {
        if (s_pay_p5 > 0) { coin_hopper_dispense(HOPPER_P5, s_pay_p5); break; }
        s_pay_leg = 1;
      }
      if (s_pay_leg == 1) {
        if (s_pay_p1 > 0) { coin_hopper_dispense(HOPPER_P1, s_pay_p1); break; }
        s_pay_leg = 2;
      }

      // Both legs done. The money is physically out and counted.
      commit_transaction();

      if (faults_has_latched()) {
        // NOW the machine may lock -- SPEC 9 invariant 8. This is also Case 20
        // path (b): with zero change due there was no payout to hang the
        // release on, and the path still reaches here.
        faults_release_latched();
        transition_to(STATE_FAULT, TRIG_LATCHED_FAULT_RELEASED);
        break;
      }

      transition_to(STATE_THANK_YOU,
                    s_change_paid > 0 ? TRIG_PAYOUT_CONFIRMED
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

  // Confirm button. INPUT_PULLUP like every other input, so a broken wire reads
  // as "not pressed" rather than as a permanently held button.
  pinMode(PIN_CONFIRM_BTN, INPUT_PULLUP);
  s_confirm_state = (digitalRead(PIN_CONFIRM_BTN) == LOW);
  s_confirm_candidate = s_confirm_state;
  s_confirm_since = millis();

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
  confirm_update();

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
