// Port oracle -- EMX-2026-WATERVENDO-01, WO-002-A.
//
// Links the REAL money-path code out of src/ and prints its answer for a large
// fixed grid of inputs. verify_port.js runs the same grid through the
// JavaScript transcription inside watervendo-hmi.html and diffs the two.
//
// src/ is authoritative: on any difference, the JavaScript is wrong.
//
// Build (host toolchain as in test/README.md, one line):
//   g++ -std=gnu++11 -Wall -Wextra -I include -o port_oracle
//       sim/hmi/port_oracle.cpp src/change_plan.cpp src/billing.cpp src/fault_mask.cpp

#include <stdio.h>
#include <stdint.h>

#include "change_plan.h"
#include "billing.h"
#include "fault_mask.h"

// Deterministic LCG, mirrored exactly in verify_port.js so both sides draw the
// same operation sequence.
static uint32_t s_rng = 12345u;
static uint32_t rnd(uint32_t n) {
  s_rng = s_rng * 1103515245u + 12345u;
  return (s_rng >> 16) % n;
}

static void dump_txn(const char *op) {
  transaction_t t;
  billing_store(&t);
  printf("bill %s %ld %ld %ld %ld\n", op, (long)t.credit, (long)t.inserted,
         (long)t.target_ml, (long)t.total_ml);
}

int main() {
  // change_plan over amounts (incl. negative and non-whole-peso) x stock.
  for (int32_t c = -200; c <= 2600; c += 50) {
    for (uint16_t p1 = 0; p1 <= 60; p1++) {
      for (uint16_t p5 = 0; p5 <= 40; p5++) {
        change_plan_t out;
        const bool ok = change_plan(c, p1, p5, &out);
        printf("cp %ld %u %u %d %u %u\n", (long)c, p1, p5, ok ? 1 : 0, out.p1, out.p5);
      }
    }
  }

  for (int32_t v = -50; v <= 2600; v++) {
    printf("rd %ld %ld\n", (long)v, (long)billing_round_down(v));
  }

  for (uint32_t m = 0; m < 256; m++) {
    printf("fm %lu %u %u\n", (unsigned long)m, (unsigned)fault_highest((uint8_t)m),
           (unsigned)fault_persistent_subset((uint8_t)m));
  }

  // Billing: random operation sequences, state dumped after every step.
  static const coin_t COINS[] = { COIN_P1, COIN_P5, COIN_P10, COIN_P20, COIN_UNKNOWN, COIN_NONE };
  for (int run = 0; run < 400; run++) {
    billing_reset();
    for (int step = 0; step < 30; step++) {
      switch (rnd(6)) {
        case 0: case 1: {
          const coin_t c = COINS[rnd(6)];
          billing_add_coin(c);
          dump_txn("add");
          break;
        }
        case 2: {
          // Two statements: C leaves operand evaluation order unspecified, and
          // the JavaScript side must draw from the generator in the same order.
          volume_t v = (volume_t)(rnd(23) * 100) - 100;
          if (rnd(4) == 0) v += 50;
          const bool can = billing_can_select(v);
          const volume_t got = billing_select(v);
          printf("sel %ld %d %ld\n", (long)v, can ? 1 : 0, (long)got);
          dump_txn("sel");
          break;
        }
        case 3: {
          const volume_t d = (volume_t)rnd(2300) - 50;
          billing_settle_partial(d);
          dump_txn("part");
          break;
        }
        case 4:
          billing_settle_complete(0);
          dump_txn("comp");
          break;
        default:
          billing_cancel_selection();
          dump_txn("canc");
          break;
      }
      printf("due %ld %d %ld\n", (long)billing_change_due(), billing_at_ceiling() ? 1 : 0,
             (long)billing_max_selectable_ml());
    }
  }
  return 0;
}
