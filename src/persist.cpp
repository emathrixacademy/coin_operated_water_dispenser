#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include "persist.h"
#include "eeprom_record.h"
#include "coin_route.h"

// EEPROM persistence.
//
// Write per transaction and at each inventory change. NEVER in loop().
// EEPROM.update() everywhere -- never EEPROM.write().

// ---------------------------------------------------------------------------
// On-EEPROM payload shapes
// ---------------------------------------------------------------------------

struct inflight_t {
  uint8_t coin;     // coin_t; COIN_NONE when nothing is in flight
};

// Ring slots. `seq` is a monotonic write counter: the newest valid slot wins on
// boot, and the counter itself is the honest wear figure for a technician --
// "this unit has done N writes" rather than "it is somewhere in a ring".
struct txn_slot_t {
  uint32_t seq;
  transaction_t txn;
};

struct inflight_slot_t {
  uint32_t seq;
  inflight_t f;
};

struct daily_t {
  uint32_t seq;     // newest valid slot wins on boot
  money_t  profit;
  volume_t volume;
  uint16_t txns;
};

struct hist_slot_t {
  uint32_t seq;     // ordering, since timestamps restart with the boot epoch
  history_entry_t e;
};

// Persistent fault flags, one bit per fault_t. See SPEC 6.1 and 7.1.
struct fault_flags_t {
  uint8_t flags;
};

// ---------------------------------------------------------------------------
// Region budget
// ---------------------------------------------------------------------------
//
// Checked at compile time so a struct that grows cannot silently start
// overwriting the region after it. Silent overlap here corrupts the coin
// inventory, which is money.

#define SLOT_DAILY_SIZE    (RECORD_OVERHEAD + (int)sizeof(daily_t))
#define SLOT_HIST_SIZE     (RECORD_OVERHEAD + (int)sizeof(hist_slot_t))
#define SLOT_TXN_SIZE      (RECORD_OVERHEAD + (int)sizeof(txn_slot_t))
#define SLOT_INFLIGHT_SIZE (RECORD_OVERHEAD + (int)sizeof(inflight_slot_t))

static_assert(EEPROM_ADDR_INVENTORY + RECORD_OVERHEAD + (int)sizeof(inventory_t)
                  <= EEPROM_ADDR_FAULTS,
              "inventory record overruns the fault-flags region");
static_assert(EEPROM_ADDR_FAULTS + RECORD_OVERHEAD + (int)sizeof(fault_flags_t)
                  <= EEPROM_ADDR_DAILY_RING,
              "fault-flags record overruns the daily ring");
static_assert(EEPROM_ADDR_DAILY_RING + (DAILY_RING_SLOTS * SLOT_DAILY_SIZE)
                  <= EEPROM_ADDR_HISTORY,
              "daily ring overruns the history region");
static_assert(EEPROM_ADDR_HISTORY + (HISTORY_ENTRIES * SLOT_HIST_SIZE)
                  <= EEPROM_ADDR_OPEN_TXN_RING,
              "history ring overruns the open-transaction ring");
// 4 B sequence + 20 B transaction + 4 B framing. If this moves, the ring
// arithmetic in config.h and decisions.md is wrong and must be redone.
static_assert(SLOT_TXN_SIZE == 28, "open-transaction slot is 28 bytes, layout 3");
static_assert(EEPROM_ADDR_OPEN_TXN_RING + (TXN_RING_SLOTS * SLOT_TXN_SIZE)
                  <= EEPROM_ADDR_INFLIGHT_RING,
              "open-transaction ring overruns the in-flight ring");
static_assert(EEPROM_ADDR_INFLIGHT_RING
                  + (INFLIGHT_RING_SLOTS * SLOT_INFLIGHT_SIZE) <= 4096,
              "in-flight ring overruns the Mega's 4KB EEPROM");

// ---------------------------------------------------------------------------
// Raw record access
// ---------------------------------------------------------------------------
//
// Payloads are small and bounded by the structs above, so one fixed scratch
// buffer serves every record. No dynamic allocation -- CLAUDE.md.

