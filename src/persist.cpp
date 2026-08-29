#include <Arduino.h>
#include <EEPROM.h>
#include "persist.h"

// Milestone 2: contract only. Implementation lands in Milestone 3.
//
// Implementation notes:
//   - EEPROM.update() everywhere. Never EEPROM.write().
//   - Every record: magic, layout version, checksum. A failed check
//     initialises fresh and sets persist_was_initialised(), which the Admin
//     page surfaces. Never act on unvalidated bytes.
//   - Daily counters rotate across DAILY_RING_SLOTS with a sequence number, so
//     the newest valid slot wins on boot.
//   - Writes happen at end of transaction and at inventory change. NEVER in
//     loop().

static inventory_t s_inventory;
static transaction_t s_open_txn;
static bool s_initialised = false;

void persist_begin() {
  // TODO(M3): read and validate the header, load or initialise.
  s_inventory.p1_count = 0;
  s_inventory.p5_count = 0;
  s_inventory.profit_p10 = 0;
  s_inventory.profit_p20 = 0;
  s_open_txn.open = false;
}

void persist_update() {
  // Nothing periodic. Persistence is event-driven by design -- a timer here
  // would be the thing that burns out the EEPROM.
}

bool persist_was_initialised() {
  return s_initialised;
}

const inventory_t *persist_inventory() {
  return &s_inventory;
}

void persist_inventory_add(coin_dest_t dest, int16_t delta) {
  (void)dest;
  (void)delta;
  // TODO(M3): apply and commit. Called with coins COUNTED, never commanded.
}

void persist_inventory_set(coin_dest_t dest, uint16_t count) {
  (void)dest;
  (void)count;
  // TODO(M3): bound by HOPPER_CAPACITY, commit, write EVT_ADMIN_EDIT with the
  // before and after counts.
}

void persist_txn_open(const transaction_t *txn) {
  (void)txn;
  // TODO(M3)
}

void persist_txn_update(const transaction_t *txn) {
  (void)txn;
  // TODO(M3)
}

void persist_txn_close() {
  // TODO(M3)
}

bool persist_has_open_txn() {
  return s_open_txn.open;
}

const transaction_t *persist_open_txn() {
  return &s_open_txn;
}

void persist_reconcile_unrouted_coin(coin_t coin) {
  (void)coin;
  // TODO(M3): credit the user, assume PROFIT CHAMBER, write EVT_COIN_UNROUTED.
  // Neither hopper is incremented. Fail toward understating hopper stock --
  // an early lock is an annoyance, a promised-but-absent coin is a jam.
}

void persist_mark_coin_in_flight(coin_t coin) {
  (void)coin;
  // TODO(M3)
}

void persist_clear_coin_in_flight() {
  // TODO(M3)
}

money_t persist_daily_profit() {
  return 0;
}

volume_t persist_daily_volume() {
  return 0;
}

uint16_t persist_daily_transactions() {
  return 0;
}

void persist_daily_add(money_t profit, volume_t volume) {
  (void)profit;
  (void)volume;
  // TODO(M3): wear-levelled ring write.
}

void persist_daily_reset() {
  // TODO(M3)
}

void persist_history_add(const history_entry_t *entry) {
  (void)entry;
  // TODO(M3)
}

uint8_t persist_history_count() {
  return 0;
}

const history_entry_t *persist_history_get(uint8_t index) {
  (void)index;
  return nullptr;
}
