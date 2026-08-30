#ifndef FAULTS_H
#define FAULTS_H

// Faults -- lockout conditions and alert routing.
//
// ===========================================================================
// IN EVERY LOCKED STATE THE COIN ACCEPTOR IS DISABLED FIRST.
// ===========================================================================
//
// Disabling the acceptor comes before changing the screen, before the buzzer,
// before anything. Never accept money the machine cannot honour. A user whose
// coin goes in one millisecond after a lockout begins is a refund the machine
// may not be able to pay.
//
// Lockout conditions:
//
//   Gallon bay empty            OUT OF WATER -- PLEASE REFILL     auto-clears
//   P1 < 25 pcs or P5 < 5 pcs   LOW CHANGE -- SERVICE REQUIRED    admin reload
//   Profit chamber full         COIN STORAGE FULL                 auto-clears
//   Hopper payout failed        CHANGE JAM -- SERVICE REQUIRED    service
//   Flow stall                  SERVICE REQUIRED                  service
//   Pump overran                SERVICE REQUIRED                  service
//
// A transaction already in progress when a lockout raises is allowed to finish
// and pay out its change where it physically can. The lockout stops a NEW
// transaction from starting. The exception is a change jam, where the machine
// has already demonstrated it cannot pay.

#include "types.h"

void faults_begin();
void faults_update();

// Highest-priority active fault, or FAULT_NONE.
fault_t faults_active();

bool faults_is_locked();

// Raise a fault that a module detected -- a jam, a stall, a pump overrun. The
// acceptor is inhibited inside this call, before it returns.
//
// Do NOT call this while money is owed to a user standing at the machine. Use
// faults_latch() -- see SPEC 9 invariant 8.
void faults_raise(fault_t fault);

// --- Deferred locking: SPEC 9 invariant 8 ------------------------------
//
// A fault may never be raised while money is owed to a user who is still
// standing there. Settle first, lock second. Stranding a user's coins inside a
// locked machine is worse than whatever the fault was protecting against, and
// it is the failure they will remember.
//
// latch() records the fault without locking, so the transaction can finish and
// the change can be paid. release_latched() then raises everything held, and is
// called by the state machine once the change has physically been counted out.
//
// CHANGE JAM is the exception and is raised immediately even if latched: the
// machine has already demonstrated it cannot pay, so there is no payout left to
// protect and deferring the lock would only let it take more money.
void faults_latch(fault_t fault);
bool faults_has_latched();
void faults_release_latched();

// Clear a serviceable fault. Conditions that auto-clear -- water, storage --
// are re-evaluated in update() and do not need this.
void faults_clear(fault_t fault);

// Display text for a fault. Points at PROGMEM-backed storage; the caller does
// not own it and must not modify it.
const char *faults_message(fault_t fault);

// True if this fault must survive a reboot. A change jam does -- rebooting the
// machine must not silently return it to service with a jammed hopper and an
// inventory that thinks it paid.
bool faults_is_persistent(fault_t fault);

#endif  // FAULTS_H