#define RECORD_SCRATCH 48
static_assert((int)sizeof(hist_slot_t) + RECORD_OVERHEAD <= RECORD_SCRATCH,
              "scratch buffer too small for the largest record");

static bool record_load(int addr, uint8_t *payload, uint8_t len) {
  uint8_t buf[RECORD_SCRATCH];
  const uint8_t total = (uint8_t)(len + RECORD_OVERHEAD);
  for (uint8_t i = 0; i < total; i++) buf[i] = EEPROM.read(addr + i);
  return record_unpack(buf, payload, len);
}

// What kind of slot sits at `addr` -- blank, valid or corrupt. See
// record_classify() for why blank and corrupt must not be confused.
static slot_kind_t record_kind(int addr, uint8_t len) {
  uint8_t buf[RECORD_SCRATCH];
  const uint8_t total = (uint8_t)(len + RECORD_OVERHEAD);
  for (uint8_t i = 0; i < total; i++) buf[i] = EEPROM.read(addr + i);
  return record_classify(buf, len);
}

static void record_save(int addr, const uint8_t *payload, uint8_t len) {
  uint8_t buf[RECORD_SCRATCH];
  record_pack(buf, payload, len);
  const uint8_t total = (uint8_t)(len + RECORD_OVERHEAD);
  // update(), not write(). Unchanged bytes cost nothing, and most of a record
  // is unchanged on most writes.
  for (uint8_t i = 0; i < total; i++) EEPROM.update(addr + i, buf[i]);
}

// ---------------------------------------------------------------------------
// State mirrors
// ---------------------------------------------------------------------------

static inventory_t s_inventory;
static transaction_t s_open_txn;

// Open-transaction ring position. s_txn_seq is the cumulative write count and
// is what Admin reports as the wear figure.
static uint32_t s_txn_seq = 0;
static uint8_t s_txn_slot = 0;

// In-flight coin ring, plus the RAM mirror of its current value.
static uint32_t s_inflight_seq = 0;
static uint8_t s_inflight_slot = 0;
static coin_t s_inflight_coin = COIN_NONE;

static daily_t s_daily;
static uint8_t s_daily_slot = 0;
static uint32_t s_hist_seq = 0;
static uint8_t s_hist_count = 0;
static history_entry_t s_hist_scratch;
static bool s_initialised = false;

// Set when the stored inventory was written in the other routing mode and was
// zeroed rather than reinterpreted. Holds the mode it was written in.
static bool s_mode_changed = false;
static uint8_t s_mode_was = 0;

static void inventory_commit() {
  // Every inventory record carries the mode of the firmware that wrote it.
  s_inventory.routing_mode = (uint8_t)COIN_ROUTING_MODE;
  s_inventory.reserved = 0;
  record_save(EEPROM_ADDR_INVENTORY, (const uint8_t *)&s_inventory, sizeof(s_inventory));
}

static void daily_commit() {
  // Wear-levelling: advance to the next slot on every write rather than
  // rewriting one address. Daily counters change far more often than anything
  // else here, and 8 slots multiplies the life of the region by 8.
  s_daily.seq++;
  s_daily_slot = (uint8_t)((s_daily_slot + 1) % DAILY_RING_SLOTS);
  record_save(EEPROM_ADDR_DAILY_RING + (s_daily_slot * SLOT_DAILY_SIZE),
              (const uint8_t *)&s_daily, sizeof(s_daily));
}

