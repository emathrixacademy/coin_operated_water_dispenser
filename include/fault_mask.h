#ifndef FAULT_MASK_H
#define FAULT_MASK_H

// Fault set arithmetic -- priority ordering and the persistent subset.
//
// Several faults can be active at once, so the active set is a BITMASK rather
// than a single value. Last-write-wins would let a LOW CHANGE raised a moment
// later mask an active CHANGE JAM, and the jam is the one the technician has to
// see.
//
// Deliberately NO Arduino dependency: this is the logic that decides which
// fault a locked machine displays and which faults survive a power cycle, so it
// links into the host-side unit tests. faults.cpp holds the state and does the
// acceptor inhibiting and the EEPROM writes; this file only does the set
// arithmetic on it.

#include "types.h"

// Bit for one fault. FAULT_NONE maps to 0 -- it is never a member of a set.
uint8_t fault_bit(fault_t f);

// Highest-priority fault in the set, or FAULT_NONE if the set is empty.
//
// SPEC 6.2, most blocking first:
//   CHANGE JAM > FLOW STALL > PUMP > ACCEPTOR > OUT OF WATER > LOW CHANGE
//   > STORAGE FULL
fault_t fault_highest(uint8_t mask);

// True if this fault must survive a reboot.
//
// A persistent fault that clears on power cycle is worse than not claiming
// persistence at all: the operator learns that the fix is a reboot, the coins
// stay jammed, and the machine returns to accepting money it cannot pay out.
bool fault_persistent(fault_t f);

// The subset of a set that must survive a reboot. This is what gets written to
// EEPROM, and what a stored value is filtered through on restore -- a stored
// bit that is not classified persistent is dropped rather than trusted, since
// it can only have come from a firmware whose fault set differed.
uint8_t fault_persistent_subset(uint8_t mask);

#endif  // FAULT_MASK_H
