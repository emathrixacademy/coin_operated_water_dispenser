# Change economics of the coin-recirculating water vendo

Project EMX-2026-WATERVENDO-01 · prepared for the research team · 4 October 2026

## Summary

The machine gives change from two hoppers, ₱1 and ₱5, and refills those hoppers only from
the ₱1 and ₱5 coins that customers insert. Every ₱10 and ₱20 coin goes to the locked coin
box and never comes back out.

That means the hoppers gain coins from customers who pay the exact amount and lose coins to
customers who pay with a ₱10 or ₱20. Unless most customers pay exact, the hoppers empty.

**With the starting float in your mockup (115 × ₱1 and 34 × ₱5, worth ₱285), the machine
is expected to stop accepting coins after about 30 to 60 sales** under two of the three
payment mixes we tried. At 100 sales a day, that is before the end of the first day.

This is a property of recirculating only two denominations. No change to the firmware can
fix it. It is also a measurable result about coin recirculation, which is the stated novelty
of this study.

**Every figure below depends on how customers pay, and we do not know that yet.** The
payment mixes are assumptions, not data. Section 6 says how to replace them with
measurements.

## 1. The mechanism

| Coin inserted | Where it goes | Can it be given back as change? |
|---|---|---|
| ₱1 | ₱1 hopper | Yes |
| ₱5 | ₱5 hopper | Yes |
| ₱10 | Locked coin box | No |
| ₱20 | Locked coin box | No |

Take one sale of 500 mL, which costs ₱5:

| How the customer pays | Effect on the hoppers |
|---|---|
| One ₱5 coin (exact) | Gain ₱5 |
| One ₱10 coin | Lose ₱5 |
| One ₱20 coin | Lose ₱15 |

The customer who pays with ₱20 takes three ₱5 coins out of the hopper and puts nothing
back, because their ₱20 went to the coin box.

## 2. The break-even ratio

To hold the hoppers level, the coins gained from exact payers must equal the coins lost to
change.

**Exact payers needed for each large-coin payer = change given ÷ price of the sale**

| Sale | Paid with | Change given | Exact payers needed to replace it |
|---|---|---|---|
| 500 mL, ₱5 | ₱10 | ₱5 | 1 |
| 500 mL, ₱5 | ₱20 | ₱15 | 3 |
| 300 mL, ₱3 | ₱20 | ₱17 | about 6 |
| 1,000 mL, ₱10 | ₱20 | ₱10 | 1 |

So for the common case, a ₱20 coin for a 500 mL bottle, **three customers must pay exact
for every one who pays with ₱20**, just to stay level.

Across the sales in our simulation (₱3 to ₱10 each, ₱6.50 on average), the averages are:

- An exact payer adds ₱6.50 to the hoppers.
- A ₱10 payer removes ₱3.50.
- A ₱20 payer removes ₱13.50.

## 3. How long the float lasts

We simulated the machine's own change rules: largest coin first, with ten ₱5 coins held in
reserve. The machine stops accepting coins when it can no longer guarantee change for the
largest possible refund. Each figure is the middle result (the median) of 2,000 simulated
runs with no operator visit.

**Assumptions, not data:** each sale is between ₱3 and ₱10; each customer pays either the
exact amount in ₱1 and ₱5 coins, or with one ₱10, or with one ₱20.

| Payment mix (exact / ₱10 / ₱20) | Hoppers gain or lose per sale |
|---|---|
| A. Mostly exact: 60% / 20% / 20% | gain ₱0.50 |
| B. Even: 34% / 33% / 33% | lose ₱3.40 |
| C. Mostly large coins: 20% / 30% / 50% | lose ₱6.50 |

**Sales before the machine stops accepting coins:**

| Starting float | Value | Mix A | Mix B | Mix C |
|---|---|---|---|---|
| 115 × ₱1, 34 × ₱5 (your mockup) | ₱285 | does not stop | 57 | 30 |
| 200 × ₱1, 60 × ₱5 | ₱500 | does not stop | 119 | 63 |
| 300 × ₱1, 100 × ₱5 | ₱800 | does not stop | 208 | 109 |
| 500 × ₱1, 200 × ₱5 | ₱1,500 | does not stop | 415 | 218 |

"Does not stop" means no lockout within 3,000 simulated sales.

A quick estimate that matches the table: **sales before lockout ≈ float value ÷ loss per
sale, less a margin.** The machine stops before the hoppers are empty, because it must keep
enough to refund a full transaction.

A larger float buys time in direct proportion. It does not change the direction. Under
mixes B and C the hoppers always run down; under mix A they never do.

## 4. Three remedies