void persist_begin() {
  s_initialised = false;

  // --- Inventory -------------------------------------------------------
  //
  // A failed load here is the case that matters. On a virgin chip every cell
  // reads 0xFF, and an unvalidated read would give 65535 coins in each hopper
  // -- a machine that believes it can make change it does not physically have,
  // which is a jam under the first paying user.
  //
  // So: zero it, flag it, and let the Admin page tell the technician the
  // inventory was reset rather than restored. Zero is also the correct failure
  // direction -- it locks the machine on LOW CHANGE until a human loads the
  // hoppers and enters real counts. Fail toward the understatement.
  s_mode_changed = false;
  if (!record_load(EEPROM_ADDR_INVENTORY, (uint8_t *)&s_inventory, sizeof(s_inventory))) {
    memset(&s_inventory, 0, sizeof(s_inventory));
    inventory_commit();
    s_initialised = true;
  } else if (s_inventory.routing_mode != (uint8_t)COIN_ROUTING_MODE) {
    // -------------------------------------------------------------------
    // A valid record, written by firmware in the OTHER routing mode.
    //
    // The same numbers mean different things in the two modes, and the
    // firmware cannot see whether servos are fitted. Reinterpreting them would
    // be a silent error in what the machine believes it can pay out. So this
    // is handled exactly like an unreadable inventory: zero it, which locks the
    // machine on LOW CHANGE until an operator counts the hoppers and enters
    // real figures. The box counters go too -- a changeover is when the box is
    // emptied.
    //
    // A history entry is written below, once the history ring has been
    // scanned, so the operator can see WHY the counts are zero.
    // -------------------------------------------------------------------
    s_mode_changed = true;
    s_mode_was = s_inventory.routing_mode;
    memset(&s_inventory, 0, sizeof(s_inventory));
    inventory_commit();
    s_initialised = true;
  }

  // --- Open transaction ring --------------------------------------------
  //
  // Scan every slot, take the valid one with the highest sequence number. A
  // slot that fails its CRC is skipped rather than trusted, so one degraded
  // cell costs at most the newest write and the previous one still restores.
  memset(&s_open_txn, 0, sizeof(s_open_txn));
  s_open_txn.phase = TXN_PHASE_NONE;
  s_txn_seq = 0;
  s_txn_slot = 0;
  bool txn_found = false;
  for (uint8_t i = 0; i < TXN_RING_SLOTS; i++) {
    txn_slot_t slot;
    if (!record_load(EEPROM_ADDR_OPEN_TXN_RING + ((int)i * SLOT_TXN_SIZE),
                     (uint8_t *)&slot, sizeof(slot))) {
      continue;
    }
    if (slot.seq > s_txn_seq) {
      s_txn_seq = slot.seq;
      s_txn_slot = i;
      s_open_txn = slot.txn;
      txn_found = true;
    }
  }

  // An unknown phase value can only come from a firmware whose phase list
  // differed. Treat it as no transaction rather than guessing which one it was.
  if (s_open_txn.phase > (uint8_t)TXN_PHASE_PAYING) {
    s_open_txn.phase = TXN_PHASE_NONE;
  }

  // ---------------------------------------------------------------------
  // R-8: IS THE NEWEST READABLE RECORD REALLY THE NEWEST?
  //
  // The slot after it is where the next write went. If that slot is corrupt
  // -- a write torn by the power cut that caused this boot, or a worn cell --
  // then what was just loaded is the state BEFORE the last write. If it says
  // "open, with credit", the lost write may well have been the one that paid
  // that credit out and closed it.
  //
  // Resuming it offers money to whoever is standing there, every time it
  // happens. Closing it costs one user once, and that user can complain to an
  // operator who can settle by hand. So: close it, and say so in the history.
  // The amount not resumed is kept for the history entry written below, once
  // the history ring has been scanned.
  // ---------------------------------------------------------------------
  money_t slot_lost_credit = 0;
  bool slot_lost = false;
  if (txn_found && s_open_txn.phase != TXN_PHASE_NONE) {
    const uint8_t next = (uint8_t)((s_txn_slot + 1) % TXN_RING_SLOTS);
    const slot_kind_t kind =
        record_kind(EEPROM_ADDR_OPEN_TXN_RING + ((int)next * SLOT_TXN_SIZE),
                    sizeof(txn_slot_t));
    if (!ring_newest_trusted(kind)) {
      slot_lost = true;
      slot_lost_credit = s_open_txn.credit;
    }
  }

  // --- In-flight coin ring ----------------------------------------------
  s_inflight_seq = 0;
  s_inflight_slot = 0;
  s_inflight_coin = COIN_NONE;
  for (uint8_t i = 0; i < INFLIGHT_RING_SLOTS; i++) {
    inflight_slot_t slot;
    if (!record_load(EEPROM_ADDR_INFLIGHT_RING + ((int)i * SLOT_INFLIGHT_SIZE),
                     (uint8_t *)&slot, sizeof(slot))) {
      continue;
    }
    if (slot.seq > s_inflight_seq) {
      s_inflight_seq = slot.seq;
      s_inflight_slot = i;
      s_inflight_coin = (slot.f.coin > (uint8_t)COIN_INVALID)
                            ? COIN_NONE : (coin_t)slot.f.coin;
    }
  }

#ifdef DEBUG
  // Ring position at boot. First question when a unit misbehaves is how far
  // through its rings it is, and this answers it before anyone opens a cabinet.
  Serial.print(F("[eeprom] txn ring slot "));
  Serial.print(s_txn_slot);
  Serial.print(F("/"));
  Serial.print((int)TXN_RING_SLOTS);
  Serial.print(F(" writes="));
  Serial.print(s_txn_seq);
  Serial.print(F("  inflight slot "));
  Serial.print(s_inflight_slot);
  Serial.print(F("/"));
  Serial.print((int)INFLIGHT_RING_SLOTS);
  Serial.print(F(" writes="));
  Serial.println(s_inflight_seq);
#endif

  // --- Daily ring -------------------------------------------------------
  //
  // Scan every slot and take the valid one with the highest sequence number.
  // Invalid slots are simply skipped: on a virgin chip none are valid and the
  // totals start at zero, which is correct.
  memset(&s_daily, 0, sizeof(s_daily));
  bool found = false;
  for (uint8_t i = 0; i < DAILY_RING_SLOTS; i++) {
    daily_t slot;
    if (!record_load(EEPROM_ADDR_DAILY_RING + (i * SLOT_DAILY_SIZE),
                     (uint8_t *)&slot, sizeof(slot))) {
      continue;
    }
    if (!found || slot.seq > s_daily.seq) {
      s_daily = slot;
      s_daily_slot = i;
      found = true;
    }
  }

  // --- History ----------------------------------------------------------
  s_hist_count = 0;
  s_hist_seq = 0;
  for (uint8_t i = 0; i < HISTORY_ENTRIES; i++) {
    hist_slot_t slot;
    if (!record_load(EEPROM_ADDR_HISTORY + (i * SLOT_HIST_SIZE),
                     (uint8_t *)&slot, sizeof(slot))) {
      continue;
    }
    s_hist_count++;
    if (slot.seq > s_hist_seq) s_hist_seq = slot.seq;
  }

  // R-8, second half. Done here because the history ring had to be scanned
  // before anything could be appended to it.
  //
  // History FIRST, then the close: if power fails between the two, the next
  // boot finds the same untrusted slot and does this again, which costs a
  // duplicate history line. The other order could close the transaction and
  // lose the only record that it ever happened.
  if (slot_lost) {
    history_entry_t e;
    memset(&e, 0, sizeof(e));
    // The clock has not been started yet at this point in setup(), and a
    // made-up time is worse than an honest "unknown".
    e.timestamp = RTC_TIMESTAMP_INVALID;
    e.tag = EVT_TXN_SLOT_LOST;
    e.amount_in = slot_lost_credit;
    persist_history_add(&e);

    // Overwrites the corrupt slot, so the ring heals itself.
    persist_txn_close();
  }

  if (s_mode_changed) {
    history_entry_t e;
    memset(&e, 0, sizeof(e));
    e.timestamp = RTC_TIMESTAMP_INVALID;   // the clock is not running yet
    e.tag = EVT_MODE_CHANGED;
    e.denomination = s_mode_was;
    persist_history_add(&e);
  }
}

