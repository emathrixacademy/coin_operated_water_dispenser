#include <Arduino.h>
#include "faults.h"
#include "fault_mask.h"
#include "coin_acceptor.h"
#include "persist.h"

// Milestone 2: contract only. Implementation lands in Milestone 5.
//
// Implementation note: faults_raise() inhibits the acceptor BEFORE it returns
// and before anything touches the display. That ordering is the requirement,
// not an optimisation -- a coin accepted one millisecond into a lockout is a
// refund the machine may not be able to pay.

static const char MSG_OUT_OF_WATER[]  PROGMEM = "OUT OF WATER - PLEASE REFILL";
static const char MSG_LOW_CHANGE[]    PROGMEM = "LOW CHANGE - SERVICE REQUIRED";
static const char MSG_STORAGE_FULL[]  PROGMEM = "COIN STORAGE FULL";
static const char MSG_CHANGE_JAM[]    PROGMEM = "CHANGE JAM - SERVICE REQUIRED";
static const char MSG_SERVICE[]       PROGMEM = "SERVICE REQUIRED";
static const char MSG_NONE[]          PROGMEM = "";

// The active fault set. Set arithmetic and priority ordering live in
// fault_mask.cpp, which has no Arduino dependency and is unit tested; this file
// owns the STATE and the two side effects that must not be tested by guesswork
// -- inhibiting the acceptor and writing EEPROM.
static uint8_t s_mask = 0;

// Faults detected while money is still owed to a user who is standing there.
//
// SPEC 9 INVARIANT 8: settle first, lock second. These are held here until
// faults_release_latched() is called, which the state machine does once the
// change has physically been counted out. Stranding a user's coins inside a
// locked machine is worse than whatever the fault was protecting against.
static uint8_t s_latched = 0;

static void commit_persistent() {
  persist_fault_flags_set(fault_persistent_subset(s_mask));
}

void faults_begin() {
  // SPEC 6.1: restore persistent faults BEFORE the machine can take a coin.
  //
  // persist_begin() must already have run -- main.cpp calls it first for
  // exactly this reason. A stored flag that is not classified persistent is
  // dropped rather than trusted; it can only come from a firmware whose fault
  // set differed.
  s_mask = fault_persistent_subset(persist_fault_flags());
  s_latched = 0;

  if (s_mask != 0) {
    // Same ordering rule as faults_raise(): acceptor first, always. The machine
    // must not be able to take a coin between boot and the screen appearing.
    coin_acceptor_inhibit();
  }
}

void faults_update() {
  // TODO(M5): re-evaluate the transient conditions and clear them when the
  // condition itself has gone -- water, storage full, low change.
  //
  // LOW CHANGE, decided in SPEC 6.1.1 and NOT to be re-derived here:
  //
  //   - Transient. It clears the moment an operator loads coins and confirms
  //     the count in Admin, because the condition has gone. There is no
  //     separate service clear.
  //   - RAISED at exactly one moment: before the first coin of a transaction is
  //     accepted, against billing_worst_case_change() per 3.4.
  //   - NEVER re-evaluated mid-transaction. A user who has already paid must
  //     not hit a lockout that strands their money -- invariant 8 from the
  //     other direction. Once coins are in, the machine finishes what it
  //     started. The gate is the only moment where refusing costs them nothing.
  //
  // So this function CLEARS low change; it does not raise it. The raise lives
  // in the STANDBY -> ACCEPTING transition.
}

fault_t faults_active() {
  return fault_highest(s_mask);
}

bool faults_is_locked() {
  return s_mask != 0;
}

void faults_raise(fault_t fault) {
  if (fault == FAULT_NONE) return;

  // ---------------------------------------------------------------------
  // ACCEPTOR FIRST. ALWAYS. Before the screen, before the buzzer, before the
  // EEPROM write below.
  //
  // SPEC 9 makes this an invariant enforced at this single choke point. A user
  // whose coin goes in one millisecond after a lockout begins is a refund the
  // machine may not be able to pay. The EEPROM write takes milliseconds; doing
  // it before the inhibit would be a window wide enough to swallow a coin.
  // ---------------------------------------------------------------------
  coin_acceptor_inhibit();

  const uint8_t before = s_mask;
  s_mask |= fault_bit(fault);

  // Only touch the EEPROM when the persistent set actually changed. Re-raising
  // an already-active fault every pass would otherwise burn a cell.
  if (fault_persistent_subset(s_mask) != fault_persistent_subset(before)) {
    commit_persistent();
  }
}

void faults_latch(fault_t fault) {
  // SPEC 9 INVARIANT 8: settle first, lock second.
  //
  // Records the fault WITHOUT locking the machine, so the transaction in
  // progress can finish and the user can be paid what they are owed. The state
  // machine calls faults_release_latched() once the change has physically been
  // counted out of the hoppers.
  //
  // CHANGE JAM is the one fault that must never be latched -- the machine has
  // already demonstrated it cannot pay, so there is no payout to protect and
  // deferring the lock would only let it take more money. Raise it immediately
  // instead of latching, so a caller that gets this wrong still fails safe.
  if (fault == FAULT_NONE) return;
  if (fault == FAULT_CHANGE_JAM) {
    faults_raise(fault);
    return;
  }
  s_latched |= fault_bit(fault);
}

bool faults_has_latched() {
  return s_latched != 0;
}

void faults_release_latched() {
  // Called by the state machine after change is paid. Everything latched is now
  // raised for real, which locks the machine.
  if (s_latched == 0) return;

  const uint8_t to_raise = s_latched;
  s_latched = 0;

  coin_acceptor_inhibit();

  const uint8_t before = s_mask;
  s_mask |= to_raise;

  if (fault_persistent_subset(s_mask) != fault_persistent_subset(before)) {
    commit_persistent();
  }
}

void faults_clear(fault_t fault) {
  if (fault == FAULT_NONE) return;

  const uint8_t before = s_mask;
  s_mask &= (uint8_t)~fault_bit(fault);
  s_latched &= (uint8_t)~fault_bit(fault);

  if (fault_persistent_subset(s_mask) != fault_persistent_subset(before)) {
    commit_persistent();
  }

  // Deliberately does NOT un-inhibit the acceptor, even when the last fault
  // clears. SPEC 2.3 puts the acceptor under the state machine's control -- it
  // is enabled in STANDBY and ACCEPTING and nowhere else. Re-enabling it from
  // here would hand a second owner to the inhibit line and could enable the
  // acceptor in, say, PAYING_CHANGE.
}

const char *faults_message(fault_t fault) {
  switch (fault) {
    case FAULT_OUT_OF_WATER: return MSG_OUT_OF_WATER;
    case FAULT_LOW_CHANGE:   return MSG_LOW_CHANGE;
    case FAULT_STORAGE_FULL: return MSG_STORAGE_FULL;
    case FAULT_CHANGE_JAM:   return MSG_CHANGE_JAM;
    case FAULT_FLOW_STALL:   return MSG_SERVICE;
    case FAULT_PUMP_RUNTIME: return MSG_SERVICE;
    case FAULT_ACCEPTOR:     return MSG_SERVICE;
    default:                 return MSG_NONE;
  }
}

bool faults_is_persistent(fault_t fault) {
  return fault_persistent(fault);
}
