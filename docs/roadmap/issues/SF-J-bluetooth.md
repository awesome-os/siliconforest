## Goal
Bluetooth on the built-in BCM4378: first a working controller, then keyboards and mice, then audio.

## Decision
Two layers, decided separately:
1. **Controller driver: port Linux `drivers/bluetooth/hci_bcm4377.c`** (2,764 lines, `GPL-2.0-only OR MIT`; take the MIT option) into a kext `PDBcm4377`, the way `PDBwfm` was done. It exposes raw HCI (command, event, ACL, SCO) to userspace through an `IOUserClient`.
2. **Host stack: in userspace, as a daemon.** PureDarwin has no Bluetooth stack (`IOBluetooth` is a 23-line stub; no socket family). Linux's stack is about 72,000 lines of GPL kernel code and is not a realistic import; no BSD has a transport for this chip. A userspace stack avoids new kernel socket families and keeps the choice open. **Choosing the stack is a work item with a licence check**, not something this ticket pre-decides: candidates named in research (from memory, unverified) are NimBLE (LE only), Zephyr's host, Android's stack, BTstack (restrictive licence), or NetBSD's in-kernel stack.

## What exists
- The Bluetooth PCI function `14e4:5f69` already enumerates at 1:0.1 with BARs assigned.
- It shares radio power (SMC `gP0d`), PERST# and DART stream 1 with Wi-Fi; MSI vectors come from the bridge (`PDApcieSetupMSI`).
- Firmware on this M1's Linux install: `brcmbt4378b1-apple,honshu-m.bin` and `.ptb`. Calibration and the device address are in Apple's device tree (m1n1 `kboot.c:1185-1300` shows which properties).
- `IOHIDFamily` has user-device support, so a userspace stack can publish keyboards and mice.

## Hard requirements from the Linux driver
- MSI is mandatory (legacy interrupts storm).
- After the function-level reset, wait 100 ms before touching any BAR or the device crashes.
- Calibration and power tables must be loaded before any radio use.
- DMA addresses must fit a 32-bit mask.

## Work items
| ID | Deliverable | Acceptance on the M1 | Size |
|---|---|---|---|
| J1 | Shared-resource ownership in the bridge (SF-A A8): Wi-Fi power-down must not take the port from under Bluetooth | Wi-Fi off/on with Bluetooth attached leaves both working | small; **do first** |
| J2 | Kext skeleton: reset, window config, OTP parse, firmware name | Log shows stepping, vendor and chosen firmware file | ~500 lines |
| J3 | Rings, MSI, firmware boot | "bootstage 2" logged; control ring opens | ~1,500 lines |
| J4 | HCI open, calibration, power tables, device address | `HCI_Reset`, `Read_BD_ADDR`, `Read_Local_Version` succeed while Wi-Fi keeps pinging | ~600 lines |
| J5 | Raw-HCI user client and a small CLI | An LE scan lists nearby advertisers, with no host stack | ~600 lines |
| J6 | Host-stack selection: licence, LE and classic coverage, HID and audio profiles, size; one-page decision | Decision recorded in this issue | research |
| J7 | Daemon with LE pairing and HID-over-GATT, publishing through `IOHIDUserDevice` | A Bluetooth keyboard types into the terminal | large |
| J8 | Classic HID, then A2DP as a virtual audio device in `coreaudiod` | Headphones play `pcmplay` output | large; needs SF-I I5 |

## Dependencies
SF-A A8. The Wi-Fi driver's lifecycle (SF-M) for coexistence testing. SF-I for audio profiles.

## Interfaces this ticket owns
`integrations/puredarwin/m1-bluetooth/`; the HCI user-client ABI.

## Risks
- Host-stack choice dominates the schedule.
- Wi-Fi/Bluetooth coexistence behaviour is unknown.
- The firmware image must fit the DMA arrangement shared with Wi-Fi.
- Device address, calibration, firmware and link keys are identifiers or secrets: never commit them.

## Research
`docs/roadmap/research/sound-bluetooth.md`, part B.