bool persist_mode_changed() {
  return s_mode_changed;
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
  // Called with coins COUNTED -- by the diverter after physical commit, or by
  // the hopper after the outlet sensor saw them leave. NEVER with a commanded
  // count.
  uint16_t *field;
  int32_t ceiling;
  switch (dest) {
    case DEST_P1_HOPPER:
      field = &s_inventory.p1_count;    ceiling = HOPPER_CAPACITY; break;
    case DEST_P5_HOPPER:
      field = &s_inventory.p5_count;    ceiling = HOPPER_CAPACITY; break;
    // SPEC 7.1: one counter per denomination, so the box's peso value is
    // derivable and a physical collection can be reconciled against it.
    case DEST_BOX_P1:
      field = &s_inventory.box_p1;   ceiling = COIN_BOX_CAPACITY; break;
    case DEST_BOX_P5:
      field = &s_inventory.box_p5;   ceiling = COIN_BOX_CAPACITY; break;
    case DEST_BOX_P10:
      field = &s_inventory.box_p10;  ceiling = COIN_BOX_CAPACITY; break;
    case DEST_BOX_P20:
      field = &s_inventory.box_p20;  ceiling = COIN_BOX_CAPACITY; break;
    // SPEC 7.1: an unrecognised coin is physically in the chamber but has no
    // known value, so it gets its OWN counter. Recording it against either
    // denomination would corrupt the peso reconciliation the split exists to
    // provide; not recording it at all would leave a physical collection
    // unexplainably larger than the record.
    case DEST_BOX_UNKNOWN:
      field = &s_inventory.box_unknown; ceiling = COIN_BOX_CAPACITY; break;
    default:
      return;
  }

  // Clamp rather than wrap. An underflow here would turn an empty hopper into
  // 65535 coins, which is the exact failure the framing above exists to stop --
  // it must not be reintroduced by arithmetic.
  const int32_t next = (int32_t)*field + delta;
  if (next < 0) {
    *field = 0;
  } else if (next > ceiling) {
    *field = (uint16_t)ceiling;
  } else {
    *field = (uint16_t)next;
  }

  inventory_commit();
}

