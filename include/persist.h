#ifndef PERSIST_H
#define PERSIST_H

// Persistence -- EEPROM inventory, open transaction, daily totals, history.
//
// Write per transaction and at each inventory change. NEVER per loop
// iteration. EEPROM is rated for roughly 100,000 writes per cell, and a write
// in loop() burns a cell in under a minute.
//
// Use EEPROM.update(), never EEPROM.write(). update() skips the write when the
// byte is unchanged, which on this machine means most of the record costs
// nothing most of the time.
//
// Daily counters change far more often than anything else, so they are
// wear-levelled across DAILY_RING_SLOTS cells rather than hammering one
// address.
//
// Every record carries EEPROM_MAGIC, a layout version and a checksum. A first
// boot or a corrupted cell is DETECTED AND INITIALISED, never read as garbage
// and acted on. A corrupt inventory read as valid means the machine believes it
// holds change it does not have, which is a jam under a paying user.

#include "types.h"

void persist_begin();
void persist_update();

// True if the stored record failed its magic or checksum on boot and the
// machine initialised fresh. Surfaced on the Admin page -- a technician needs
// to know the inventory was reset rather than restored.
bool persist_was_initialised();

// --- Inventory ----------------------------------------------------------

const inventory_t *persist_inventory();

// Adjust a hopper count and commit immediately.
//
// Called with coins COUNTED by the outlet sensor, never with coins commanded.
// If the sensor did not see it leave, the machine still owns it.
void persist_inventory_add(coin_dest_t dest, int16_t delta);

// Admin correction. Sets an absolute count and commits immediately.
//
// This is the one write path that can desync the stored inventory from the
// physical hopper contents, so it is bounded by HOPPER_CAPACITY, requires an
// explicit confirm at the HMI layer, and writes an EVT_ADMIN_EDIT history entry
// recording the before and after counts.
void persist_inventory_set(coin_dest_t dest, uint16_t count);

// --- Open transaction ---------------------------------------------------

// An in-flight transaction is persisted so a power cut does not cost the user
// their money. On boot the machine restores it and resumes with the remaining
// balance shown -- see scenarios.md case 12.
void persist_txn_open(const transaction_t *txn);
void persist_txn_update(const transaction_t *txn);
void persist_txn_close();

bool persist_has_open_txn();
const transaction_t *persist_open_txn();

// --- Boot reconciliation ------------------------------------------------

// Handle a coin credited but never confirmed routed -- power lost inside the
// COIN_LOCKOUT_MS window. See scenarios.md case 13.
//
// POLICY, decided and not to be quietly changed:
//   1. The user keeps the credit. They inserted real money.
//   2. The coin is assumed to have reached the PROFIT CHAMBER. Neither hopper
//      is incremented.
//   3. An EVT_COIN_UNROUTED entry is written to the history ring buffer.
//
// The asymmetry is the point. Understating hopper stock makes the machine lock
// early, which is an annoyance. Overstating it makes the machine promise change
// it does not physically hold, which is a jam and an angry user.
// ALWAYS FAIL TOWARD THE UNDERSTATEMENT.
//
// The distinct event tag exists so a technician reading the history sees why
// the count is off by one. Without it an unexplained discrepancy in a machine
// full of cash reads as theft, and they go looking for a person instead of a
// power cut.
void persist_reconcile_unrouted_coin(coin_t coin);

// Mark a coin as credited but not yet routed. Written before the servo moves
// and cleared once the diverter reports settled -- this is what makes the
// reconciliation above possible.
void persist_mark_coin_in_flight(coin_t coin);
void persist_clear_coin_in_flight();

// The in-flight coin recorded before the last diverter move, or COIN_NONE.
// Read once on boot to drive the reconciliation above.
coin_t persist_coin_in_flight();

// --- Persistent faults --------------------------------------------------
//
// SPEC 6.1 and 7.1. A bitmask, one bit per fault_t, holding only the faults
// classified persistent by faults_is_persistent().
//
// A persistent fault that clears on power cycle is worse than not claiming
// persistence at all: the operator learns that the fix is a reboot, the coins
// stay jammed, and the machine returns to accepting money it cannot pay out.
//
// Owned by faults.cpp. Nothing else may write these.
uint8_t persist_fault_flags();
void    persist_fault_flags_set(uint8_t flags);

// --- Daily totals -------------------------------------------------------

money_t  persist_daily_profit();
volume_t persist_daily_volume();
uint16_t persist_daily_transactions();
void     persist_daily_add(money_t profit, volume_t volume);

// Close out the day at the midnight boundary. Writes the closing totals to the
// history ring BEFORE zeroing them.
//
// The write-then-zero order is not a detail. The daily total is what the owner
// counts cash against, and once it is zeroed nothing anywhere remembers what it
// was -- a rollover that zeroed first would lose the whole day on a power cut
// in between. Both this and the Admin reset go through one internal choke point
// so neither can grow a path that skips the write.
//
// Called by the state machine when rtc_day_rolled() reports a boundary.
void persist_daily_rollover(uint32_t timestamp);

// Admin "reset daily totals". Same write-then-zero guarantee, tagged
// distinctly so a technician can tell a hand-ended day from a midnight one.
void persist_daily_reset(uint32_t timestamp);

// --- History ------------------------------------------------------------

// Twenty entries, oldest overwritten. The Mega has 4 KB of EEPROM and CLAUDE.md
// rules out an SD logger, so this is the ceiling. A longer history is a new
// scope item to be quoted, not built.
void persist_history_add(const history_entry_t *entry);

uint8_t persist_history_count();

// index 0 is the most recent.
const history_entry_t *persist_history_get(uint8_t index);

#endif  // PERSIST_H
