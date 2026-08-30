// Transaction simulator -- EMX-2026-WATERVENDO-01.
//
// ===========================================================================
// THIS LINKS THE REAL FIRMWARE CODE. It is not a mock-up of the behaviour.
// ===========================================================================
//
// billing.cpp, change_plan.cpp, fault_mask.cpp, calendar.cpp and
// eeprom_record.cpp are compiled straight out of src/ and called directly, so
// every peso and every millilitre printed below is computed by the same
// functions that will run on the Mega.
//
// What IS simulated, and is marked [SIM] wherever it appears:
//   - the coin acceptor, flow sensor, floats and hoppers (no hardware here)
//   - the hopper inventory, which lives in persist.cpp and needs EEPROM
//   - the state machine, which is Milestone 5 and does not exist yet
//
// Build and run (needs the same host toolchain as `pio test -e native` --
// see test/README.md for the setup and the 248-character path trap):
//
//   g++ -std=gnu++11 -Wall -Wextra -I include -o watervendo-sim
//       sim/simulate.cpp src/billing.cpp src/change_plan.cpp
//       src/fault_mask.cpp src/calendar.cpp
//   (one line; wrapped here for width)
//   ./watervendo-sim
//
// It takes no input and always runs the same scenarios, so the output is
// diffable: if a change to the money path alters any figure below, it shows up
// as a diff rather than as a surprise on a machine full of coins.

#include <stdio.h>
#include <string.h>

#include "billing.h"
#include "change_plan.h"
#include "fault_mask.h"
#include "calendar.h"

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

static void rule(char c) {
  for (int i = 0; i < 74; i++) putchar(c);
  putchar('\n');
}

static void scenario(int n, const char *title) {
  putchar('\n');
  rule('=');
  printf("  SCENARIO %d - %s\n", n, title);
  rule('=');
}

// Money is centavos everywhere. Formatted with integer division only, exactly
// as hmi_setAmount() will do it -- there is no float in this path either.
static const char *peso(money_t centavos) {
  static char buf[4][16];
  static int slot = 0;
  slot = (slot + 1) % 4;
  const char *sign = centavos < 0 ? "-" : "";
  const money_t a = centavos < 0 ? -centavos : centavos;
  snprintf(buf[slot], sizeof(buf[slot]), "%sP%ld.%02ld",
           sign, (long)(a / 100), (long)(a % 100));
  return buf[slot];
}

static void step(const char *what) {
  printf("  %-46s", what);
}

// Display text. faults_message() itself lives in faults.cpp behind PROGMEM and
// an Arduino include, so the strings are mirrored here for the simulation. The
// PRIORITY and PERSISTENCE decisions below are the real ones from fault_mask.
static const char *fault_text(fault_t f) {
  switch (f) {
    case FAULT_OUT_OF_WATER: return "OUT OF WATER - PLEASE REFILL";
    case FAULT_LOW_CHANGE:   return "LOW CHANGE - SERVICE REQUIRED";
    case FAULT_STORAGE_FULL: return "COIN STORAGE FULL";
    case FAULT_CHANGE_JAM:   return "CHANGE JAM - SERVICE REQUIRED";
    case FAULT_FLOW_STALL:   return "SERVICE REQUIRED (flow stall)";
    case FAULT_PUMP_RUNTIME: return "SERVICE REQUIRED (pump)";
    case FAULT_ACCEPTOR:     return "SERVICE REQUIRED (acceptor)";
    default:                 return "(none)";
  }
}

static void show_credit() {
  printf("credit %-9s inserted %s\n",
         peso(billing_credit()), peso(billing_inserted()));
}

// ---------------------------------------------------------------------------
// [SIM] Hopper inventory -- persist.cpp needs EEPROM, so it is mirrored here.
// ---------------------------------------------------------------------------

struct hoppers_t {
  uint16_t p1;
  uint16_t p5;
  uint16_t profit_p10;
  uint16_t profit_p20;
  uint16_t profit_unknown;
};

