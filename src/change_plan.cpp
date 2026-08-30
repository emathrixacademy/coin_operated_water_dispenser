// Change payout planning -- SPEC 3.4.
//
// No Arduino dependency by design. See change_plan.h.

#include "change_plan.h"

bool change_plan(money_t centavos, uint16_t p1_stock, uint16_t p5_stock,
                 change_plan_t *out) {
  if (out) {
    out->p1 = 0;
    out->p5 = 0;
  }

  if (centavos < 0) return false;

  // Nothing owed is trivially coverable.
  if (centavos == 0) return true;

  // The money path is centavos throughout -- config.h explains why that
  // resolution is kept -- but every coin this machine pays out is a whole peso.
  // An amount that is not a whole number of pesos cannot be paid AT ALL, so it
  // is a failure rather than something to round. Rounding here would create or
  // destroy money at the moment of payout, which rule 0 forbids.
  if (centavos % CENTAVOS_PER_PESO != 0) return false;
  const int32_t pesos = centavos / CENTAVOS_PER_PESO;

  // ---------------------------------------------------------------------
  // Largest-coin-first, with a hard P5 reserve.
  //
  // P5 is the scarce coin: it arrives slowly and leaves fast in a machine where
  // P15 change is a common outcome. P1 recirculates heavily and absorbs the
  // pressure, which is why the P1 hopper is not the one drained first.
  //
  // Below HOPPER_RESERVE_P5, P5 payouts stop entirely and change is made in P1,
  // so a run of large transactions cannot empty the P5 hopper and strand every
  // subsequent user who needs a P5 in their change.
  // ---------------------------------------------------------------------
  int32_t p5_spendable = (int32_t)p5_stock - HOPPER_RESERVE_P5;
  if (p5_spendable < 0) p5_spendable = 0;

  int32_t n5 = pesos / 5;
  if (n5 > p5_spendable) n5 = p5_spendable;

  const int32_t n1 = pesos - (n5 * 5);

  // There is no P1 reserve. P1 is the coin of last resort, and if it cannot
  // cover the remainder the plan fails outright. Failing here -- before a coin
  // has been accepted -- is what stops the machine promising change it does not
  // physically hold.
  if (n1 > (int32_t)p1_stock) return false;

  if (out) {
    out->p1 = (uint16_t)n1;
    out->p5 = (uint16_t)n5;
  }
  return true;
}
