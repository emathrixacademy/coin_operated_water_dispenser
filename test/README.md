# test/

Host-side unit tests. `pio test -e native`.

**Status: 40/40 passing** as of 2026-08-30 (27 `test_billing`, 13 `test_eeprom`).

## Host toolchain setup — read this before you spend an afternoon on it

The `native` environment needs a host C++ compiler. PlatformIO does not ship one;
`toolchain-atmelavr` builds for the Mega and cannot build the tests.

On Windows:

```
winget install --id BrechtSanders.WinLibs.POSIX.UCRT.LLVM -e
```

**Then copy it somewhere with a short path.** This is not optional and it is not
tidiness.

WinGet installs to
`%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT.LLVM_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\`,
which is 140 characters before the toolchain's own directory tree starts. GCC's
internal include path is an unexpanded `bin/../lib/gcc/.../../../../include/...`
string that adds another 91, so a header like
`include/c++/14.2.0/x86_64-w64-mingw32/bits/cpu_defines.h` resolves to **249
characters** and fails to open, while `bits/c++config.h` in the *same directory*
resolves to 247 and opens fine. The legacy Windows path limit for this API is
248.

The failure looks nothing like a path problem:

```
bits/c++config.h:683:10: fatal error: bits/cpu_defines.h: No such file or directory
```

The file is present and readable; `-I` pointing straight at it does not help, and
a directory junction does not help either because GCC canonicalises back to its
real install path. Copying the tree does:

```
robocopy "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT.LLVM_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64" C:\mg /E /MT:16
set PATH=C:\mg\bin;%PATH%
pio test -e native
```

Any short root works; `C:\mg` is what this machine uses. Nothing in the repo
depends on the location — keep it on `PATH`, not in `platformio.ini`, so the
build stays portable.

| Suite | Covers |
|---|---|
| `test_billing/` | Credit accumulation, the ₱20 ceiling, volume selection, refund rounding, settlement |
| `test_eeprom/` | Record framing, CRC, and the first-boot virgin-cell path |

## Why these run off-target

`billing.cpp` and `eeprom_record.cpp` carry **no Arduino dependency**, deliberately. That
is what makes the money arithmetic and the EEPROM validation testable on the development
machine instead of only being observable on hardware.

If you add an Arduino call to either file, these tests stop building and the money math
stops being checked. Don't.

`platformio.ini` builds only those two translation units in the `native` environment.

## What is worth knowing about the coverage

**Refund rounding** is tested at its boundaries (299 / 300 / 301 / 399 / 400) and
explicitly against rounding-to-nearest: 399 mL must settle as 300 mL, not 400, even
though 400 is closer. Rounding always favours the machine.

**The conservation invariant** — `test_no_money_is_created_or_destroyed` — asserts that
across a mixed sequence of partial and complete pours, what the user was charged plus
what they get back equals what they put in. If that ever fails, the difference is coming
out of the hoppers on every transaction.

**The virgin-EEPROM path** is the reason `test_eeprom` exists. A factory-fresh AVR cell
reads `0xFF`, so an unvalidated inventory read gives 65535 coins in each hopper and a
machine that believes it can make change it does not have. The tests assert that an
all-`0xFF` region is rejected *and that the destination struct is left untouched* — a
rejected record must not leave garbage in the mirror.

`test_every_single_bit_flip_in_payload_is_caught` walks every bit of the inventory
payload and confirms the CRC rejects each corruption, rather than trusting the property
from the polynomial's reputation.

## Known coverage gaps

Two pieces of money logic are **not** covered here because they carry Arduino
dependencies and cannot link into the `native` environment:

- **`coin_hopper_plan()`** (SPEC 3.4, largest-coin-first with the ₱5 reserve).
  The arithmetic is pure, but `plan()` reads stock through
  `coin_hopper_count()` → `persist_inventory()` → `EEPROM`. Testing it off-target
  means extracting the arithmetic into a free function taking the two stock
  counts as arguments. Worth doing — it decides what change a user is handed.
- **The fault priority mask** (SPEC 6.2) in `faults.cpp`, which depends on
  `coin_acceptor` and `persist`.

Tracked as R-11 and 7-2 in `docs/remaining.md`.

## What does not belong here

The coin path. `docs/scenarios.md` is explicit: simulated pulses do not count as a pass
for the coin path, which needs real coins of both series, including wet ones.

**A green run here is not a release gate.** The scenario cases are. These tests cover the
arithmetic and framing that sit behind the coin path, not the coin path itself.
