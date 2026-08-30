// Fault set arithmetic. No Arduino dependency by design -- see fault_mask.h.

#include "fault_mask.h"

// SPEC 6.2, display priority, most blocking first.
//
// THE ORDER OF THIS ARRAY IS THE PRIORITY. Nothing else encodes it, so
// reordering these lines changes what a locked machine shows a technician.
//
// It also enumerates every fault, which is what lets fault_persistent_subset()
// walk the whole set rather than assuming a contiguous enum range.
static const fault_t PRIORITY[] = {
  FAULT_CHANGE_JAM,     // machine has demonstrated it cannot pay -- worst
  FAULT_FLOW_STALL,     // cannot deliver water
  FAULT_PUMP_RUNTIME,   // pump damaging itself
  FAULT_ACCEPTOR,       // silently swallowing coins
  FAULT_OUT_OF_WATER,   // refillable by the operator
  FAULT_LOW_CHANGE,     // serviceable, machine is otherwise healthy
  FAULT_STORAGE_FULL    // least blocking
};
#define PRIORITY_COUNT (sizeof(PRIORITY) / sizeof(PRIORITY[0]))

uint8_t fault_bit(fault_t f) {
  return (f == FAULT_NONE) ? 0u : (uint8_t)(1u << (uint8_t)f);
}

fault_t fault_highest(uint8_t mask) {
  for (uint8_t i = 0; i < PRIORITY_COUNT; i++) {
    if (mask & fault_bit(PRIORITY[i])) return PRIORITY[i];
  }
  return FAULT_NONE;
}

bool fault_persistent(fault_t f) {
  // A change jam and a flow stall must survive a reboot. Power-cycling the
  // machine must not silently return it to service with a jammed hopper and an
  // inventory that believes it paid.
  return f == FAULT_CHANGE_JAM ||
         f == FAULT_FLOW_STALL ||
         f == FAULT_PUMP_RUNTIME ||
         f == FAULT_ACCEPTOR;
}

uint8_t fault_persistent_subset(uint8_t mask) {
  uint8_t out = 0;
  for (uint8_t i = 0; i < PRIORITY_COUNT; i++) {
    const fault_t f = PRIORITY[i];
    if ((mask & fault_bit(f)) && fault_persistent(f)) out |= fault_bit(f);
  }
  return out;
}
