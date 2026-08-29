#include <Arduino.h>
#include "faults.h"
#include "coin_acceptor.h"

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

static fault_t s_active = FAULT_NONE;

void faults_begin() {
  s_active = FAULT_NONE;
}

void faults_update() {
  // TODO(M5): re-evaluate auto-clearing conditions (water, storage) and hold
  // the serviceable ones until cleared by hand.
}

fault_t faults_active() {
  return s_active;
}

bool faults_is_locked() {
  return s_active != FAULT_NONE;
}

void faults_raise(fault_t fault) {
  // Acceptor first. Always. Before the screen, before the buzzer.
  coin_acceptor_inhibit();
  s_active = fault;
  // TODO(M5): priority ordering when several are active at once.
}

void faults_clear(fault_t fault) {
  (void)fault;
  // TODO(M5)
}

const char *faults_message(fault_t fault) {
  switch (fault) {
    case FAULT_OUT_OF_WATER: return MSG_OUT_OF_WATER;
    case FAULT_LOW_CHANGE:   return MSG_LOW_CHANGE;
    case FAULT_STORAGE_FULL: return MSG_STORAGE_FULL;
    case FAULT_CHANGE_JAM:   return MSG_CHANGE_JAM;
    case FAULT_FLOW_STALL:   return MSG_SERVICE;
    case FAULT_PUMP_RUNTIME: return MSG_SERVICE;
    default:                 return MSG_NONE;
  }
}

bool faults_is_persistent(fault_t fault) {
  // A change jam and a flow stall must survive a reboot. Power-cycling the
  // machine must not silently return it to service with a jammed hopper and an
  // inventory that believes it paid.
  return fault == FAULT_CHANGE_JAM ||
         fault == FAULT_FLOW_STALL ||
         fault == FAULT_PUMP_RUNTIME;
}