static hoppers_t HOP;

static void hoppers_reset(uint16_t p1, uint16_t p5) {
  memset(&HOP, 0, sizeof(HOP));
  HOP.p1 = p1;
  HOP.p5 = p5;
}

static void hoppers_show(const char *label) {
  const money_t change_value = (money_t)HOP.p1 * 100 + (money_t)HOP.p5 * 500;
  printf("  %-20s P1 x%-4u  P5 x%-4u  = %-9s"
         " | chamber P10 x%u P20 x%u ?x%u\n",
         label, HOP.p1, HOP.p5, peso(change_value),
         HOP.profit_p10, HOP.profit_p20, HOP.profit_unknown);
}

// [SIM] The diverter. Routing is the real rule from coin_diverter.cpp:
// P1 and P5 to their hoppers, everything else to the locked chamber.
static void route_coin(coin_t c) {
  switch (c) {
    case COIN_P1:  HOP.p1++; break;
    case COIN_P5:  HOP.p5++; break;
    case COIN_P10: HOP.profit_p10++; break;
    case COIN_P20: HOP.profit_p20++; break;
    default:       HOP.profit_unknown++; break;   // COIN_UNKNOWN
  }
}

static const char *coin_name(coin_t c) {
  switch (c) {
    case COIN_P1:  return "P1";
    case COIN_P5:  return "P5";
    case COIN_P10: return "P10";
    case COIN_P20: return "P20";
    case COIN_UNKNOWN: return "unrecognised";
    default: return "none";
  }
}

// Insert a coin: credit it (REAL billing) and route it (SIM diverter).
static void insert(coin_t c) {
  char line[64];
  snprintf(line, sizeof(line), "insert %s", coin_name(c));
  step(line);
  billing_add_coin(c);
  route_coin(c);
  show_credit();
}

// ---------------------------------------------------------------------------
// [SIM] Flow sensor. Mirrors the remainder-carry arithmetic in flow.cpp so the
// truncation behaviour is visible, and deliberately runs SLIGHTLY FAST to show
// that sensor error never reaches the money.
// ---------------------------------------------------------------------------

static volume_t flow_deliver(volume_t want_ml, int error_percent) {
  const uint32_t pulses =
      (uint32_t)(((int32_t)want_ml * (100 + error_percent) / 100)
                 * ML_PER_PULSE_DEN / ML_PER_PULSE_NUM);

  volume_t ml = 0;
  uint32_t carry = 0;
  for (uint32_t i = 0; i < pulses; i++) {
    const uint32_t scaled = (uint32_t)ML_PER_PULSE_NUM + carry;
    ml += (volume_t)(scaled / (uint32_t)ML_PER_PULSE_DEN);
    carry = scaled % (uint32_t)ML_PER_PULSE_DEN;
  }
  return ml;
}

// ---------------------------------------------------------------------------
// Change payout -- REAL change_plan(), [SIM] hopper motors
// ---------------------------------------------------------------------------

static bool pay_change(money_t due) {
  change_plan_t plan;
  const bool ok = change_plan(due, HOP.p1, HOP.p5, &plan);

  char line[64];
  snprintf(line, sizeof(line), "plan change of %s", peso(due));
  step(line);

  if (!ok) {
    printf("CANNOT COVER -> refuse\n");
    return false;
  }
  printf("P5 x%u + P1 x%u\n", plan.p5, plan.p1);

  HOP.p1 -= plan.p1;
  HOP.p5 -= plan.p5;
  step("hoppers count the coins out  [SIM]");
  printf("paid %s\n", peso((money_t)plan.p1 * 100 + (money_t)plan.p5 * 500));
  return true;
}

// Conservation check: what the user was charged plus what they got back must
// equal what they put in. If this ever fails, the difference is coming out of
// the hoppers on every transaction.
static void audit(money_t inserted, money_t change) {
  const money_t charged = billing_price_of(billing_total_dispensed());
  rule('-');
  printf("  AUDIT  inserted %s = charged %s + change %s   %s\n",
         peso(inserted), peso(charged), peso(change),
         (charged + change == inserted) ? "[BALANCED]" : "[** MISMATCH **]");
}

