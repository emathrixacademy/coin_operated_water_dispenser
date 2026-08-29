# src/

Firmware implementation files. Empty at Milestone 1 by design — module skeletons and
header contracts land in Milestone 2, after the pin map and scenario cases are signed
off.

Planned contents, per `CLAUDE.md`:

```
main.cpp            state machine, top-level transitions only
coin_acceptor.cpp   pulse capture, coin value resolution
coin_diverter.cpp   servo routing, per-coin lockout window
coin_hopper.cpp     change payout, outlet counting, jam retry
flow.cpp            flow pulse capture, volume accumulation
bottle.cpp          proximity sensor debounce, present/absent
water_level.cpp     three floats, pump control, lockout states
dispense.cpp        valve control, cutoff, partial-stop handling
billing.cpp         coin-to-volume allocation, refund computation
persist.cpp         EEPROM read/write of coin inventory and open transaction
hmi.cpp             Nextion serial commands, screen state
faults.cpp          lockout conditions and alert routing
```

Every module exposes `begin()`, `update()`, and query functions. Nothing owns the loop.
