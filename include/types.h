#ifndef TYPES_H
#define TYPES_H

// Shared enums and small value types.
// Kept separate from config.h so modules can include the vocabulary without
// pulling in the whole pin map.

#include <stdint.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Coins
// ---------------------------------------------------------------------------

enum coin_t : uint8_t {
  COIN_NONE = 0,
  COIN_P1,
  COIN_P5,
  COIN_P10,
  COIN_P20,
  // A pulse train inside COIN_PULSE_MAX that matched no denomination.
  //
  // SPEC 3.1: credited at the MINIMUM denomination and routed to profit. Fails
  // against the machine on routing and in the user's favour on credit, which is
  // the correct direction for both. It is a real coin -- the user inserted
  // something -- so crediting nothing would be taking their money.
  //
  // Ordered before COIN_INVALID so persist's `coin > COIN_INVALID` range check
  // keeps working.
  COIN_UNKNOWN,
  COIN_INVALID   // not a coin at all: nothing pending, or an out-of-range train
};

// Where a coin is routed.
//
// P1 and P5 are reused as change. Everything else goes to the locked profit
// chamber, but the chamber's COUNTERS are split by denomination.
//
// SPEC 7.1: box_p10 and box_p20 are separate counters. Without the split
// the chamber's peso value cannot be derived from its count, and reconciling a
// physical collection against the recorded total becomes impossible.
//
// The servo has only THREE positions -- the two profit destinations and the
// unknown destination all share the one profit angle. The split is in the books,
// not in the mechanism.
enum coin_dest_t : uint8_t {
  DEST_P1_HOPPER = 0,
  DEST_P5_HOPPER,

  // The locked coin box, counted BY DENOMINATION -- SPEC 7.1. Five counters,
  // so the box's peso value is 1*p1 + 5*p5 + 10*p10 + 20*p20 and an operator's
  // physical count can be reconciled against the record.
  //
  // In RECIRCULATE only P10 and P20 are routed here; BOX_P1 and BOX_P5 then
  // count only coins whose routing was interrupted by a power cut (3.3). In
  // COLLECT_ALL every coin comes here, P1 and P5 included.
  //
  // These were called "profit" until layout version 4. The box holds TAKINGS,
  // not profit -- profit is takings less the change paid out -- and that
  // distinction matters on a screen an operator reads.
  DEST_BOX_P1,
  DEST_BOX_P5,
  DEST_BOX_P10,
  DEST_BOX_P20,

  // In the box, value never established: a pulse train that matched no
  // denomination (3.1). Folding these into any denomination counter would
  // corrupt the peso reconciliation; leaving them uncounted would mean a
  // physical collection never matches the record with nothing to explain it.
  DEST_BOX_UNKNOWN
};

// Value of a denomination in centavos. COIN_NONE and COIN_INVALID are worth 0.
//
// Defined in billing.cpp rather than coin_acceptor.cpp so it carries no Arduino
// dependency and the host-side arithmetic tests can link it.
money_t coin_value(coin_t coin);

// ---------------------------------------------------------------------------
// Faults
// ---------------------------------------------------------------------------
//
// Every one of these locks the machine, and in every one the coin acceptor is
// disabled FIRST. Never accept money the machine cannot honour.

enum fault_t : uint8_t {
  FAULT_NONE = 0,
  FAULT_OUT_OF_WATER,    // OUT OF WATER -- PLEASE REFILL
  FAULT_LOW_CHANGE,      // LOW CHANGE -- SERVICE REQUIRED
  FAULT_STORAGE_FULL,    // COIN STORAGE FULL
  FAULT_CHANGE_JAM,      // CHANGE JAM -- SERVICE REQUIRED
  FAULT_FLOW_STALL,      // SERVICE REQUIRED (flow stall, case 19)
  FAULT_PUMP_RUNTIME,    // SERVICE REQUIRED (pump ran past PUMP_MAX_RUN_MS)
  FAULT_ACCEPTOR         // SERVICE REQUIRED (acceptor output stuck or noisy)
};

// ---------------------------------------------------------------------------
// Machine state
// ---------------------------------------------------------------------------
//
// The single non-blocking state machine in main.cpp. Transitions live there and
// nowhere else -- a module may report that something happened, but it does not
// change the state itself.