void persist_inventory_set(coin_dest_t dest, uint16_t count) {
  uint16_t before;
  switch (dest) {
    case DEST_P1_HOPPER:
      if (count > HOPPER_CAPACITY) count = HOPPER_CAPACITY;
      before = s_inventory.p1_count;   s_inventory.p1_count = count;   break;
    case DEST_P5_HOPPER:
      if (count > HOPPER_CAPACITY) count = HOPPER_CAPACITY;
      before = s_inventory.p5_count;   s_inventory.p5_count = count;   break;
    case DEST_BOX_P1:
      if (count > COIN_BOX_CAPACITY) count = COIN_BOX_CAPACITY;
      before = s_inventory.box_p1; s_inventory.box_p1 = count; break;
    case DEST_BOX_P5:
      if (count > COIN_BOX_CAPACITY) count = COIN_BOX_CAPACITY;
      before = s_inventory.box_p5; s_inventory.box_p5 = count; break;
    case DEST_BOX_P10:
      if (count > COIN_BOX_CAPACITY) count = COIN_BOX_CAPACITY;
      before = s_inventory.box_p10; s_inventory.box_p10 = count; break;
    case DEST_BOX_P20:
      if (count > COIN_BOX_CAPACITY) count = COIN_BOX_CAPACITY;
      before = s_inventory.box_p20; s_inventory.box_p20 = count; break;
    case DEST_BOX_UNKNOWN:
      if (count > COIN_BOX_CAPACITY) count = COIN_BOX_CAPACITY;
      before = s_inventory.box_unknown; s_inventory.box_unknown = count; break;
    default:
      return;
  }
  inventory_commit();

  // This is the one write path that can desync stored inventory from physical
  // hopper contents, so it leaves a trail: before and after, tagged distinctly.
  history_entry_t e;
  memset(&e, 0, sizeof(e));
  e.timestamp = millis() / 1000UL;
  e.tag = EVT_ADMIN_EDIT;
  e.amount_in = (money_t)before;
  e.change_out = (money_t)count;
  persist_history_add(&e);
}