// ===========================================================================

static void scenario_1_normal() {
  scenario(1, "Normal transaction - the mockup's P20 example");
  billing_reset();
  hoppers_reset(115, 34);
  hoppers_show("hoppers before");
  putchar('\n');

  insert(COIN_P20);

  step("acceptor inhibited at ceiling?");
  printf("%s\n", billing_at_ceiling() ? "YES - stop taking money" : "no");

  step("largest volume this credit can buy");
  printf("%ld mL\n", (long)billing_max_selectable_ml());

  step("user selects 500 mL");
  billing_select(500);
  printf("price %s deducted BEFORE the valve opens\n", peso(billing_price_of(500)));
  step("");
  show_credit();

  const volume_t got = flow_deliver(500, 3);   // sensor reads 3% fast
  char line[64];
  snprintf(line, sizeof(line), "pour 500 mL, sensor reads %ld mL  [SIM]", (long)got);
  step(line);
  printf("valve shuts at target\n");

  billing_settle_complete(got);
  step("settle: target reached, no refund arises");
  printf("dispensed total %ld mL\n", (long)billing_total_dispensed());

  putchar('\n');
  const money_t due = billing_change_due();
  pay_change(due);
  putchar('\n');
  hoppers_show("hoppers after");
  audit(billing_inserted(), due);
  printf("\n  Note: the sensor over-read by %ld mL and the user was charged\n"
         "  for 500 mL regardless. Coins set the volume; the sensor only\n"
         "  closed the valve.\n", (long)(got - 500));
}

static void scenario_2_partial() {
  scenario(2, "Bottle removed mid-pour - the documented 305 mL case");
  billing_reset();
  hoppers_reset(115, 34);

  insert(COIN_P20);
  step("user selects 1000 mL");
  billing_select(1000);
  printf("price %s deducted, credit now %s\n",
         peso(billing_price_of(1000)), peso(billing_credit()));

  step("bottle removed at 305 mL, grace expires  [SIM]");
  printf("valve shut\n");

  step("round DOWN to the nearest 100 mL");
  printf("305 mL -> %ld mL\n", (long)billing_round_down(305));

  billing_settle_partial(305);
  step("settle partial: charge the ROUNDED figure");
  printf("charged %s, refunded %s to credit\n",
         peso(billing_price_of(300)), peso(billing_price_of(700)));
  step("");
  show_credit();

  putchar('\n');
  const money_t due = billing_change_due();
  pay_change(due);
  audit(billing_inserted(), due);
  printf("\n  Rounding always favours the machine. 305 mL became 300 mL,\n"
         "  never 400 mL and never 'nearest'. The 5 mL is the margin against\n"
         "  the sensor's 2-5%% tolerance, and it must always fall this way.\n");
}

static void scenario_3_reserve() {
  scenario(3, "The P5 reserve - why the P1 hopper is not drained first");
  printf("\n  HOPPER_RESERVE_P5 = %d. Below it, P5 payouts stop entirely.\n\n",
         HOPPER_RESERVE_P5);

  const struct { uint16_t p1, p5; const char *note; } cases[] = {
    { 115, 34, "healthy stock" },
    { 115, 13, "getting low" },
    { 115, 11, "one above the reserve" },
    { 115, 10, "exactly at the reserve" },
    { 115,  4, "below the reserve" },
    {   2, 10, "P1 nearly out AND P5 at the reserve" },
  };

  printf("  %-24s %-14s %s\n", "stock", "change P15", "result");
  rule('-');
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    change_plan_t p;
    const bool ok = change_plan(1500, cases[i].p1, cases[i].p5, &p);
    char stock[32], out[32];
    snprintf(stock, sizeof(stock), "P1 x%u  P5 x%u", cases[i].p1, cases[i].p5);
    if (ok) snprintf(out, sizeof(out), "P5 x%u + P1 x%u", p.p5, p.p1);
    else    snprintf(out, sizeof(out), "CANNOT COVER");
    printf("  %-24s %-14s %s\n", stock, out, cases[i].note);
  }
  printf("\n  P5 is the scarce coin: it arrives slowly and leaves fast when P15\n"
         "  change is a common outcome. P1 recirculates and absorbs the\n"
         "  pressure. The last row refuses rather than paying what it can.\n");
}