// Names and membership follow SPEC 2.1 exactly. Thirteen states.
enum state_t : uint8_t {
  STATE_BOOT = 0,
  STATE_STANDBY,          // idle, acceptor enabled, waiting for a coin
  STATE_ACCEPTING,        // coins going in, credit accumulating
  STATE_SELECTING,        // user picking a target within their credit
  STATE_AWAITING_BOTTLE,  // confirm pressed, waiting for a bottle
  STATE_DISPENSING,       // valve open, flow counting toward target
  STATE_PAUSED,           // bottle removed mid-pour, grace countdown running
  STATE_SETTLING,         // valve closed, flow tail draining
  STATE_COMPLETE,         // pour done, offering again-or-finish
  STATE_PAYING_CHANGE,    // hoppers running, outlet sensors counting
  STATE_THANK_YOU,        // summary held for the user
  STATE_FAULT,            // locked, acceptor inhibited
  STATE_ADMIN             // admin page, change loading and correction
};

// ---------------------------------------------------------------------------
// History events
// ---------------------------------------------------------------------------
//
// The ring buffer holds ordinary transactions plus service events that explain
// an inventory discrepancy. The distinct tags matter: an unexplained
// discrepancy in a machine full of cash reads as theft, and a technician goes
// looking for a person instead of a power cut.

enum event_tag_t : uint8_t {
  EVT_TRANSACTION = 0,   // normal completed transaction
  EVT_COIN_UNROUTED,     // power lost mid-diverter; coin credited, chamber assumed
  EVT_ADMIN_EDIT,        // inventory corrected by hand, before and after recorded
  EVT_CHANGE_JAM,        // payout fell short; records commanded vs counted
  EVT_FLOW_STALL,        // pour stalled; records volume delivered and refunded
  EVT_DAY_CLOSE,         // midnight rollover; the day's closing totals
  EVT_OVERPAY,           // hopper paid out more than commanded (SPEC 3.5)
  // Power was lost mid-payout. The interrupted leg was deducted from inventory
  // in full and paid again (decisions.md D-4). Tagged so an operator whose
  // physical count is off by up to one leg can see why.
  EVT_LEG_REPAID,
  // The newest transaction slot was unreadable and the one before it said
  // "open". That older state cannot be trusted -- it may be a balance that was
  // already paid out -- so the transaction was closed rather than offered to
  // whoever is standing there (decisions.md R-8). amount_in holds the credit
  // that was NOT resumed, for the operator to settle by hand.
  EVT_TXN_SLOT_LOST,
  // The stored inventory was written by firmware in the other coin routing
  // mode. It was zeroed rather than reinterpreted, and the machine locked until
  // the counts were re-entered. denomination holds the mode it was written in.
  EVT_MODE_CHANGED
};

// ---------------------------------------------------------------------------
// Wall-clock time
// ---------------------------------------------------------------------------
//
// From the DS3231. The daily totals and the Thank You receipt both show a real
// date, so this is not decorative -- see config.h for why a battery-backed,
// temperature-compensated part was specified rather than millis().

struct datetime_t {
  uint16_t year;    // full year, e.g. 2026
  uint8_t  month;   // 1-12
  uint8_t  day;     // 1-31
  uint8_t  hour;    // 0-23, 24-hour
  uint8_t  minute;  // 0-59
  uint8_t  second;  // 0-59
};

// One history entry. Packed to keep twenty of them inside the EEPROM budget.
struct history_entry_t {
  uint32_t timestamp;      // seconds since boot-epoch; see persist.h note
  uint8_t  tag;            // event_tag_t
  uint8_t  denomination;   // coin_t, for EVT_COIN_UNROUTED
  money_t  amount_in;      // centavos
  volume_t volume_out;     // millilitres
  money_t  change_out;     // centavos
};

// Hopper and chamber inventory, mirrored in EEPROM. SPEC 7.1.
//
// The chamber's peso value is 10*box_p10 + 20*box_p20. box_unknown
// counts coins in the chamber whose denomination was never established, so a
// physical collection that exceeds the derived value has a documented reason.
struct inventory_t {
  // The two change hoppers. What change_plan() is allowed to spend.
  uint16_t p1_count;
  uint16_t p5_count;

  // The coin box, by denomination. See coin_dest_t.
  uint16_t box_p1;
  uint16_t box_p5;
  uint16_t box_p10;
  uint16_t box_p20;
  uint16_t box_unknown;

  // COIN_ROUTING_MODE of the firmware that wrote this record.
  //
  // The counts above MEAN DIFFERENT THINGS in the two modes -- in COLLECT_ALL
  // the hoppers are operator-loaded reserves, in RECIRCULATE they are fed by
  // customers -- and firmware cannot detect whether servos are fitted. A unit
  // reflashed from one mode to the other must not silently reinterpret its
  // saved counts, so on a mismatch the inventory is treated as unknown and the
  // machine locks until an operator re-enters it. See persist_begin().
  uint8_t  routing_mode;
  uint8_t  reserved;     // write 0
};