// =========================================================================
// The open-transaction ring.
//
// EVERY write to the open transaction goes through here, and every one of them
// advances a slot. That is the whole mechanism: no cell is written twice in a
// row, so the region's life is multiplied by TXN_RING_SLOTS.
//
// The per-coin write this makes affordable is DELIBERATE and is not to be
// optimised away -- see docs/decisions.md. A power cut between the last coin
// and the selection would otherwise take the user's money with no record of it,
// and on Philippine mains in a school that is not a hypothetical.
// =========================================================================
static void txn_commit() {
  s_txn_seq++;
  s_txn_slot = (uint8_t)((s_txn_slot + 1) % TXN_RING_SLOTS);

  txn_slot_t slot;
  slot.seq = s_txn_seq;
  slot.txn = s_open_txn;
  record_save(EEPROM_ADDR_OPEN_TXN_RING + ((int)s_txn_slot * SLOT_TXN_SIZE),
              (const uint8_t *)&slot, sizeof(slot));

#ifdef DEBUG
  // Slot advance. If a unit ever starts losing transactions, the first question
  // is whether the ring wrapped or a CRC failed -- this answers it without
  // instrumenting anything.
  if (s_txn_slot == 0) {
    Serial.print(F("[eeprom] txn ring wrapped, seq="));
    Serial.println(s_txn_seq);
  }
#endif
}

// A record written through open() or update() is an OPEN transaction, whatever
// phase the caller left in it. A caller that has not set one gets CREDIT, the
// phase that resumes to COMPLETE with the balance and touches nothing else --
// the safe reading of "there is money in here and I know nothing more".
//
// TODO(WO-005 item 3): main.cpp does not set phases yet, so every record is
// CREDIT today. POUR and PAYING arrive with the P-4/P-5 fixes.
static void txn_store(const transaction_t *txn) {
  s_open_txn = *txn;
  if (s_open_txn.phase == TXN_PHASE_NONE ||
      s_open_txn.phase > (uint8_t)TXN_PHASE_PAYING) {
    s_open_txn.phase = TXN_PHASE_CREDIT;
  }
  s_open_txn.reserved = 0;
  txn_commit();
}

void persist_txn_open(const transaction_t *txn) {
  if (!txn) return;
  txn_store(txn);
}

void persist_txn_update(const transaction_t *txn) {
  if (!txn || s_open_txn.phase == TXN_PHASE_NONE) return;
  txn_store(txn);
}

void persist_txn_close() {
  // Only the phase changes. The figures are left as they were so the closed
  // record still says what the last transaction held, which is what a
  // technician reading a dump wants to see.
  s_open_txn.phase = TXN_PHASE_NONE;
  s_open_txn.leg_count = 0;
  txn_commit();
}

uint32_t persist_txn_ring_writes() {
  return s_txn_seq;
}

uint8_t persist_txn_ring_slot() {
  return s_txn_slot;
}

uint32_t persist_inflight_ring_writes() {
  return s_inflight_seq;
}

bool persist_has_open_txn() {
  return s_open_txn.phase != TXN_PHASE_NONE;
}

const transaction_t *persist_open_txn() {
  return &s_open_txn;
}

