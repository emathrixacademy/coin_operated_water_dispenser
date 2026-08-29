#include <Arduino.h>
#include "coin_hopper.h"
#include "persist.h"

// Milestone 2: contract only. Implementation lands in Milestone 3.
//
// Implementation notes:
//   - Outlet sensors are POLLED here, debounced by HOPPER_COUNT_DEBOUNCE_MS.
//     They are not interrupts -- coins leave at 5-10/sec and CLAUDE.md spends
//     both interrupts elsewhere. This depends on the loop staying fast.
//   - After the motor stops, wait HOPPER_SETTLE_MS before declaring the count
//     final. A coin already in the throat still has to fall past the sensor,
//     and calling it short early makes the machine retry a payout that worked.
//   - Retry pays the SHORTFALL only, never the original count again.
//   - Inventory decrements by counted, never by commanded.

void coin_hopper_begin() {
  pinMode(PIN_HOPPER_P1_COUNT, INPUT_PULLUP);
  pinMode(PIN_HOPPER_P5_COUNT, INPUT_PULLUP);
  pinMode(PIN_HOPPER_P1_RUN, OUTPUT);
  pinMode(PIN_HOPPER_P5_RUN, OUTPUT);
  digitalWrite(PIN_HOPPER_P1_RUN, RELAY_OFF);
  digitalWrite(PIN_HOPPER_P5_RUN, RELAY_OFF);
}

void coin_hopper_update() {
  // TODO(M3): poll outlet sensors, run the timeout and retry ladder.
}

void coin_hopper_dispense(hopper_id_t hopper, uint16_t count) {
  (void)hopper;
  (void)count;
  // TODO(M3)
}

payout_result_t coin_hopper_status() {
  return PAYOUT_IDLE;
}

uint16_t coin_hopper_counted() {
  return 0;
}

uint16_t coin_hopper_commanded() {
  return 0;
}

void coin_hopper_clear() {
  // TODO(M3)
}

bool coin_hopper_is_busy() {
  return false;
}

uint16_t coin_hopper_count(hopper_id_t hopper) {
  const inventory_t *inv = persist_inventory();
  if (!inv) return 0;
  return (hopper == HOPPER_P1) ? inv->p1_count : inv->p5_count;
}

bool coin_hopper_is_low() {
  return coin_hopper_count(HOPPER_P1) < HOPPER_LOW_P1 ||
         coin_hopper_count(HOPPER_P5) < HOPPER_LOW_P5;
}

bool coin_hopper_can_cover(money_t centavos) {
  uint16_t p1 = 0, p5 = 0;
  return coin_hopper_plan(centavos, &p1, &p5);
}

bool coin_hopper_plan(money_t centavos, uint16_t *out_p1, uint16_t *out_p5) {
  (void)centavos;
  if (out_p1) *out_p1 = 0;
  if (out_p5) *out_p5 = 0;
  // TODO(M5): denomination strategy. The P1 hopper covers P5 dissipation, so it
  // is not drained first. The worst-case drain arithmetic is reported before
  // this is written -- see scenarios.md case 18.
  return false;
}
