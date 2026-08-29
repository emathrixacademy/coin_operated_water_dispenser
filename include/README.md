# include/

Headers and constants.

| File | Contents |
|---|---|
| `config.h` | Pin map, timing constants, calibration constants |
| `types.h` | Shared enums and value types — coins, faults, states, records |
| *module headers* | One per module in [src/](../src/), carrying the contract |

## config.h

The only place a number with meaning is allowed to live. If a magic number appears in a
logic file, it belongs here instead.

The pin map mirrors [docs/wiring.md](../docs/wiring.md). The two must never disagree — if
a pin moves, it moves in both places in the same commit.

**Every timing constant carries a comment saying what breaks if it changes.** That is not
decoration; several were chosen against physical failure modes that are invisible in the
code. `COIN_LOCKOUT_MS` is the model to follow — shortening it to make the machine feel
faster jams the chute.

## Units

Money is integer **centavos**, volume is integer **millilitres**, via `money_t` and
`volume_t`. No floating point in the money or volume path — a float in the change
computation is how a machine loses a coin per transaction.

`ML_PER_PULSE` is expressed as an exact integer ratio (`_NUM` / `_DEN`) rather than a
decimal, because the true figure does not divide cleanly and there is no float available
to hold it. `flow.cpp` carries the division remainder between calls so truncation error
stays under a millilitre across a whole pour instead of accumulating once per pulse.

`FLOW_PULSES_PER_LITRE` is a **placeholder**. Measure it per unit against a graduated
cylinder before the machine handles money — see
[docs/calibration.md](../docs/calibration.md).
