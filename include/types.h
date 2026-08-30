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
// SPEC 7.1: profit_p10 and profit_p20 are separate counters. Without the split
// the chamber's peso value cannot be derived from its count, and reconciling a
// physical collection against the recorded total becomes impossible.
//
// The servo has only THREE positions -- the two profit destinations and the
// unknown destination all share the one profit angle. The split is in the books,
// not in the mechanism.
enum coin_dest_t : uint8_t {
  DEST_P1_HOPPER = 0,
  DEST_P5_HOPPER,
  DEST_PROFIT_P10,
  DEST_PROFIT_P20,
  // Physically routed to the profit chamber, counted in its OWN counter.
  //
  // SPEC 7.1: a third counter, for coins whose denomination the firmware could
  // not identify (3.1) and coins whose routing was interrupted by power loss
  // (3.3). Folding these into profit_p10 or profit_p20 would corrupt the exact
  // peso reconciliation the split exists to provide; leaving them uncounted
  // would mean a physical collection never matches the record with nothing to
  // explain the gap.
  //
  // The chamber is worth 10*p10 + 20*p20, with profit_unknown coins of
  // unstated value alongside it. The discrepancy stays legible to whoever opens
  // the chamber instead of looking like a shortfall.
  DEST_PROFIT_UNKNOWN
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
  EVT_OVERPAY            // hopper paid out more than commanded (SPEC 3.5)
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
// The chamber's peso value is 10*profit_p10 + 20*profit_p20. profit_unknown
// counts coins in the chamber whose denomination was never established, so a
// physical collection that exceeds the derived value has a documented reason.
struct inventory_t {
  uint16_t p1_count;
  uint16_t p5_count;
  uint16_t profit_p10;
  uint16_t profit_p20;
  uint16_t profit_unknown;
};

// An in-flight transaction, persisted so a power cut does not cost the user
// their money. See scenarios.md cases 12 and 13.
struct transaction_t {
  money_t  credit;          // centavos still available to spend
  money_t  inserted;        // centavos inserted this transaction, for the summary
  volume_t target_ml;       // selected target for the current pour
  volume_t dispensed_ml;    // delivered so far toward target_ml
  volume_t total_ml;        // delivered across all pours this transaction
  bool     open;            // true if this transaction survives a reboot
};

#endif  // TYPES_H