These are options with their costs. None is recommended here; the choice is yours.

### Remedy 1 — Larger float and a daily reload

**What changes in the machine:** nothing.

**Effect:** the machine runs until the float is used up, then waits for the operator.

**What the operator does every day:**

1. Open the coin box and remove the ₱10 and ₱20 coins.
2. Take them away and exchange part of them for ₱1 and ₱5 coins.
3. Bring the ₱1 and ₱5 coins back and load the hoppers.
4. Enter the new counts on the Admin screen.

**How much must be exchanged**, at 100 sales a day:

| Mix | ₱1 and ₱5 coins to bring back each day |
|---|---|
| A | none |
| B | about ₱340 |
| C | about ₱650 |

This daily exchange is built into the current design. It is the real running cost of the
machine and it should be stated in the paper.

**Effort:** none for the build. A daily task for the operator.

### Remedy 2 — Lower the limit from ₱20 to ₱10 per transaction

**What changes in the machine:** one firmware constant, and the coin acceptor is set to
refuse ₱20 coins. The second part is necessary: the machine must credit every coin it
accepts, so the only way to keep ₱20 coins out is for the acceptor not to take them.

**Effect:**

- The largest purchase becomes 1,000 mL.
- A customer who has only a ₱20 coin cannot buy.
- The largest change on a ₱3 sale falls from ₱17 to ₱7.

| Mix | Customers turned away | Sales before lockout, mockup float |
|---|---|---|
| A | 20% | does not stop |
| B | 33% | does not stop |
| C | 50% | about 740 |

The hoppers now hold level or nearly so, but at the cost of the turned-away customers.

**Effort:** about half a day of firmware work including its tests, plus re-teaching the
acceptor. The volume
screen would show 10 options, not 20.

### Remedy 3 — A third hopper for ₱10 coins

**What changes in the machine:** ₱10 coins are kept in their own hopper and given back as
change, so they recirculate. ₱20 coins still go to the coin box.

**Effect**, starting from the mockup float plus 20 × ₱10:

| Mix | Today | With a ₱10 hopper |
|---|---|---|
| A | does not stop | does not stop |
| B | 57 sales | about 300 sales |
| C | 30 sales | about 105 sales |

This is a large improvement, but it does not fully close under mix C. The ₱10 hopper
itself drains whenever more customers pay with ₱20 than with ₱10, and the machine then
falls back on ₱1 and ₱5 coins.

**What it would take:**

| Area | Change |
|---|---|
| Parts | One more coin hopper, one outlet sensor, one more gate with its servo, one relay channel |
| Fabrication | A third gate and a fourth chute; a wider back panel; a new revision of the fabrication drawing |
| Wiring | Three more controller pins (servo, motor, sensor). The controller has spare pins |
| Firmware | Change planning for three denominations; a third hopper in the payout sequence; a new stored-inventory layout; Admin and Inventory screens; new tests. About three working days |
| Screen | Inventory and Admin pages gain a ₱10 row |

The cost of the hopper is not in the project records and would need a quotation. Adding a
part is a change of scope under the service agreement.

## 5. What this means for the study

The hypothesis behind the design is that coins inserted by customers can supply the change
for other customers. The analysis says:

- **It holds only above a threshold share of exact payers.** For 500 mL sales paid with
  ₱20, that threshold is three exact payers for each ₱20 payer.
- **Below the threshold, the machine needs an outside supply of small coins**, and the
  size of that supply can be predicted from the payment mix.
- **Recirculating a third denomination moves the threshold** but does not remove it.

That is a quantified result, and it can be tested on the finished machine.

## 6. What to measure

The payment mix is the one input everything depends on, and it is currently a guess.

1. **Before the build is finished:** observe or survey how students pay at an existing
   vending point. Record, for each purchase, the price and the coins used.
2. **On the finished machine:** the machine already records, for every sale, the amount
   inserted, the volume, and the change given, and it counts ₱10 and ₱20 coins
   separately. From the Admin history and the coin counts you can compute the real mix and
   the real loss per sale.
3. **Compare** the measured sales-before-lockout against the estimate in Section 3.

## 7. Limits of this analysis

- The payment mixes are assumed. Real customers also pay in ways not modelled here, for
  example two ₱5 coins for a ₱7 sale.
- Sale sizes are assumed to be spread evenly from ₱3 to ₱10.
- The simulation assumes a transaction limit that allows up to ₱39 of credit, following
  the ruling that every accepted coin is credited.
- Hopper capacity is taken as 500 coins each, a placeholder until the hoppers are bought.
- The results are from simulation. Nothing here has been measured on a machine.
