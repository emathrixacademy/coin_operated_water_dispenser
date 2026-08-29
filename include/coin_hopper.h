#ifndef COIN_HOPPER_H
#define COIN_HOPPER_H

// Coin hoppers -- change payout, outlet counting, jam retry.
//
// THE CENTRAL RULE OF THIS MODULE:
// A payout is not complete because the hopper was told to run. It is complete
// when the outlet sensor counts the coins out. Never assume a payout succeeded.
//
// Inventory is decremented by coins COUNTED, never by coins commanded. If the
// sensor did not see it leave, the machine still owns it.
//
// Undercount within HOPPER_TIMEOUT_MS triggers a retry for the shortfall only,
// up to HOPPER_RETRY_MAX. After that the machine locks with CHANGE JAM --
// SERVICE REQUIRED and the shortfall is written to the history ring buffer so a
// technician can see what was actually paid.

#include "types.h"

enum hopper_id_t : uint8_t {
  HOPPER_P1 = 0,
  HOPPER_P5
};

enum payout_result_t : uint8_t {
  PAYOUT_IDLE = 0,
  PAYOUT_RUNNING,
  PAYOUT_COMPLETE,   // counted == commanded
  PAYOUT_JAMMED      // short after HOPPER_RETRY_MAX; machine must lock
};

void coin_hopper_begin();
void coin_hopper_update();

// Command a payout of `count` coins from one hopper. Non-blocking.
// Ignored if a payout is already running.
void coin_hopper_dispense(hopper_id_t hopper, uint16_t count);

payout_result_t coin_hopper_status();

// Coins the outlet sensor actually counted for the payout in progress or just
// finished. This -- not the commanded count -- is what the inventory and the
// user's change are reconciled against.
uint16_t coin_hopper_counted();

// Commanded count for the payout in progress or just finished. Compared
// against counted() to size a retry and to record the shortfall on a jam.
uint16_t coin_hopper_commanded();

// Clears a COMPLETE or JAMMED status back to IDLE. The caller must have already
// consumed counted() and updated the inventory.
void coin_hopper_clear();

bool coin_hopper_is_busy();

// Inventory queries. These read the in-RAM mirror maintained by persist.
uint16_t coin_hopper_count(hopper_id_t hopper);

// True if either hopper is below its low-change threshold. The machine locks
// on this -- it must not take money it cannot make change for.
bool coin_hopper_is_low();

// Can the hoppers physically cover this much change right now?
// Checked BEFORE accepting any coin for a transaction, against the worst case
// for the ceiling amount, not against what the user has inserted so far.
bool coin_hopper_can_cover(money_t centavos);

// Plan a payout across the two hoppers.
//
// The P1 hopper covers P5 dissipation, so it is not drained first -- see
// CLAUDE.md. Writes the coin counts for each denomination and returns false if
// the amount cannot be made from current stock, in which case the caller must
// not promise it.
bool coin_hopper_plan(money_t centavos, uint16_t *out_p1, uint16_t *out_p5);

#endif  // COIN_HOPPER_H
