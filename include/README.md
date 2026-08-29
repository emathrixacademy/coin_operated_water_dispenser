# include/

Headers. Empty at Milestone 1 by design.

`config.h` lands in Milestone 2 and carries the full pin map, every timing constant and
every calibration constant. No magic numbers anywhere else in the codebase — if a
number has meaning, it is named here.

Every timing constant carries a comment saying what breaks if it changes. `COIN_LOCKOUT_MS`
is the example to follow: shortening it to make the machine feel faster jams the chute.

The pin map in `config.h` mirrors `docs/wiring.md`. The two must never disagree; if a
pin moves, it moves in both places in the same commit.