// =========================================================================
// The in-flight coin ring.
//
// TWO writes per coin -- one marking it before the servo moves, one clearing it
// once the diverter settles -- which made this the shortest-lived cell in the
// machine at a single address: 25 days at worst case, 100 transactions a day.
// More slots than the transaction ring because it is written twice as often and
// each slot is a quarter the size.
// =========================================================================
static void inflight_commit(coin_t coin) {
  s_inflight_seq++;
  s_inflight_slot = (uint8_t)((s_inflight_slot + 1) % INFLIGHT_RING_SLOTS);

  inflight_slot_t slot;
  slot.seq = s_inflight_seq;
  slot.f.coin = (uint8_t)coin;
  record_save(EEPROM_ADDR_INFLIGHT_RING
                  + ((int)s_inflight_slot * SLOT_INFLIGHT_SIZE),
              (const uint8_t *)&slot, sizeof(slot));

#ifdef DEBUG
  if (s_inflight_slot == 0) {
    Serial.print(F("[eeprom] inflight ring wrapped, seq="));
    Serial.println(s_inflight_seq);
  }
#endif
}

void persist_mark_coin_in_flight(coin_t coin) {
  s_inflight_coin = coin;
  inflight_commit(coin);
}

void persist_clear_coin_in_flight() {
  s_inflight_coin = COIN_NONE;
  inflight_commit(COIN_NONE);
}

coin_t persist_coin_in_flight() {
  // COLLECT_ALL never writes the in-flight ring -- with the flaps pinned there
  // is no travel for a power cut to interrupt -- so anything found in it is a
  // leftover from a RECIRCULATE build and must not be reconciled.
  if (COIN_ROUTING_MODE == COIN_ROUTING_COLLECT_ALL) return COIN_NONE;

  // Served from the RAM mirror established at boot. Re-reading EEPROM here
  // would scan 64 slots on every call for a value already known.
  if ((uint8_t)s_inflight_coin > (uint8_t)COIN_INVALID) return COIN_NONE;
  return s_inflight_coin;
}

void persist_reconcile_unrouted_coin(coin_t coin) {
  if (coin == COIN_NONE || coin == COIN_INVALID) return;

  // POLICY -- SPEC 3.3 (decided, not to be quietly changed):
  //   1. The user keeps the credit -- handled by the caller restoring the
  //      transaction. They inserted real money.
  //   2. The coin is assumed to have reached the PROFIT CHAMBER. The PROFIT
  //      counter for its denomination is incremented; NEITHER HOPPER IS,
  //      whatever denomination it was.
  //   3. The event is tagged distinctly in the history.
  //
  // Understating hopper stock makes the machine lock early, which is an
  // annoyance. Overstating it makes the machine promise change it does not
  // hold, which is a jam and an angry user. ALWAYS FAIL TOWARD THE
  // UNDERSTATEMENT.
  //
  // The distinct tag exists so a service tech reading history sees why a
  // physical count differs from the recorded one, rather than suspecting theft.
  //
  // (An earlier revision incremented no counter at all, on the argument that
  // the coin's location was genuinely unknown. Spec 3.3 has since decided this:
  // assume profit. A P1 or P5 that actually reached its hopper is then
  // understated by one, which is the safe direction.)
  history_entry_t e;
  memset(&e, 0, sizeof(e));
  e.timestamp = millis() / 1000UL;
  e.tag = EVT_COIN_UNROUTED;
  e.denomination = (uint8_t)coin;
  e.amount_in = coin_value(coin);
  persist_history_add(&e);

  // Assume the profit chamber, per the policy above. coin_destination() would
  // send a P1 or P5 to a hopper, so the profit destination is chosen here
  // explicitly rather than by asking where the coin was headed.
  // coin_route_unrouted() always answers with a BOX counter, never a hopper,
  // whatever the coin was headed for -- and it keeps the denomination, so a P1
  // or P5 caught mid-route is recorded as "one P5, assumed in the box" rather
  // than as a coin of unknown value. Before layout version 4 there was no box
  // counter for P1 or P5 and these went to box_unknown.
  persist_inventory_add(coin_route_unrouted(coin), +1);

  persist_clear_coin_in_flight();
}

