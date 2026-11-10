#include <Arduino.h>
#include <Servo.h>

#include "config.h"
#include "types.h"

#include "coin_acceptor.h"
#include "coin_diverter.h"
#include "coin_hopper.h"
#include "coin_level.h"
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
#include "relay.h"

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
  TRIG_ADMIN_EXIT,
  TRIG_STORAGE_FULL
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
// The fill pump was booked to run alongside this pour (tank sensor lost).
static bool     s_refill_booked = false;
// This pour ended because the bottle was taken away and not put back: it is
// charged in full (BOTTLE_REMOVED_CHARGES_FULL), not refunded as a partial.
static bool     s_charge_full = false;
// A hopper leg of THIS payout is running. Without it a finished Admin or console
// test payout reads as this sale's leg: the P5 leg is skipped and the customer
// is short-changed.
static bool     s_leg_running = false;
// The hoppers cannot cover what is due. Credit can reach MAX_CREDIT_PESOS, far
// above the amount the STANDBY gate guarantees, so this is a real case: pay all
// that is there, then record the rest as owed and lock.
static bool     s_pay_short = false;

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
  // The on-screen CONFIRM (or the test console's "c") counts as a press.
  if (hmi_confirm_take()) s_confirm_pressed = true;
  if (!CONFIRM_BTN_FITTED) return;
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

// ---------------------------------------------------------------------------
// Buzzer
// ---------------------------------------------------------------------------
//
// One active buzzer, owned here like the confirm button. A beep is a start
// time, never a delay(): the pin goes high, and buzzer_update() drops it once
// BUZZER_BEEP_MS has passed.

static bool s_buzz_on = false;
static uint32_t s_buzz_since = 0;
static uint8_t s_bottle_warns = 0;   // AWAITING_BOTTLE warnings sounded so far
static bool s_bottle_by_button = false;   // this pour was started by CONFIRM, not the IR

static void buzzer_beep() {
  digitalWrite(PIN_BUZZER, BUZZER_ON_LEVEL);
  s_buzz_on = true;
  s_buzz_since = millis();
}

// Bottle alarm (team 2026-11-10): beeps on repeat in the last seconds before the
// machine gives up on a bottle -- waiting for one after a selection, and after
// one was taken away mid-pour.
static bool bottle_alarm_wanted() {
  const uint32_t t = state_elapsed();
  if (s_state == STATE_AWAITING_BOTTLE) {
    return t >= BOTTLE_WAIT_WARN1_MS && t < BOTTLE_WAIT_CANCEL_MS;
  }
  if (s_state == STATE_PAUSED) {
    return t >= BOTTLE_GRACE_ALARM_MS && t < BOTTLE_REMOVED_GRACE_MS;
  }
  return false;
}

static bool s_alarm_on = false;
static uint32_t s_alarm_ms = 0;

static void buzzer_beep();

static void buzzer_alarm_update(bool wanted) {
  if (!wanted) { s_alarm_on = false; return; }
  if (!s_alarm_on || (uint32_t)(millis() - s_alarm_ms) >= BUZZER_ALARM_PERIOD_MS) {
    s_alarm_on = true;
    s_alarm_ms = millis();
    buzzer_beep();
  }
}

// Test console "buzzdiag": D47 held LOW for 2 s, then HIGH for 2 s, then a
// 2 kHz tone for 2 s, so the team can hear which kind of buzzer is fitted.
static uint8_t s_bdiag_step = 0;   // 0 off, 1 LOW, 2 HIGH, 3 tone
static uint32_t s_bdiag_ms = 0;

void buzzer_diag() {
  s_bdiag_step = 1;
  s_bdiag_ms = millis();
  digitalWrite(PIN_BUZZER, LOW);
}

static void buzzer_diag_update() {
  if (!s_bdiag_step || (uint32_t)(millis() - s_bdiag_ms) < 2000) return;
  s_bdiag_ms = millis();
  if (s_bdiag_step == 1) { digitalWrite(PIN_BUZZER, HIGH); s_bdiag_step = 2; return; }
  if (s_bdiag_step == 2) { tone(PIN_BUZZER, 2000); s_bdiag_step = 3; return; }
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
  s_bdiag_step = 0;
}

