## Goal
Plug a dock into the Mac and use it: USB devices and Ethernet first, then an external display, then Thunderbolt.

## Decision
Four stages. Stages 1-2 are **native IOKit kexts** (the Linux Type-C, PHY and role frameworks are absent from our compatibility layer and not worth importing for this). Stage 3 rides on the display ticket. Stage 4 **ports Linux's Thunderbolt code as GPL-2.0**, as we did for PCIe.

| Stage | Delivers | Typical USB-C dock | Thunderbolt dock |
|---|---|---|---|
| 1 | USB 2/3 host on the free port | Hub and storage work | Only if it falls back to USB |
| 2 | Plug detection, orientation, power-delivery status, re-plug; USB Ethernet | Ethernet works | same |
| 3 | DisplayPort alt mode | HDMI/DP output works | same |
| 4 | Thunderbolt/USB4 with tunnelled PCIe | n/a | Full function |

## What exists (`docs/roadmap/research/sleep-thunderbolt.md`, part B)
- PureDarwin's xHCI driver (`RavynXHCIPort`, 3,435 lines) already matches the Synopsys controller and sets host mode, but allocates DMA by physical address: **it cannot work behind a DART yet**. No isochronous transfers.
- `IOUSBFamily` and a CDC-NCM driver exist. **No driver for the common dock Ethernet chips** (RTL8153, AX88179) or CDC-ECM.
- `IOPCIFamily` has tunnel hot-plug logic.
- Linux references: Type-C PHY `drivers/phy/apple/atc.c` (3,412 lines, `GPL-2.0 OR BSD-2-Clause`), port controller `drivers/usb/typec/tipd/` (2,314 lines, GPL-2.0; the CD321x chip negotiates power delivery itself, the OS reads status), `dwc3-apple.c` (727), Thunderbolt `drivers/thunderbolt/apple.c` (2,437) on a 39,000-line core, tunnelled PCIe in `pcie-apple.c`.
- m1n1 prepares PHY tunables for Linux (`src/kboot_atc.c`, 927 lines); we read Apple's device tree directly, so that logic must be ported.
- Charging works electrically without any driver.

## Rig constraints
- The hypervisor owns USB-C port index 0 for its debug link and removes that port's nodes from the guest. **Only port 1 is available**, so power and data share it: test with a dock that passes power through.
- Which physical connector is index 0 is unverified.
- The guest must not touch the port-0 power domains the hypervisor hooks.

## Work items
| ID | Deliverable | Acceptance on the M1 | Size | Stage |
|---|---|---|---|---|
| L1 | xHCI DMA through the DART (SF-A A5) | A USB 2 stick mounts on port 1 | ~400 lines | 1 |
| L2 | Type-C PHY port (BSD-2 option) incl. tunables from Apple's device tree | A USB 3 stick reports SuperSpeed in both orientations | ~2,000 lines | 1 |
| L3 | Port-controller driver (CD321x status, orientation, roles) on SF-A I2C and GPIO interrupts; controller re-initialisation per plug | 20 plug/unplug cycles; state visible in the registry | ~1,200 lines | 2 |
| L4 | USB Ethernet for dock chips (RTL8153 and/or AX88179; check BSD drivers first for a permissive base) | DHCP and ping through a dock | ~1,500 lines | 2 |
| L5 | Isochronous transfers in xHCI (USB audio, webcams) | A USB headset plays | medium | 2 |
| L6 | DisplayPort alt mode: external display coprocessor, DP endpoint, crossbar mux (with SF-H H8) | A monitor on the dock shows the desktop | 8-10k lines | 3 |
| L7 | Thunderbolt: ACIO coprocessor, NHI, connection manager | A Thunderbolt dock's PCIe devices enumerate | 10k+ lines | 4 |
| L8 | Tunnelled PCIe ports and hot-plug in our bridge kext | Unplug and replug re-enumerates | ~1,000 lines | 4 |

## Dependencies
SF-A (A2, A5, A6, A7; A3 for stages 3-4). SF-H for stage 3. Stage 1 can start as soon as A5 lands.

## Interfaces this ticket owns
`integrations/puredarwin/m1-usbc/`; DART support inside the xHCI driver.

## Risks
The USB controller must be torn down and re-initialised on every plug; polled xHCI events on one CPU; whether the external-display firmware is loaded when booting through m1n1.