// ---------------------------------------------------------------------------
// Persistent fault flags -- SPEC 6.1 and 7.1
// ---------------------------------------------------------------------------
//
// A persistent fault that clears on power cycle is worse than not claiming
// persistence at all: the operator learns that the fix is a reboot, the coins
// stay jammed, and the machine returns to accepting money it cannot pay out.
//
// Stored with the same framing as every other record, so a corrupt fault cell
// reads as "no fault stored" rather than as a garbage fault code. That failure
// direction is deliberate but not free -- a corrupted cell loses a jam lockout.
// The physical jam is still there and the next payout re-raises it.

uint8_t persist_fault_flags() {
  fault_flags_t f;
  if (!record_load(EEPROM_ADDR_FAULTS, (uint8_t *)&f, sizeof(f))) return 0;
  return f.flags;
}

void persist_fault_flags_set(uint8_t flags) {
  fault_flags_t f;
  f.flags = flags;
  record_save(EEPROM_ADDR_FAULTS, (const uint8_t *)&f, sizeof(f));
}

money_t persist_daily_profit() {
  return s_daily.profit;
}

volume_t persist_daily_volume() {
  return s_daily.volume;
}

uint16_t persist_daily_transactions() {
  return s_daily.txns;
}

void persist_daily_add(money_t profit, volume_t volume) {
  s_daily.profit += profit;
  s_daily.volume += volume;
  s_daily.txns++;
  daily_commit();
}

// =========================================================================
// THE ONLY PATH THAT ZEROES THE DAILY TOTALS.
//
// It writes the closing figures to the history ring BEFORE zeroing them, and
// the ordering is the entire point: the daily total is what the owner counts
// cash against, and once it is zeroed there is nothing anywhere that remembers
// what it was. A rollover that zeroes first and writes second loses the day on
// any power cut in between.
//
// Both the midnight rollover and the Admin reset come through here so neither
// can grow a path that skips the write.
// =========================================================================
static void daily_close(uint32_t timestamp, uint8_t tag) {
  history_entry_t e;
  memset(&e, 0, sizeof(e));
  e.timestamp  = timestamp;
  e.tag        = tag;
  e.amount_in  = s_daily.profit;   // the day's takings
  e.volume_out = s_daily.volume;   // the day's water
  e.change_out = (money_t)s_daily.txns;
  persist_history_add(&e);

  s_daily.profit = 0;
  s_daily.volume = 0;
  s_daily.txns = 0;
  daily_commit();
}

void persist_daily_rollover(uint32_t timestamp) {
  daily_close(timestamp, (uint8_t)EVT_DAY_CLOSE);
}

void persist_daily_reset(uint32_t timestamp) {
  // Admin reset. Tagged distinctly from a midnight rollover so a technician
  // reading the history can tell a day that ended on its own from one a person
  // ended by hand.
  daily_close(timestamp, (uint8_t)EVT_ADMIN_EDIT);
}

void persist_history_add(const history_entry_t *entry) {
  if (!entry) return;
  hist_slot_t slot;
  slot.seq = ++s_hist_seq;
  slot.e = *entry;
  const uint8_t idx = (uint8_t)((s_hist_seq - 1) % HISTORY_ENTRIES);
  record_save(EEPROM_ADDR_HISTORY + (idx * SLOT_HIST_SIZE),
              (const uint8_t *)&slot, sizeof(slot));
  if (s_hist_count < HISTORY_ENTRIES) s_hist_count++;
}

uint8_t persist_history_count() {
  return s_hist_count;
}

const history_entry_t *persist_history_get(uint8_t index) {
  if (index >= s_hist_count) return nullptr;

  // index 0 is the most recent.
  const uint32_t want = s_hist_seq - index;
  hist_slot_t slot;
  const uint8_t idx = (uint8_t)((want - 1) % HISTORY_ENTRIES);
  if (!record_load(EEPROM_ADDR_HISTORY + (idx * SLOT_HIST_SIZE),
                   (uint8_t *)&slot, sizeof(slot))) {
    return nullptr;
  }
  if (slot.seq != want) return nullptr;

  s_hist_scratch = slot.e;
  return &s_hist_scratch;
}
