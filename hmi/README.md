# hmi/

Nextion Editor project. Empty at Milestone 1 — the HMI is built in Milestone 6.

`watervendo.HMI` is the tracked source. The compiled `.tft` is a build artifact and is
gitignored; regenerate it from the Editor and flash over microSD or serial.

**Firmware and HMI versions must match.** Adding a screen means changing both this
project and the firmware, in the same commit. A Nextion page that the firmware does not
know about is a screen the machine can never reach; a page the firmware sends to that
does not exist leaves the display on whatever it was showing, which during a
transaction means a user looking at a stale balance.

## Planned pages

Layout follows the client's mockup in `WATER-VENDO.pdf`. Where the mockup and
`CLAUDE.md` disagree on layout, the mockup wins; on billing behaviour, `CLAUDE.md` wins.

| Page | Content |
|---|---|
| Standby | Welcome, prompt to insert coin to start |
| Select Volume | Grid of volume options with peso equivalents, greyed above available credit, inserted amount shown |
| Insert Bottle | Prompt with selected volume and balance, bottle auto-detected |
| Dispensing | Selected volume, dispensed, target, balance, progress bar with percentage |
| Waiting | Bottle removed mid-pour, with the grace countdown |
| Thank You | Volume dispensed, amount inserted, change due, date and time, prompt to take bottle and change |
| Statistics | Water consumed today, total profit today, date |
| System Status | Water level, temperature, cooling status |
| Coin Inventory | ₱1 and ₱5 pieces available, total change value |
| Admin | Change loading, inventory correction, recent transaction history |

Bottom navigation bar of Home, Statistics, Status, Inventory across the operator-facing
pages, matching the mockup.

## Constraints

The Mega never renders. It sends short commands over `Serial2` and reads touch events.
Every command terminates with three `0xFF` bytes.

Transaction history is capped at a **twenty-entry ring buffer in EEPROM** holding
timestamp, amount in, volume out and change out. The Mega has 4 KB of EEPROM and
`CLAUDE.md` rules out an SD logger. A longer history is a new scope item to be quoted,
not built.

The Admin change-inventory edit needs an explicit confirm step and commits to EEPROM
immediately on accept. It is the one write path that can desync the stored inventory
from the physical hopper contents.