static void scenario_4_guard() {
  scenario(4, "The guard that runs BEFORE the first coin");
  billing_reset();

  printf("\n  Worst case the machine could owe: %s\n",
         peso(billing_worst_case_change()));
  printf("  (SPEC 2.2 reaches PAYING_CHANGE with full credit and no pour,\n"
         "   so the whole ceiling is refundable -- not the ceiling less one step.)\n\n");

  const struct { uint16_t p1, p5; } stock[] = { {115, 34}, {60, 12}, {10, 2} };
  printf("  %-22s %s\n", "stock", "may the machine accept a coin?");
  rule('-');
  for (unsigned i = 0; i < sizeof(stock) / sizeof(stock[0]); i++) {
    const bool ok = change_plan(billing_worst_case_change(),
                                stock[i].p1, stock[i].p5, nullptr);
    char s[32];
    snprintf(s, sizeof(s), "P1 x%u  P5 x%u", stock[i].p1, stock[i].p5);
    printf("  %-22s %s\n", s,
           ok ? "yes" : "NO -> LOW CHANGE, acceptor inhibited");
  }
  printf("\n  Checked once, at the gate, before the machine has taken anything.\n"
         "  Never re-checked mid-transaction: a user who has already paid must\n"
         "  never hit a lockout that strands their money.\n");
}

static void scenario_5_stall() {
  scenario(5, "Flow stall - settle first, lock second (invariant 8)");
  billing_reset();
  hoppers_reset(115, 34);

  insert(COIN_P20);
  step("user selects 2000 mL");
  billing_select(2000);
  printf("price %s deducted\n", peso(billing_price_of(2000)));

  step("pour reaches 640 mL then the line blocks  [SIM]");
  printf("no pulses for %d ms\n", FLOW_STALL_TIMEOUT_MS);

  uint8_t mask = 0;
  step("fault LATCHED, machine NOT locked yet");
  printf("user keeps their screen\n");

  billing_settle_partial(640);
  step("settle on the rounded volume");
  printf("charged %s for %ld mL\n",
         peso(billing_price_of(600)), (long)billing_round_down(640));

  const money_t due = billing_change_due();
  putchar('\n');
  pay_change(due);
  putchar('\n');

  mask |= fault_bit(FAULT_FLOW_STALL);
  step("change is out -> NOW release the latch");
  printf("locked: %s\n", fault_text(fault_highest(mask)));

  audit(billing_inserted(), due);
  printf("\n  Raising the fault immediately would have inhibited the acceptor\n"
         "  and locked the machine with %s of the user's money still inside it.\n",
         peso(due));
}

static void scenario_6_faults() {
  scenario(6, "Fault priority - a jam must never be hidden");
  printf("\n  Several faults can be active at once. SPEC 6.2 orders them so the\n"
         "  most blocking one is what the technician actually sees.\n\n");

  uint8_t mask = 0;
  const fault_t seq[] = { FAULT_STORAGE_FULL, FAULT_LOW_CHANGE,
                          FAULT_OUT_OF_WATER, FAULT_CHANGE_JAM };
  printf("  %-34s %s\n", "raised so far", "displayed");
  rule('-');
  for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
    mask |= fault_bit(seq[i]);
    printf("  + %-32s %s\n", fault_text(seq[i]),
           fault_text(fault_highest(mask)));
  }
  printf("\n  Last-write-wins would have left COIN STORAGE FULL on the screen\n"
         "  while the hopper was jammed. The technician reloads coins, walks\n"
         "  away, and the machine is still broken.\n\n");

  printf("  Surviving a power cut (only these are written to EEPROM):\n");
  const fault_t all[] = { FAULT_CHANGE_JAM, FAULT_FLOW_STALL, FAULT_PUMP_RUNTIME,
                          FAULT_ACCEPTOR, FAULT_OUT_OF_WATER, FAULT_LOW_CHANGE,
                          FAULT_STORAGE_FULL };
  for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
    printf("    %-32s %s\n", fault_text(all[i]),
           fault_persistent(all[i]) ? "persists" : "re-evaluated on boot");
  }
}