static_assert(sizeof(inventory_t) == 16,
              "inventory_t is part of the EEPROM layout: 16 bytes, version 4");

// What a reboot does with a stored transaction -- SPEC 7.1, decisions.md D-3.
//
// A RESUME CODE, NOT A MACHINE STATE. Storing the raw state_t would break
// silently the day the state list is reordered: the EEPROM would still hold a
// number, and it would now mean a different state. These four values are part
// of the EEPROM layout and must never be renumbered without a layout version
// bump.
//
//   NONE    no transaction                         -> STANDBY
//   CREDIT  ACCEPTING, SELECTING, COMPLETE          -> COMPLETE with the credit
//   POUR    AWAITING_BOTTLE .. SETTLING             -> settle from the last
//                                                      checkpoint, refund the
//                                                      rest, then COMPLETE
//   PAYING  PAYING_CHANGE                           -> PAYING_CHANGE for what is
//                                                      still owed
//
// THE VALVE IS NEVER REOPENED ON BOOT -- SPEC 9 invariant 9. No phase resumes
// into a pour. A machine that pours on power-up with no bottle present is P-1
// wearing a different hat.
enum txn_phase_t : uint8_t {
  TXN_PHASE_NONE = 0,
  TXN_PHASE_CREDIT = 1,
  TXN_PHASE_POUR = 2,
  TXN_PHASE_PAYING = 3
};

// The open transaction, persisted so a power cut does not cost the user their
// money. SPEC 7.1; scenarios.md cases 12 and 13. Layout version 3.
//
// EXACTLY 20 BYTES, and the static_assert below holds it there: the EEPROM ring
// is sized from this struct, and a field added casually would shrink the ring's
// life without anybody deciding to.
//
// Volumes are uint16_t here although volume_t is 32-bit everywhere else. A
// transaction is capped at MAX_TRANSACTION_ML (2000), and four bytes saved per
// field is what lets the ring hold 64 slots.
struct transaction_t {
  // Money the user still has in the machine. THIS RECORD OWNS THE CREDIT. The
  // coin-in-flight marker owns only where a coin physically went, and boot
  // never credits from it -- doing so double-credits (decisions.md, P-2).
  money_t  credit;

  // Centavos inserted this transaction. Thank You, the history entry and the
  // daily profit (inserted less change paid) all need it.
  money_t  inserted;

  // Volume committed for the CURRENT pour; its price is already out of credit.
  // 0 when no pour is committed.
  uint16_t target_ml;

  // Poured in the COMPLETED SEGMENTS OF THE CURRENT POUR -- a segment ends at a
  // bottle pause. Raw sensor millilitres, not yet billed.
  //
  // NOT the same thing as total_ml, and the two must never be merged. Billing
  // rounds down ONCE per pour, on banked_ml + segment_ml. Folding this into a
  // per-segment billed figure would round at every pause and favour the machine
  // once per segment, which is one time too many.
  uint16_t banked_ml;

  // Poured in the segment in progress, as of the last checkpoint. Written each
  // time banked_ml + segment_ml crosses a REFUND_ROUND_ML boundary, so after a
  // power cut the round-down of the sum is exactly what the user would have
  // been billed. Coarser takes money from the user; finer burns EEPROM for
  // nothing (decisions.md D-1).
  uint16_t segment_ml;

  // BILLED volume of EARLIER, COMPLETED pours in this transaction. Already
  // rounded, already paid for. Feeds the history entry and the daily total.
  uint16_t total_ml;

  uint8_t  phase;        // txn_phase_t

  // The payout leg in progress, written when the hopper starts and cleared
  // when it finishes. leg_count == 0 means no leg is running. After a cut
  // mid-leg the count of coins that left is unknowable, so boot deducts the
  // whole commanded leg from inventory and pays it again -- the machine guesses
  // against itself (decisions.md D-4).
  uint8_t  leg_hopper;   // hopper_id_t
  uint8_t  leg_count;    // coins commanded

  uint8_t  reserved;     // keeps the struct at 20 on every target; write 0
};

static_assert(sizeof(transaction_t) == 20,
              "transaction_t is part of the EEPROM layout: 20 bytes, version 3");

#endif  // TYPES_H
