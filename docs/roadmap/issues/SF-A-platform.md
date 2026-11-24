## Goal
One shared kernel layer for the Apple-specific building blocks every other roadmap ticket needs, with fixed interfaces, so feature work does not copy or fork them.

## Why this is first
Sound, Bluetooth, sleep, USB-C, display and GPU all need the same pieces. Several already work but live inside the Wi-Fi kexts, hard-wired to one device. Every research report for this roadmap listed them as "exists but trapped" or "missing". If each feature keeps its own copy, the copies diverge within a week.

## Decision
A new kext, `PDApplePlatform` (GPL-2.0, because the DART and PMGR parts derive from Linux), under `integrations/puredarwin/m1-platform/`, publishing IOKit services that clients find by matching, plus a small C header per service. Native IOKit, ported from m1n1 (MIT) and Omacom Linux where code already exists. No LinuxKPI dependency here: the LinuxKPI-based tickets (GPU, display) get thin adapters on top of these services, not the other way round.

## What exists today (proven on the M1 unless noted)
| Piece | Where | State |
|---|---|---|
| PMGR power domains | `m1-apcie-host/PDApcieT8103Link.cpp` (ported `pmgr_adt_power_enable`) | Works for the PCIe domains only; enable only |
| RTKit + SMC key read/write | `m1-apcie-host/PDAppleSMC.cpp` (898 lines) | Works; polled; one session per call; no notifications |
| DART (T8020 type) | `m1-apcie-host/dart/pd_dart_map.cpp` (649 lines) | Works for `dart-apcie0` stream 1 only |
| Tunables from the Apple device tree | `PDApcieT8103Link.cpp` | Works for PCIe lists |
| GPIO | ad hoc MMIO pokes (PERST#, CLKREQ#, keyboard) | No service, no pin interrupts |
| AIC interrupts | `PDAppleAIC` in the PureDarwin tree | Works; every source routed to CPU 0 |
| Apple I2C, NCO clocks, SPMI | none | Missing |

## Work items (each independently assignable once A1 fixes the interfaces)
| ID | Deliverable | Acceptance on the M1 | Size |
|---|---|---|---|
| A1 | Interface headers and service names for all pieces below, reviewed before code; move existing code behind them with no behaviour change | Wi-Fi join/DHCP/ping test still passes using the shared kext | 2-3 days |
| A2 | `PDApplePMGR`: enable **and disable** by device-tree node and by domain id, refcounted, parents handled; port the rest of Linux `drivers/pmdomain/apple/pmgr-pwrstate.c` (380 lines, GPL-2.0 OR MIT) | Domain off/on verified by register readback; PCIe still links | ~400 lines |
| A3 | `PDAppleRTKit`: persistent sessions, interrupt-driven mailbox (AIC), endpoint API, shared-memory buffer callbacks for coprocessors that ask the host to allocate; SMC client rebuilt on it | SMC stays up across 100 key reads; a second coprocessor (DCP or AOP) reaches "ready" read-only | ~1,200 lines |
| A4 | SMC notifications (lid `MSLD`, power button `bHLD`) and a key API for other kexts; reference Linux `drivers/mfd/macsmc.c` (525 lines) | Serial log shows lid open/close and button press | ~300 lines |
| A5 | `PDAppleDART`: one instance per DART node, per-stream domains, map/unmap API (generalise `pd_dart_map.cpp`); support locked DARTs read-only (`disp0`) | Wi-Fi runs on the shared DART; a second DART instance (`dart-sio` or a USB DART) maps and unmaps without fault | ~600 lines |
| A6 | `PDAppleGPIO`: pin direction/value/function and pin interrupts as an IOInterruptController; reference `pinctrl-apple-gpio.c` (619 lines) | Toggle a reset pin and observe the device; receive one pin interrupt | ~450 lines |
| A7 | `PDAppleI2C` as a `PDI2CController` (PureDarwin `PDI2CFamily`); reference `i2c-pasemi-core.c` (465 lines) and m1n1 `hw/i2c.py` | Read a known chip ID on i2c0 and i2c2 | ~400 lines |
| A8 | Reference-counted ownership of shared radio resources (SMC `gP0d`, PERST#, port) in the PCIe bridge | Wi-Fi off does not take the port down while another client holds it | small |

## Interfaces this ticket owns
Directory `integrations/puredarwin/m1-platform/`. Nobody else adds PMGR, RTKit, SMC, DART, GPIO or I2C code elsewhere; other tickets request changes here.

## Dependencies
None. Blocks: sound, Bluetooth (A8), sleep (A2, A4), USB-C (A2, A5, A6, A7), display (A3, A5), GPU (A2, A3).

## Risks
- RTKit under interrupts: only the polled path is proven.
- AIC routes everything to CPU 0; revisit with the SMP ticket.
- Locked DARTs left by iBoot limit what can be remapped.

## Evidence and references
`reports/sprint-010/pcie-kext-linkup-and-tree-patches-001.md`, `docs/roadmap/research/*.md`; m1n1 `src/pmgr.c`, `rtkit.c`, `asc.c`, `smc.c`, `dart.c`; Linux files named above.