static void scenario_7_daybreak() {
  scenario(7, "Midnight rollover and a failed clock");

  datetime_t before = { 2026, 8, 31, 23, 59, 30 };
  datetime_t after  = { 2026, 9,  1,  0,  0, 10 };
  printf("\n  %-30s day #%ld\n", "2026-08-31 23:59:30",
         (long)calendar_day_number(&before));
  printf("  %-30s day #%ld  -> BOUNDARY CROSSED\n", "2026-09-01 00:00:10",
         (long)calendar_day_number(&after));
  printf("\n  On the boundary the closing totals are written to the history\n"
         "  ring BEFORE they are zeroed. Zero-then-write would lose the whole\n"
         "  day on any power cut in between -- the day the owner was reading.\n\n");

  const struct { datetime_t dt; const char *why; } bad[] = {
    { { 2000, 1, 1, 0, 0, 0 },   "dead battery, year before the firmware" },
    { { 2026, 2, 30, 12, 0, 0 }, "30 February" },
    { { 2026, 8, 31, 24, 0, 0 }, "hour 24, the classic BCD misread" },
  };
  printf("  Readings the machine REFUSES to trust:\n");
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    printf("    %04u-%02u-%02u %02u:%02u  %-38s %s\n",
           bad[i].dt.year, bad[i].dt.month, bad[i].dt.day,
           bad[i].dt.hour, bad[i].dt.minute, bad[i].why,
           calendar_plausible(&bad[i].dt) ? "accepted" : "REJECTED");
  }
  printf("\n  A failed clock shows a clock-not-set state and keeps selling\n"
         "  water. It never stamps a receipt with a date it guessed.\n");
}

static void scenario_8_unknown_coin() {
  scenario(8, "An unrecognised coin still belongs to the user");
  billing_reset();
  hoppers_reset(115, 34);

  step("6-pulse train - inside COIN_PULSE_MAX, matches nothing");
  printf("decoded as %s\n", coin_name(COIN_UNKNOWN));
  insert(COIN_UNKNOWN);

  printf("\n");
  hoppers_show("hoppers after");
  printf("\n  Credited at the MINIMUM denomination (%s) and routed to the locked\n"
         "  chamber, counted in its own 'unknown' column.\n\n"
         "  Discarding it -- which is what the code did before the spec\n"
         "  reconciliation -- took the user's coin and gave them nothing.\n"
         "  Counting it as P10 or P20 would corrupt the chamber's peso value.\n",
         peso(coin_value(COIN_UNKNOWN)));
}

int main(void) {
  rule('#');
  printf("  EMX-2026-WATERVENDO-01  -  TRANSACTION SIMULATION\n");
  printf("  Real billing / change / fault / calendar code, linked from src/.\n");
  printf("  Hardware and the Milestone 5 state machine are [SIM].\n");
  rule('#');

  billing_begin();

  scenario_1_normal();
  scenario_2_partial();
  scenario_3_reserve();
  scenario_4_guard();
  scenario_5_stall();
  scenario_6_faults();
  scenario_7_daybreak();
  scenario_8_unknown_coin();

  putchar('\n');
  rule('#');
  printf("  END OF SIMULATION\n");
  rule('#');
  putchar('\n');
  return 0;
}