// Test console "buzz <n>": n beeps, one every BUZZER_TEST_GAP_MS.
static uint8_t s_buzz_test_left = 0;
static uint32_t s_buzz_test_ms = 0;

void buzzer_test(uint8_t beeps) {
  s_buzz_test_left = beeps > 10 ? 10 : beeps;
  s_buzz_test_ms = millis() - BUZZER_TEST_GAP_MS;
}

static void buzzer_update() {
  if (s_buzz_on && (uint32_t)(millis() - s_buzz_since) >= BUZZER_BEEP_MS) {
    digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
    s_buzz_on = false;
  }
  if (s_buzz_test_left && !s_buzz_on &&
      (uint32_t)(millis() - s_buzz_test_ms) >= BUZZER_TEST_GAP_MS) {
    s_buzz_test_ms = millis();
    s_buzz_test_left--;
    buzzer_beep();
  }
}

// Service servo: all three coin IRs full means nowhere left for any coin. Only
// from STANDBY or FAULT, so a customer mid-sale still gets their water and
// change first (settle first, lock second). Swings back by itself once any IR
// clears. STANDBY also locks on the box IR alone (COIN STORAGE FULL).
#ifndef BENCH_COIN_TEST
static Servo s_service_servo;
static bool s_service_active = false;
static uint32_t s_service_move_ms = 0;

static void service_servo_write(int angle) {
  if (!SERVICE_SERVO_ENABLED) return;
  if (!s_service_servo.attached()) s_service_servo.attach(PIN_SERVICE_SERVO);
  s_service_servo.write(angle);
  s_service_move_ms = millis();
}

// Test console "svc <n>": rest -> active -> rest, n times.
static uint8_t s_service_test_steps = 0;
static uint32_t s_service_test_ms = 0;

void service_servo_test(uint8_t cycles) {
  if (s_service_active || s_service_test_steps || cycles == 0) return;
  if (cycles > 10) cycles = 10;
  s_service_test_steps = (uint8_t)(cycles * 2);
  s_service_test_ms = millis() - SERVICE_SERVO_TEST_STEP_MS;
}

static void service_servo_update() {
  if (!SERVICE_SERVO_ENABLED) return;
  if (s_service_test_steps) {
    if ((uint32_t)(millis() - s_service_test_ms) < SERVICE_SERVO_TEST_STEP_MS) return;
    s_service_test_ms = millis();
    service_servo_write((s_service_test_steps % 2) ? SERVICE_SERVO_REST_DEG
                                                   : SERVICE_SERVO_ACTIVE_DEG);
    s_service_test_steps--;
    return;
  }
  const bool idle = s_state == STATE_STANDBY || s_state == STATE_FAULT;
  const bool want = idle && coin_level_box_full() &&
                    coin_level_hopper_full(HOPPER_P1) &&
                    coin_level_hopper_full(HOPPER_P5);
  if (want != s_service_active) {
    s_service_active = want;
    service_servo_write(want ? SERVICE_SERVO_ACTIVE_DEG : SERVICE_SERVO_REST_DEG);
#ifdef DEBUG
    Serial.println(want ? F("[service servo] all coin IRs full -- moved")
                        : F("[service servo] back to rest"));
#endif
  }
  if (!s_service_active && s_service_servo.attached() &&
      (uint32_t)(millis() - s_service_move_ms) >= GATE_DETACH_MS) {
    s_service_servo.detach();
  }
}
#else
void service_servo_test(uint8_t) {}   // no state machine, no service servo
#endif  // !BENCH_COIN_TEST

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
  e.timestamp  = persist_next_sale_number();
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

// A payout the machine cannot finish -- the hopper jammed, or no change plan
// exists. F19 and D-5: the amount still owed is written to history NOW, before
// the fault locks, so it survives a power cut and an Admin clear, and the
// transaction is then CLOSED so clearing the jam cannot offer this user's
// credit to the next person. The operator settles the balance by hand.
//
// History entry, transaction-level (denomination COIN_NONE, unlike the
// per-leg entry coin_hopper writes):
//   amount_in  = centavos still OWED to the user
//   volume_out = millilitres dispensed
//   change_out = centavos of change actually counted out
// Bench bypass ("skip", on from power-up in the test build). Never true in a
// build that sells.
static bool bench_bypass_on() {
#ifdef SERIAL_CONSOLE
  return hmi_console_bypass();
#else
  return false;
#endif
}

