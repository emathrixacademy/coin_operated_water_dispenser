# test/

Host-side unit tests, run with `pio test -e native`.

Empty at Milestone 1. Tests for the billing arithmetic and the refund rounding land in
Milestone 3 alongside the coin path.

## What belongs here

Integer arithmetic that can be checked without hardware:

- Coin-to-credit accumulation and the ₱20 ceiling
- Credit-to-volume target computation at ₱1 = 100 mL
- Partial refund rounding — **down** to the nearest 100 mL, never to nearest, never up.
  A measured 305 mL is charged as 300 mL. The boundary cases are the ones worth
  testing: 299, 300, 301, 399, 400.
- Change denomination selection, including the rule that the ₱1 hopper covers ₱5
  dissipation so it is not drained first
- EEPROM record checksum and magic number validation

## What does not belong here

The coin path itself. `docs/scenarios.md` is explicit: simulated pulses do not count as
a passing test for the coin path. These tests cover the arithmetic that sits behind the
coin path, not the coin path. A green run here is not a release gate — the scenario
cases are.
