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

## 4. Four remedies

These are options with their costs. None is recommended here; the choice is yours.

**Only exact payment stops the drain, and nothing on this list makes customers pay exact.**
Each remedy either replaces what drains, slows it, or avoids the customers who cause it.

| Remedy | Slows or stops the drain? | What it costs |
|---|---|---|
| 1. Larger float, daily reload | Neither. It replaces what drains | A daily operator task |
| 2. ₱10 limit, refuse ₱20 coins | Stops it under mixes A and B, slows it under C, by turning away ₱20 payers | 20% to 50% of customers; 1,000 mL maximum |
| 3. Third hopper for ₱10 | Slows it. Does not stop it | A hopper, a gate, a wider panel, firmware |
| 4. Narrow the choice as change runs low | Prevents the lockout only if customers accept a larger size; otherwise no help | Customers are served worse before they are refused |

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

**This slows the drain. It does not stop it.** A ₱10 hopper still drains when ₱20 payers
outnumber ₱10 payers, and the machine then falls back on ₱1 and ₱5 coins. Under mix C it
moves the lockout from about 30 sales to about 105, not to never.

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

### Remedy 4 — Narrow the choice before refusing the customer

**The idea.** The drain is not caused by ₱20 coins. It is caused by small purchases made
with ₱20 coins. A customer who puts in ₱20 and takes 2,000 mL takes nothing out of the
hoppers. The same customer taking 500 mL takes ₱15 out.

So as the change stock falls, the machine could dim the smaller volumes for a customer
holding a large coin, and offer only the sizes that need little or no change. A customer
paying the exact amount would see no difference, because they need no change.

**It degrades choice before it degrades service.** A student who wanted 500 mL and is
offered only 1,500 mL and above has been served worse, even though they were served. That
is the trade, and it should be judged on those terms.

**What changes in the machine:** firmware and screens only. No new parts.

**The rule.** A volume stays available only if giving its change would still leave the
machine able to refund a full transaction to the next customer. Two things are always
available whatever the stock: the option that needs the least change, and "finish and get
my money back".

This rule needs no chosen threshold. Narrowing starts by itself at the last moment it can:
when one more large-change sale would lock the machine. We also tried starting earlier. It
narrowed far more customers and gave no better result.

**The customer's money is never trapped.** The check made before the first coin, that the
machine can refund the whole amount, stays exactly as it is. Narrowing applies only after
that check has passed, so a full refund is always possible. The two rules do not conflict.

**The result depends on one thing: whether the customer accepts a larger size.**

A customer who refuses and asks for their money back is the worst case for the hoppers.
Their ₱20 coin is already in the locked coin box, so the refund is ₱20 in small coins. That
is more than any sale would have cost.

Sales before the machine stops, starting from the mockup float:

| Share of narrowed customers who accept a larger size | Mix B | Mix C |
|---|---|---|
| No narrowing (today) | 56 | 30 |
| 100% accept | does not stop | does not stop |
| 70% accept, 30% take a refund | 60 | 33 |
| 30% accept, 70% take a refund | 57 | 30 |

If everyone accepts, the machine never locks. If even three in ten take a refund, the
benefit is almost gone.

**What "does not stop" looks like.** The machine does not recover. It stays at its lowest
working stock, and from then on most customers with a large coin are offered a narrowed
choice: about 6 in 10 under mix B and more than 8 in 10 under mix C. It keeps selling, in a
reduced way, until the operator reloads it.

**The condition that makes it work.** The customer must be told before the coin goes in.
A notice on the first screen, for example "Change is low. ₱20 buys 2,000 mL only", lets a
customer who does not want that walk away with their coin. A customer who walks away costs
the hoppers nothing. A customer who inserts the coin and then asks for it back costs ₱20.
The machine cannot refuse a ₱20 coin on its own while still accepting others; the acceptor
is either on or off.

**With the other remedies.**

- With remedy 1 (larger float): it works the same way and simply starts later. At a float
  of ₱800 almost no customer is narrowed until the float is nearly used up.
- With remedy 3 (₱10 hopper): it works better, because a refused ₱10 coin can be returned
  as the same coin. Under mix B with 70% acceptance, about 1,500 sales before lockout,
  against about 300 for remedy 3 alone. Under mix C the gain is small: about 115 against
  105.
- It works with any hopper arrangement, because it only asks the machine "can you still
  pay this?"

**Effort:** about two working days of firmware and screen work, including tests. The
volume screen must also show why an option is dimmed, since "you cannot afford this" and
"the machine cannot give change for this" are different messages.

## 5. What this means for the study

The hypothesis behind the design is that coins inserted by customers can supply the change
for other customers. The analysis says:

- **It holds only above a threshold share of exact payers.** For 500 mL sales paid with
  ₱20, that threshold is three exact payers for each ₱20 payer.
- **Below the threshold, the machine needs an outside supply of small coins**, and the
  size of that supply can be predicted from the payment mix.
- **Recirculating a third denomination moves the threshold** but does not remove it.
- **Steering customers toward purchases that need less change can prevent a lockout**, but
  only if they accept it, and only if they are told before they pay.

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
- For remedy 4, how many customers would accept a larger size is unknown. The 100%, 70%
  and 30% figures are illustrations, not estimates.
- Hopper capacity is taken as 500 coins each, a placeholder until the hoppers are bought.
- The results are from simulation. Nothing here has been measured on a machine.
