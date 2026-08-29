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
  COIN_INVALID   // pulse train matched no denomination
};

// Where a coin is routed. P1 and P5 are reused as change; P10 and P20 go to
// the locked profit chamber.
enum coin_dest_t : uint8_t {
  DEST_P1_HOPPER = 0,
  DEST_P5_HOPPER,
  DEST_PROFIT
};

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
  FAULT_PUMP_RUNTIME     // SERVICE REQUIRED (pump ran past PUMP_MAX_RUN_MS)
};

// ---------------------------------------------------------------------------
// Machine state
// ---------------------------------------------------------------------------
//
// The single non-blocking state machine in main.cpp. Transitions live there and
// nowhere else -- a module may report that something happened, but it does not
// change the state itself.

enum state_t : uint8_t {
  STATE_BOOT = 0,
  STATE_STANDBY,          // idle, acceptor enabled, waiting for a coin
  STATE_ACCEPTING,        // coins going in, credit accumulating
  STATE_SELECT_VOLUME,    // user picking a target within their credit
  STATE_WAIT_BOTTLE,      // selection made, waiting for a bottle
  STATE_DISPENSING,       // valve open, flow counting toward target
  STATE_BOTTLE_REMOVED,   // paused mid-pour, grace countdown running
  STATE_POUR_COMPLETE,    // target reached, offering again-or-finish
  STATE_PAYING_CHANGE,    // hoppers running, outlet sensors counting
  STATE_THANK_YOU,        // summary held for the user
  STATE_LOCKED,           // a fault is active
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
  EVT_FLOW_STALL         // pour stalled; records volume delivered and refunded
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

// Hopper inventory, mirrored in EEPROM.
struct inventory_t {
  uint16_t p1_count;
  uint16_t p5_count;
  uint16_t profit_p10;
  uint16_t profit_p20;
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
