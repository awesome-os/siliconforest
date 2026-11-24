## Goal
Close the lid and have the machine sleep; open it and be back online in seconds.

## Decision
**Native IOKit glue around XNU's own power management; two phases.**
- XNU already has the whole sleep state machine (`IOPMrootDomain`, `IOServicePM`). What is missing is platform glue, so porting Linux's PM core would add nothing.
- Linux on the M1 implements idle sleep only (CPUs parked, coprocessors alive, wake on lid or power button). It does **not** use Apple's deep sleep, so there is no reference for that.

**Phase 1, idle sleep** (testable on today's rig): lid close blanks the panel, stops Wi-Fi, parks the CPUs in WFI; lid open, power key or keyboard wakes. No kernel suspend.
**Phase 2, real suspend:** full driver power-down and restore through XNU's sleep state. Not testable under the hypervisor today (see Rig).
**Not in this ticket:** Apple deep sleep through the SMC (`MBSE=susp`) and reset-vector wake. Research only.

## What exists (`docs/roadmap/research/sleep-thunderbolt.md`, part A)
- `pdpowerd` is 64 lines and only handles the power button on x86 ACPI.
- `PDArmCPU` halt/quiesce are empty stubs; our PMGR stub has no idle states.
- No lid or power-button source, no display blanking, no policy daemon.
- Untested hooks already written: Wi-Fi sleep/wake (`m1-bwfm/PDBwfm.cpp`), bridge port shutdown/resume (`m1-apcie-host/PDApcieT8103.cpp`).
- Linux references: `cpuidle-apple.c` (177 lines, dual MIT), `macsmc-input.c` (210, lid and button), `pcie-apple.c` and `apple-dart.c` suspend/resume, `pmgr-pwrstate.c`.

## Work items
| ID | Deliverable | Acceptance on the M1 | Size | Phase |
|---|---|---|---|---|
| K1 | Lid and power-button events from SMC notifications (SF-A A4) delivered to the root domain | Serial log shows lid close/open and button press | ~300 lines | 1 |
| K2 | Policy in `pdpowerd`: lid close requests sleep; button behaviour defined; idle timer | Lid close triggers the sleep path; button still shuts down on long press | ~200 lines | 1 |
| K3 | Idle sleep: quiesce Wi-Fi (existing hook), blank the panel (SF-H H7, or backlight-off fallback), deep-WFI idle state | Lid closed 60 s then opened: `en0` rejoins and pings within 5 s; battery drain during the minute reported from SMC | ~400 lines | 1 |
| K4 | Battery, charger and AC status from SMC keys, exposed the way userspace expects | Battery percentage and charging state readable | ~300 lines | 1 |
| K5 | Driver power states (`setPowerState`) for bridge, DART, Wi-Fi, SPI keyboard/trackpad, audio, USB | 20 suspend cycles without panic | ~500 lines | 2 |
| K6 | Secondary CPU shutdown and restart across sleep (needs SF-C) | All 8 CPUs back after wake | with SF-C | 2 |
| K7 | Rig support for real suspend: hypervisor handling of XNU's sleep entry and re-entry, or a boot path without the hypervisor | One real suspend/resume cycle observed | unknown | 2 |

## Rig
The hypervisor refuses the register write XNU uses to enter real sleep (`src/hv/hv_exc.c:330-336`). Phase 1 uses plain WFI and is testable now. Phase 2 needs K7 first.

## Dependencies
SF-A (A4 SMC notifications, A2 PMGR disable). SF-H H7 for a proper panel off. SF-C for K6. SF-M for Wi-Fi lifecycle correctness.

## Interfaces this ticket owns
`pdpowerd`; the platform sleep hooks in the platform expert; the battery/AC service.

## Risks
Coprocessors that do not come back after sleep (m1n1 notes such cases for the display); root-domain behaviour with no acknowledging power daemon; deep-WFI state retention under the hypervisor.