// Change that could not be paid on the last transaction, for the Thank You
// screen. With the bench bypass on, a failed payout is recorded and shown
// instead of locking the machine (team request 2026-10-27, test build only).
static money_t s_change_unpaid = 0;

static void record_unpaid_and_close() {
  money_t owed = billing_change_due() - s_change_paid;
  if (owed < 0) owed = 0;

  history_entry_t e;
  memset(&e, 0, sizeof(e));
  e.timestamp    = persist_next_sale_number();
  e.tag          = EVT_CHANGE_JAM;
  e.denomination = (uint8_t)COIN_NONE;
  e.amount_in    = owed;
  e.volume_out   = billing_total_dispensed();
  e.change_out   = s_change_paid;
  persist_history_add(&e);

  // The day's takings exclude what is still owed: the operator pays it out of
  // the box.
  persist_daily_add(billing_inserted() - s_change_paid - owed,
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
  { STATE_ACCEPTING,       STATE_PAYING_CHANGE },   // refund before choosing

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
    case TRIG_STORAGE_FULL:            return "coin box full";
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
  // Team 2026-11-03: a customer on the picker or the "Dispense more" screen
  // who drops another coin must get it credited. Only STANDBY and ACCEPTING
  // took coins, and the coinslot ignores the inhibit, so the coin was kept
  // and never credited.
  return s == STATE_STANDBY || s == STATE_ACCEPTING ||
         s == STATE_SELECTING || s == STATE_COMPLETE;
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
  // A latched or persistent fault (e.g. CHANGE JAM restored from EEPROM, or
  // LOW CHANGE after the last payout) must keep it shut even for the single
  // pass before STANDBY routes to FAULT.
  if (accepting_state(next) && !faults_is_locked()) {
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
      s_change_unpaid = 0;
      hmi_set_unpaid(0);

      // Case 20 path (d): a fault latched while nothing was owed. There was no
      // payout to hang the release on, so it happens here. Without this the
      // latch sits forever and the safety mechanism is decorative.
      faults_release_latched();
      break;

    case STATE_ACCEPTING: {
      // Discard a confirm press left over from standby or the previous user.
      // Acting on it would close ACCEPTING the moment the first coin lands.
      (void)confirm_take();

      // Open the EEPROM transaction record. From here on a power cut is
      // recoverable -- scenarios.md case 12.
      transaction_t txn;
      billing_store(&txn);
      persist_txn_open(&txn);
      break;
    }

    case STATE_AWAITING_BOTTLE:
      // The warning ladder counts from this entry, so a second selection in
      // the same transaction gets its own two warnings.
      s_bottle_warns = 0;
      // A press left over from the previous screen must not start the pour.
      (void)confirm_take();
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
        // Team 2026-11-10: with the tank ultrasonic lost, or the tank at half or
        // below, the fill pump puts the water back WHILE it pours, booked for
        // the full target now and trued up in SETTLING. Above half it does not
        // run alongside the pour.
        s_refill_booked = !FILL_FOLLOWS_POUR &&
                          (PUMPS_ALWAYS_TOGETHER || !water_tank_sensor_ok() ||
                           water_tank_distance_mm() >= TANK_SONAR_HALF_MM);
        if (s_refill_booked) water_refill_add(s_target_ml);
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
      s_change_unpaid = 0;
      s_leg_running = false;
      s_pay_short = false;
      hmi_set_unpaid(0);

      const money_t due = billing_change_due();
      if (due <= 0) break;    // nothing owed; PAYING_CHANGE falls straight through

      change_plan_t plan;
      if (!change_plan(due, coin_hopper_count(HOPPER_P1),
                       coin_hopper_count(HOPPER_P5), &plan)) {
        // Not enough in the hoppers. Hand over every coin there is, reserve
        // included; the shortfall is recorded and locks once they are out.
        const int32_t pesos = due / CENTAVOS_PER_PESO;
        const int32_t p5_have = coin_hopper_count(HOPPER_P5);
        const int32_t p1_have = coin_hopper_count(HOPPER_P1);
        const int32_t n5 = (pesos / 5 < p5_have) ? pesos / 5 : p5_have;
        const int32_t rest = pesos - n5 * 5;
        plan.p5 = (uint16_t)n5;
        plan.p1 = (uint16_t)((rest < p1_have) ? rest : p1_have);
        s_pay_short = true;
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
      if (faults_is_locked()) {
        transition_to(STATE_FAULT, TRIG_BOOT_PERSISTENT_FAULT);
      } else if (persist_has_open_txn()) {
        // Power cut or reset mid-transaction (team/client request 2026-10-30):
        // the customer's credit comes back and they resume at COMPLETE --
        // dispense more, or finish and take their change.
        const transaction_t *t = persist_open_txn();
        billing_load(t);
        // A pour cut off part way: bill what was delivered, rounded down, and
        // return the rest of its price to credit.
        if (t->target_ml > 0) billing_settle_partial((volume_t)t->banked_ml + t->segment_ml);
        // A payout leg cut off part way: how many coins left is unknowable, so
        // the whole leg comes off inventory and is paid again from credit --
        // the machine guesses against itself (decisions.md D-4).
        if (t->phase == TXN_PHASE_PAYING && t->leg_count > 0) {
          persist_inventory_add(t->leg_hopper == HOPPER_P5 ? DEST_P5_HOPPER : DEST_P1_HOPPER,
                                -(int16_t)t->leg_count);
        }
        s_target_ml = 0;
        transaction_t txn;
        billing_store(&txn);
        txn.phase = TXN_PHASE_CREDIT;
        txn.leg_count = 0;
        billing_load(&txn);
        persist_txn_update(&txn);
        if (billing_credit() > 0) {
          transition_to(STATE_COMPLETE, TRIG_BOOT_OPEN_TXN);
        } else {
          commit_transaction();
          transition_to(STATE_STANDBY, TRIG_BOOT_CLEAN);
        }
      } else {
        transition_to(STATE_STANDBY, TRIG_BOOT_CLEAN);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_STANDBY: {
      const bool bench_bypass = bench_bypass_on();
      // Bench bypass: nothing locks the test build (team request 2026-10-27).
      // Every fault is cleared here, stored ones included, so hardware that is
      // still being wired -- hopper relays, coin-line noise, the tank sensor --
      // cannot leave the machine out of service between tests.
      if (bench_bypass && faults_is_locked()) faults_clear_all();
      if (faults_is_locked()) {
        transition_to(STATE_FAULT, TRIG_FAULT_RAISED_WHILE_IDLE);
        break;
      }

      // Cold tank below a full P20 pour, or its sensor lost. Refused here,
      // before the first coin, and never mid-transaction: a pour already paid
      // for draws on the margin TANK_SONAR_MIN_SERVE_MM leaves for it.
      if (!bench_bypass && water_is_locked_out()) {
        faults_raise(FAULT_OUT_OF_WATER);
        transition_to(STATE_FAULT, TRIG_FAULT_RAISED_WHILE_IDLE);
        break;
      }

      // Coin box full: nowhere left for a P10 or P20 to go. Refuse money
      // before the first coin, like LOW CHANGE below.
      if (coin_level_box_full()) {
        faults_raise(FAULT_STORAGE_FULL);
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
      if (!bench_bypass && !coin_hopper_can_cover(billing_worst_case_change())) {
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
    }

    // -------------------------------------------------------------------
    case STATE_ACCEPTING:
      // Further coins are handled here rather than by re-entering the state --
      // re-entry would re-run on_enter() and reopen the transaction record.
      accept_pending_coin();

      // The box filled while this user was paying. Take no more coins, but let
      // them spend what is already in (SPEC 9 invariant 8): latch, and move on
      // once nothing is arriving so the coin in flight is still credited.
      if (coin_level_box_full() && coin_acceptor_idle() &&
          !coin_diverter_is_busy()) {
        faults_latch(FAULT_STORAGE_FULL);
        transition_to(STATE_SELECTING, TRIG_STORAGE_FULL);
        break;
      }

      // At the ceiling: stop taking money, but wait for CONTINUE (team request
      // 2026-10-30). Jumping to the picker on its own left the customer on a
      // screen they had not asked for.
      if (billing_at_ceiling() && !coin_acceptor_is_inhibited() &&
          coin_acceptor_idle() && !coin_diverter_is_busy()) {
        coin_acceptor_inhibit();
      }
      // No refund before buying (client, 2026-10-31): the machine sells water,
      // it does not change coins. Any FINISH/BACK is taken and dropped here so
      // it cannot surface later as a refund.
      if (hmi_event_available()) {
        int32_t payload = 0;
        (void)hmi_take_event(&payload);
      }
      // Leaving ACCEPTING inhibits the acceptor and discards anything in
      // flight, so a coin still arriving when confirm is pressed would be kept
      // and never credited. Hold the press until the chute is quiet.
      if (coin_acceptor_idle() && !coin_diverter_is_busy() && confirm_take()) {
        transition_to(STATE_SELECTING, TRIG_CONFIRM_PRESSED);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_SELECTING: {
      // Coins are taken on the picker as well (see accepting_state()); hold
      // the selection while one is in the chute so it is credited first.
      accept_pending_coin();
      if (!coin_acceptor_idle() || coin_diverter_is_busy()) break;
#if AUTO_DISPENSE_ALL_CREDIT
      // No picker: CONFIRM means "pour everything I paid for". The selection
      // is still made through billing_select(), so the price comes off the
      // credit before the valve opens, exactly as a picked amount would.
      {
        volume_t all = billing_max_selectable_ml();
        if (all > MAX_TRANSACTION_ML) all = MAX_TRANSACTION_ML;
        if (all > 0 && billing_can_select(all)) {
          s_target_ml = billing_select(all);

          transaction_t txn;
          billing_store(&txn);
          persist_txn_update(&txn);

          transition_to(STATE_AWAITING_BOTTLE, TRIG_TARGET_CHOSEN);
        } else {
          // Less than one peso of credit buys nothing; give it back.
          transition_to(STATE_PAYING_CHANGE, TRIG_FINISH_NO_POUR);
        }
        break;
      }
#endif
      // CONFIRM button on the picker: ask the screen for the size it shows.
      if (confirm_take()) hmi_confirm_on_picker();

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

      // Finish here only once water has been bought this transaction (back
      // from "Dispense more"). Before the first pour there is no refund
      // (client, 2026-10-31): the machine sells water, it does not change coins.
      if ((e == HMI_EVENT_FINISH || e == HMI_EVENT_BACK) && billing_total_dispensed() > 0) {
        transition_to(STATE_PAYING_CHANGE, TRIG_FINISH);
      }
      break;
    }

    // -------------------------------------------------------------------
    case STATE_AWAITING_BOTTLE: {
      // A removal edge from before the pour (bottle set down and lifted while
      // choosing) must not pause the pour the moment it starts.
      (void)bottle_just_removed();
      if (bottle_present()) {
        s_bottle_by_button = false;
        transition_to(STATE_DISPENSING, TRIG_BOTTLE_DETECTED);
        break;
      }
      // The CONFIRM push button also starts the pour (team request
      // 2026-10-30): the cabinet's bottle IR misses bottles, and a customer
      // who has paid must still get water. The customer has placed the bottle
      // and said so; this pour then ignores the IR (see DISPENSING).
      if (confirm_take()) {
        s_bottle_by_button = true;
        transition_to(STATE_DISPENSING, TRIG_BOTTLE_DETECTED);
        break;
      }
#if BOTTLE_AUTO_START_MS
      // Bench 2026-11-03: the IR still misses bottles and customers do not
      // know to press CONFIRM, so a paid sale sat on "Place your bottle".
      // After BOTTLE_AUTO_START_MS the pour starts on its own, IR ignored.
      if (state_elapsed() >= BOTTLE_AUTO_START_MS) {
        s_bottle_by_button = true;
        transition_to(STATE_DISPENSING, TRIG_BOTTLE_DETECTED);
        break;
      }
#endif

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
      // Silent to BOTTLE_WAIT_WARN1_MS, then the alarm sounds until the cancel
      // (team 2026-11-10; was two single beeps). See bottle_alarm_wanted().
      if (state_elapsed() >= BOTTLE_WAIT_CANCEL_MS) {
        // Cancelled. The selection never poured, so its price goes back to
        // credit in full before the payout is planned -- otherwise the user
        // would be refunded only what they had left after paying for water
        // they never received.
        billing_cancel_selection();
        s_target_ml = 0;
        // Before any water has been bought, no refund (client, 2026-10-31):
        // back to the picker with the credit intact. Otherwise a select-and-
        // walk-away would turn the machine into a coin changer.
        if (billing_total_dispensed() == 0) {
          transaction_t txn;
          billing_store(&txn);
          persist_txn_update(&txn);
          transition_to(STATE_SELECTING, TRIG_BOTTLE_WAIT_TIMEOUT);
        } else {
          transition_to(STATE_PAYING_CHANGE, TRIG_BOTTLE_WAIT_TIMEOUT);
        }
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
      // Debounced state as well as the edge: a removal edge consumed earlier
      // must not leave the valve open over an empty tray.
      // A pour started by the button or the auto-start (IR never saw the
      // bottle) pauses only on a removal edge -- the IR saw a bottle and then
      // lost it. Waiting on presence would pause it at once.
      if (IR_PAUSES_POUR &&
          (bottle_just_removed() ||
           ((NO_BOTTLE_STOPS_POUR || !s_bottle_by_button) && !bottle_present()))) {
        transition_to(STATE_PAUSED, TRIG_BOTTLE_REMOVED);
        break;
      }
      if (dispense_status() == DISPENSE_COMPLETE) {
        transition_to(STATE_SETTLING, TRIG_TARGET_REACHED);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_PAUSED:
      // Resume on the debounced state, never on the edge alone. A "placed"
      // edge left over from before the pour (bottle set down before the coins)
      // would otherwise resume dispensing the instant the bottle was lifted.
      (void)bottle_just_placed();
      if (bottle_present()) {
        transition_to(STATE_DISPENSING, TRIG_BOTTLE_REPLACED);
        break;
      }
      if (state_elapsed() >= BOTTLE_REMOVED_GRACE_MS) {
        s_charge_full = BOTTLE_REMOVED_CHARGES_FULL;
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
        if ((delivered >= s_target_ml || s_charge_full) && s_target_ml > 0) {
          billing_settle_complete(delivered);
        } else {
          billing_settle_partial(delivered);
        }
        s_charge_full = false;
        // The refill for the full target was booked when the pour started (it
        // runs alongside it). True it up to what physically left the tank --
        // plumbing, not money.
        if (s_refill_booked) {
          if (delivered < s_target_ml) {
            water_refill_cancel(s_target_ml - delivered);
          } else {
            water_refill_add(delivered - s_target_ml);
          }
          s_refill_booked = false;
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
      // More coins can be added here too (see accepting_state()). Hold the
      // buttons while one is in the chute, so it is credited before the
      // screen moves on and the acceptor is inhibited.
      accept_pending_coin();
      if (!coin_acceptor_idle() || coin_diverter_is_busy()) break;
      if (billing_credit() <= 0) {
        transition_to(STATE_PAYING_CHANGE, TRIG_CREDIT_ZERO);
        break;
      }
      int32_t payload = 0;
      const hmi_event_t e = hmi_take_event(&payload);
      if (e == HMI_EVENT_DISPENSE_AGAIN) {
        transition_to(STATE_SELECTING, TRIG_DISPENSE_MORE);
      } else if ((e == HMI_EVENT_FINISH || e == HMI_EVENT_BACK) &&
                 billing_total_dispensed() > 0) {
        // Credit restored at boot with nothing poured yet reaches here too.
        // No refund before buying (client, 2026-10-31), so Finish is refused.
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

      // A result this payout did not start belongs to a test run; drop it.
      if (!s_leg_running) coin_hopper_clear();

      // Reap a finished leg before starting the next. Inventory has already
      // been decremented by coin_hopper's own finish(), by COUNTED coins --
      // never by commanded ones. If the sensor did not see it leave, the
      // machine still owns it.
      const payout_result_t r = coin_hopper_status();
      if (r == PAYOUT_JAMMED) {
        s_change_paid += (money_t)coin_hopper_counted() *
                         (s_pay_leg == 0 ? 500 : 100);
        coin_hopper_clear();
        s_leg_running = false;
        s_change_unpaid = billing_change_due() - s_change_paid;
        if (s_change_unpaid < 0) s_change_unpaid = 0;
        record_unpaid_and_close();
        if (bench_bypass_on()) {
          // Bench bypass: recorded above (F19), shown on the Thank You screen
          // for CHANGE_UNPAID_HOLD_MS, then back to selling.
          hmi_set_unpaid(s_change_unpaid);
          transition_to(STATE_THANK_YOU, TRIG_PAYOUT_SHORT);
          break;
        }
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
        s_leg_running = false;
        s_pay_leg++;
      }

      // Start the next leg. Two hoppers, run in sequence -- coin_hopper drives
      // one at a time and refuses an overlapping command.
      if (s_pay_leg == 0) {
        if (s_pay_p5 > 0) {
          coin_hopper_dispense(HOPPER_P5, s_pay_p5);
          s_leg_running = true;
          break;
        }
        s_pay_leg = 1;
      }
      if (s_pay_leg == 1) {
        if (s_pay_p1 > 0) {
          coin_hopper_dispense(HOPPER_P1, s_pay_p1);
          s_leg_running = true;
          break;
        }
        s_pay_leg = 2;
      }

      // The hoppers ran out before the change did: every coin is out, the rest
      // goes on record, and the machine locks until an operator settles it.
      if (s_pay_short) {
        s_pay_short = false;
        s_change_unpaid = billing_change_due() - s_change_paid;
        if (s_change_unpaid < 0) s_change_unpaid = 0;
        record_unpaid_and_close();
        if (bench_bypass_on()) {
          hmi_set_unpaid(s_change_unpaid);
          transition_to(STATE_THANK_YOU, TRIG_PAYOUT_SHORT);
          break;
        }
        faults_raise(FAULT_CHANGE_JAM);
        transition_to(STATE_FAULT, TRIG_PAYOUT_SHORT);
        break;
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
      // With unpaid change the notice stays up the full CHANGE_UNPAID_HOLD_MS
      // so the customer can read how much they are owed.
      if (s_change_unpaid > 0) {
        if (state_elapsed() >= CHANGE_UNPAID_HOLD_MS) {
          transition_to(STATE_STANDBY, TRIG_THANKYOU_DONE);
        }
        break;
      }
      if (state_elapsed() >= HMI_THANKYOU_HOLD_MS || bottle_just_removed()) {
        transition_to(STATE_STANDBY, TRIG_THANKYOU_DONE);
      }
      break;

    // -------------------------------------------------------------------
    case STATE_FAULT:
      // Bench bypass: never stay locked (see STANDBY).
      if (bench_bypass_on()) faults_clear_all();
      // COIN STORAGE FULL is transient: it clears when the box is emptied and
      // the beam is clear again.
      if (!coin_level_box_full()) {
        faults_clear(FAULT_STORAGE_FULL);
      }
      // OUT OF WATER likewise clears once the pump has brought the tank back
      // above the serve line.
      if (!water_is_locked_out()) {
        faults_clear(FAULT_OUT_OF_WATER);
      }
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
  // Relays OFF before anything else (see relay.h for what OFF means).
  static const uint8_t RELAYS[] = { PIN_PUMP, PIN_VALVE, PIN_HOPPER_P1_RUN,
                                    PIN_HOPPER_P5_RUN, PIN_COMPRESSOR };
  for (uint8_t i = 0; i < sizeof(RELAYS); i++) relay_write(RELAYS[i], false);

#ifdef DEBUG
  Serial.begin(DEBUG_BAUD);
#ifdef SERIAL_CONSOLE
  Serial.println(F("[console] TEST CONSOLE -- type h for commands. NOT FOR SALE."));
#endif
  Serial.println(F("EMX-2026-WATERVENDO-01 boot [DEBUG BUILD - NOT FOR SERVICE]"));
  // The firmware cannot detect whether gate servos are fitted, so a build that
  // does not match the hardware is invisible until money goes wrong. Say which
  // build this is, first thing.
  Serial.println(COIN_ROUTING_MODE == COIN_ROUTING_COLLECT_ALL
      ? F("[boot] coin routing: COLLECT_ALL (no servos, gate A pinned to coin box, P5 reserve 0)")
      : F("[boot] coin routing: RECIRCULATE (gate servos fitted, P5 reserve 10)"));
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
  boot_step(F("coin level"));    coin_level_begin();
  boot_step(F("flow"));          flow_begin();
  boot_step(F("bottle"));        bottle_begin();
  boot_step(F("water level"));   water_level_begin();
  boot_step(F("dispense"));      dispense_begin();
  boot_step(F("billing"));       billing_begin();
  boot_step(F("hmi"));           hmi_begin();
#ifndef BENCH_COIN_TEST
  service_servo_write(SERVICE_SERVO_REST_DEG);
#endif

  // Confirm button. INPUT_PULLUP like every other input, so a broken wire reads
  // as "not pressed" rather than as a permanently held button.
  // Level first, then output: the other order drives the pin LOW for an instant,
  // and a low-level-trigger buzzer chirps on every boot.
  digitalWrite(PIN_BUZZER, BUZZER_OFF_LEVEL);
  pinMode(PIN_BUZZER, OUTPUT);

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

#ifdef BENCH_COIN_TEST
  // Bench coin-path test: the acceptor is enabled regardless of faults so the
  // coinslot, gates and hopper IR can be exercised before Admin exists.
  Serial.println(F("[bench] COIN PATH TEST -- no billing, no water. NOT FOR SALE."));
  coin_acceptor_uninhibit();
#endif
}

#ifdef BENCH_COIN_TEST
// Takes each identified coin and routes it through the gates, nothing else. No
// credit, no state machine. Inventory counts DO change as coins land, as they
// would in service -- reset them in Admin before the unit takes money.
static void bench_update() {
  if (coin_diverter_is_busy()) return;
  if (!coin_acceptor_available()) return;
  const coin_t c = coin_acceptor_take_coin();
  if (c == COIN_NONE) return;
  coin_diverter_route(c);
}
#endif

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
  // RAM compare every pass; an EEPROM write only when the day changes.
  persist_daily_sync_day(rtc_day_number());
  (void)rtc_day_rolled();   // superseded by the stored day number above
  coin_acceptor_update();
  // Aim the gates the instant a coin is identified. The state machine may not
  // route it until after an EEPROM write (~100 ms on the first coin), and the
  // coin is falling the whole time.
  // Not while the train is still counting: a servo swung on the first pulse
  // put enough noise and sag on the lines that the rest of a P5's pulses were
  // lost (read as P1) and the display rebooted (bench 2026-10-30).
  if (coin_acceptor_available()) coin_diverter_aim(coin_acceptor_peek());
  coin_diverter_update();
  coin_hopper_update();
  coin_level_update();
  flow_update();
  bottle_update();
  water_level_update();
  dispense_update();
  hmi_update();
  faults_update();
  confirm_update();
  buzzer_alarm_update(bottle_alarm_wanted());
  buzzer_update();
  buzzer_diag_update();

#ifdef BENCH_COIN_TEST
  (void)state_update;   // the sale path is compiled, just not run
  bench_update();
#else
  state_update(s_state);
  // Stops at FILL_FOLLOW_PERMILLE of the pour's run, so it puts back what was
  // poured rather than what it pumps in the same time.
  // Only with a bottle the IR can see (team 2026-11-10): a pour started without
  // one (auto-start, CONFIRM) does not refill the drum.
  water_fill_follow_pour(FILL_FOLLOWS_POUR && s_state == STATE_DISPENSING &&
                         dispense_valve_open() && bottle_present() &&
                         (int32_t)dispense_delivered() * 1000L <
                             (int32_t)dispense_stop_ml() * FILL_FOLLOW_PERMILLE);
  service_servo_update();
  hmi_sync(s_state);
#endif

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
