# SF-AF: USB-C, Thunderbolt and external display on J293

Owner: CODEX-TB. Survey started15 November2026; current update16 November03:45Z.
Thunderbolt/dock trials and AVE remain paused. The standing Firefox/desktop
kernel grant uses2-minute notice, locks, UART and rollback, without an owner-idle
or hourly wait. Qualified45 is installed normal;44 remains the immediate rollback.

| Piece | Observed on the M1 | Built / still pending |
|---|---|---|
| Devfs clone recovery | Qualified41/43:60000 reservations balanced, slots3→3/fail0; GL hash50/Vulkan50;44 GL/Vulkan10/slots3 | Actual browser helper/relaunch integration remains with FX |
| Killable GPU waits |43:60 binary/timeline infinite-wait signal cases reap within2.6ms; same drivers retained44 | Every DMA/recovery wait is not qualified killable |
| Shared decoder capture |44:3x14 BO pin/import/destroy; strict H264600+HEVC300 visible NV12 hashes+PTS match Linux, held64/copied0, jobs900/done900/no faults/remaining allocations | GPU consumer completion, coherent browser zero-copy/CPU/presentation remain media/FX gates |
| Producer death |44:3 own children unmap/SIGKILL/reap2.53–2.54ms; next CREATE retires context, consumer data survives pressure; final allocations0 | Lazy CREATE retirement, not immediate exit cleanup or GPU-fence proof; v1 immediate-INFO predicate stays FAIL |
| Pinch / speaker |44 ring776B/version2; extra speaker cap0 per owner, boot argument retained | Physical pinch and sense calibration unobserved; current model has0 violations |
| ext4 EOF allocation |45:28 IO cases/7 resizes/29 byte oracles;247 initialized blocks+681 hole/unwritten tails pass; st_blocks data-only assertion FAIL preserved (extent metadata) | Real VM frozenRC4/fixedRC0; old errors unrepaired, no whole-root cleanliness or latency improvement |
| Firefox rendering / FPS | Original41 fresh-launch20 mapped/quit/reap passed but all page paints failed | Parent-side rendering fixes and current presented-content metrics belong to FX/TERM/DESK; no kernel FPS claim |
| TS3 router / PCIe | Earlier native trials authorize TS3, bring NHI/PCIe tunnel up and enumerate12 functions including4 xHCI and I210 | No new dock trials while paused |
| Dock Ethernet | Earlier en1 link1Gb/s | No DHCP lease or demonstrated packets |
| Dock USB / Samsung | No USB device/HID typing, HPD/EDID/external output or10-cycle hotplug qualification | Source/host/SDK preparation only; DP provider graph missing |

Private qualification evidence is under runs/sftb/firefox-install-004{1,3,4}
and firefox-task-death-0044{,-v2}. Current45 capsule5c323fb6/prelink2BFF4349, same
DESK9-next001/socket/inject verified; all locks/UART released03:45:05. Root
read-only fsck returns4 with two legacy EOF records and bitmap/count errors; no
repair or clean-filesystem claim. The previous startup-cache record is absent
from the pre45 check, so no improvement is credited to45.

Next kernel work: obtain steady-state/stage timing for fsync/unlink before
selecting a latency fix, and consume media/FX's actual GPU-consumer/presentation
measurements. The descriptive postboot30-file sample is worse, not a speed win.
No further install is scheduled. Dock/AVE machine work stays paused. The
completed45 boot sequence and exact evidence are recorded at the end below.

## Historical target and observed state (owner change; updated15:20Z)

The CalDigit TS3 Plus remains attached to left-front Type-C port 1 and is
now the **first hardware target**. Direct USB/DWC3 and a USB-C DP adapter are
retained source paths, deferred until the owner makes that hardware available;
no further direct-USB-only boots are planned. The TS3 is Thunderbolt 3-only:
its USB ports cannot exercise the M1's direct DWC3 host before a PCIe tunnel
and PCI xHCI are operating.

**Latest partial CIO26:** the actual TS3 Plus router (vendor003d/device0011)
was retained on both UP lanes after disabling the unavailable optional NVMEM
switch service. Administrator approval then returned ENOMEM12 before the
native PCI preparation hook. The candidate failed qualification; the normal
installer restored CIO25 at15:43:53Z and UART was reaped15:43:54Z. No independent
PCI inventory ran after that failure. The separate remaining NVM-102 message
is from optional retimer setup. No PCI tunnel/peripheral result is claimed.

**Observed in Darwin:** installed non-AVE CIO25, source `96ae5be19`,
capsule `be6732426e81…`, installed15:25:21Z, qualified15:27:43Z, UART reaped
15:27:45Z. The missing ACIO1 GPIO41 default is now observed fixed:
`0x0087600e`→`0x0087622f`, matching Linux's full word. Firmware reaches
Phase3/Connected and both lanes enter CL0/report `TB_PORT_UP=2` for the first
time. M3 control clears to0 and TBT3/bonded PORT_CS18 becomes0x3710, both
matching Linux without a guessed control write. The router is still absent:
remote0, TS3 approval ENODEV19, independent PCI inventory exit2/functions0.
No dock PCI/USB/Ethernet/display milestone is claimed. Standard desktop/
injection, Wi-Fi, NHI/RTKit/PHY, catalogue, UID and snapshot ABI checks pass.
All57 modules are non-AVE, no pdave argument or endpoint mapper/MSI admission;
only the USB/PHY module changed from CIO24, the qualified non-AVE rollback.

Quiet60s: kernel2.44799% of one CPU, aggregate98.02729% idle,24 NHI IRQs
including query boundaries (all22 other sources zero); compositor3.85521%
with cleanup2.60706%, main0.85826%, HID0.38698%. Warm50M83.676–83.769ms,
100k syscalls14.643ms. No interrupt storm or fixed-work slowdown appears;
the reported AppKit slowdown's cause remains unresolved.

**Previous CIO22 observation:** kernel source `455ea303f`,
capsule `8f3800a8bf4c…`, qualified14:31:04Z; UART reaped before lock release
14:31:06Z. Standard desktop/injection, Wi-Fi, RTKit/NHI/PHY and actual
catalogue/UID/snapshot checks pass. The new five-control comparison rules out
a root sleep or TBT3-disable mismatch: those controls agree with active Linux.
The primary port's TBT3-mode/bonded/configured flags are clear only in Darwin;
these are observations, not bits to force. Both lanes remain unplugged7,
remote0. Actual TS3 approval returns ENODEV19/count1 and independent PCI
inventory exit2/functions0, no native host. Firmware logs again show cable411,
Phase1 then Phase2, no Phase3. All57 modules are non-AVE; no pdave boot argument
or endpoint mapper is present. The installed image includes the separate
PCIe-C physical-power access guard, which has not executed without a peer.

**Previously qualified CIO21 lifecycle:** kernel source `a7cf6b22e`,
CLI `0d36fe096`, capsule `e0519170e573…`, qualified13:46:19Z; owned UART
reaped13:46:21Z before lock release. The normal desktop/Wi-Fi/NHI/RTKit/PHY and
snapshot ABI remain healthy. At13:45:17Z an actual administrator peerless
retry completed the real domain/ring/worker/IRQ/DMA/DART drain, RTKit and ACIO
poweroff, broker-verified PHY release, reacquisition/calibration and second
NHI startup. The native registry retains one host-router and one physical-port
node; no stale children appeared. UID65534 denial leaves count0->0, root retry
advances0->1, and a second root retry is denied1->1. M3 control reads3 before
and after START in both generations, so bit0 was already enabled.
Both lanes still report UNPLUGGED7/remote0. The second real TS3 approval returns
ENODEV19/count2 and actual PCI inventory returns exit2/functions0. The actual
catalogue still contains only three PCI bridge personalities and exact Wi-Fi;
57 code modules, no AVE kext, no pdave argument and no endpoint mapper. This is
peerless lifecycle evidence, not a dock/peripheral/physical-hotplug milestone.

**CIO16 failed qualification:** the same non-AVE diagnostic image reached
healthy NHI/RTKit/Type-C/PHY startup, but the standard desktop and inject
directory were absent at75s. It was restored to qualified CIO15 at11:45:55Z;
the owned UART reader was reaped at11:45:56Z before lock release. No new
compositor-start record or first SF_SESSION marker was captured. The cause
is unresolved. A same-image CIO16B repeat ran at12:00Z,
with process/launchd state capture and bounded additional session waiting.
That repeat qualified at12:03:54Z (UART reaped12:03:55Z): the normal desktop
was already present at75s and all snapshot/privilege/ABI checks passed. The
earlier startup failure has not reproduced and its cause remains unknown.

**Not observed in Darwin:** the dock router, authorization, any dock PCI
function, Ethernet link/DHCP, dock USB enumeration/HID/read-only storage,
HPD/EDID/external scanout or physical dock hotplug cycles. Host-router
readiness is not dock discovery. PCIe-C cold-init/link helpers, endpoint
DMA/bridge work and external-display sources remain separate source/host
evidence. DESK5's second-output userland half is built; K1–K13 remain the
kernel contract. CIO01/CIO02/CIO04–06 failures and recoveries are retained
below, not relabeled as successful boots.

The shortest path is shared port-1 PHY ownership → TBT ATC calibration →
ACIO M3/RTKit power/reset/firmware and SID1 DART → NHI DMA/interrupts → the
software manager's host/router enumeration and authorization → apciec1
PCIe tunnel/root/hotplug → existing I210 and PCI xHCI drivers → dock DP-IN
and dcpext. Each boundary requires a Darwin register/trace observation
compared with the saved Linux TS3 trace before calling the next stage ready.
Bench Type-C port 0 stays reserved. Ordinary Wi-Fi PCIe port 0 is a different
namespace from that protected USB-C port; retain its working bridge.

## Reference and baseline

Omacom `sources/gpu-plan/linux-aurora`, detached at
`6a08a08e5755`, is the port reference. HEAD was not moved. The existing remote
ref `origin/integration/aurora` is `39d5f88ab4e5`; its DWC3 source differs. Do
not silently combine the two versions. The local history at HEAD is shallow;
`git log --all -- drivers/usb/dwc3/dwc3-apple.c` also exposes newer fixes,
including `cda9c909149d` (set driver data before first cable connection),
`368d058a36b5` (fixed-hub handover) and `dc838cbcc3ff` (suspend-to-idle).
J293 has direct USB2 connections, not the newer J700 fixed-hub topology.

PureDarwin reference: `sources/puredarwin-rebase`; build uses the lab recipes,
not edits to that checkout. Existing patterns read: m1-apcie-host (bridge,
posted MMIO, DART), m1-bwfm (network interface and DHCP), m1-rust-kext,
m1-drm72, m1-lkpi-extras, m1-dcp and m1-gpu-dt/PDDispDT.

Before the first change, `m1-native-boot/install/build.sh
runs/m1-usbc/kit-base` reproduced GFX2's k2 `kernel.bin` **byte for byte**:
SHA-256 `7ad3b4a9e6e7e3e15ab038c9a61d824c6d1710f92b072ea418b207db911449d6`.
Comparison: `runs/gfx2/kit-k2/kernel.bin`; build log and kit remain private in
`runs/m1-usbc/`. This baseline includes `pdavd=1`, `if_attach_nx=46`, the
IO80211Family output counters and LinuxKPI taskqueue fix 0041. This is a local
rebuild comparison, not a new readback of SFBOOT. GFX2 confirmed k2 in the
coordination log at 00:54:36Z. The first installation and its evidence are recorded below.

## Inventory and port order

Linux paths below are relative to the reference checkout. This table retains
the initial survey inventory; the current target above and acceptance order
below supersede historical next-step notes.

| piece | Linux driver + path | what exists in SiliconForest today | port plan | size/risk |
|---|---|---|---|---|
| CD321x bus | `drivers/i2c/busses/i2c-pasemi-{core,platform}.c`; J293 `t8103-jxxx.dtsi` I2C0 | `PDI2CFamily` controller/client ABI; m1-sound `pd_i2c.cpp` already ports the polled PA Semi transfer engine, with reusable `pd_soc.cpp` power/MMIO | Reuse the existing Linux port for I2C0; serialize both CD321x clients on one bus. Start with port 1 state reads | small; shared bus also serves the protected serial port |
| PD, orientation, data role, alt modes | `drivers/usb/typec/tipd/{core,i2c}.c`, `tps6598x.h`, `cd321x-{dp,pm,setup}.h`; `apple,cd321x` | `m1-typec` now reads port 1 over the existing polled I2C transport; APP/status/TBT observed in tc01. No setup/mux/role writes yet | Port the CD321x branch, length-prefixed I2C blocks, APP/S0 setup, status/event handling and ordered mux/role callbacks; use its existing debounce and retry semantics | medium; incorrect role/reset order can kill serial or reset SoC |
| SPMI / ACE | `tipd/spmi.c`, same tipd core | no SPMI tipd transport needed for this board | J293 binds I2C addresses 0x38 and 0x3f. SPMI is a different transport, not a J293 prerequisite. The controller's firmware owns PD negotiation; host reads status and sends its 4CC commands. Do not invent an ACE RTKit endpoint or flash/update firmware | no J293 SPMI port; firmware interface needs live confirmation |
| ATC PHY / pipehandler | `drivers/phy/apple/atc.c`, `dptx.c`, `dptx.h`; `apple,t8103-atcphy` | no Type-C PHY provider; generic PHY declarations in DCP are insufficient | Port T8103 register/tunable, USB2, USB3, orientation, reset and PIPE routing paths first; keep DP/TB additions in same upstream ordering | large (whole atc.c 5,504 lines); calibration, power domains, cable-dependent reset |
| DWC3 Apple host wrapper | `drivers/usb/dwc3/dwc3-apple.c`, `core.c`, `host.c`, `dwc3-apple-state.h` | RavynXHCIPort has `snps,dwc3` support for an already powered host; no Apple lifecycle | Port Apple core setup, vendor registers and role/disconnect sequencing; publish an xHCI child only after PHY/core/PIPE ready | medium; leaving it running across disconnect loses subsequent devices |
| xHCI, hubs, USB device model | `drivers/usb/host/xhci*.c`, `drivers/usb/core/` | `RavynXHCIPort` is in the arm64 boot set, supports PCI, DWC3, hubs, polling/interrupts and hotplug; bridges `IOUSBFamily` through `RavynXHCIUSBBus` | Reuse host/class stack, add Apple prepared-host binding and translate **all** DMA allocations, contexts, rings and transfers through USB DART; audit teardown before detach | medium/large; current physical-address ring programming is not an Apple DART adapter |
| USB DART | `drivers/iommu/apple-dart.c`, `io-pgtable-dart.c`; two per-port DART instances, streams 0 and 1 | m1-apcie-host mapper is hardwired to dart-apcie0/stream1; DCP has its own mapper; `m1-usb-dwc3` paired USB1 domain compiles as SDK objects only | Factor/use per-instance T8103 mapping for both USB stream paths, with invalidation and fault reporting; never reuse Wi-Fi stream/window | medium; wrong IOVA causes arbitrary DMA faults |
| HID | `drivers/hid/usbhid/` | `IOUSBHIDDriver`, keyboard/mouse, PDUSBHIDDevice, IOHIDFamily in boot set | Reuse; require physical keypress and event delivery after hotplug | small once host works; enumeration alone insufficient |
| storage | `drivers/usb/storage/`, UAS separately | IOUSB mass-storage sources and Ravyn xHCI BOT disk; IOStorageFamily, GPT/MBR, FAT/ext4 | Reuse BOT initially; explicitly mount external test media read-only by observed identity; no internal-disk-number assumptions | small/medium; UAS not established; never automount unknown media writable |
| direct USB Ethernet | `drivers/net/usb/{cdc_ncm,cdc_ether,r8152,ax88179_178a}.c` depending on actual adapter | `PDUSBNCM` source, IONetworkingFamily; NCM is not in upstream kc-arm64 boot list; no evidence of r8152/AX88179 or generic CDC-ECM support | Start with actual adapter class/VID:PID inventory; include NCM if applicable, port adapter driver otherwise; reuse pd-dhcp for new interface | hardware-dependent; do not claim all USB Ethernet is NCM |
| external DCP and scanout | `drivers/gpu/drm/apple/{dcp,apple_drv,connector,iomfb*,dcpext_scanout,dcpext_drm}.c` | PDDcp drives internal panel. External scanout functions in `kext/pd_dcp_linux.c` are stubs; pre-probe mailbox path is hardcoded `/arm-io/dcp`; PDDispDT supplies internal topology | Publish dcpext plus display/DART graph; per-instance RTKit/mailbox context; compile real external scanout and required helpers; preserve internal panel | large; second firmware instance, reserved memory, DMA and existing singleton assumptions |
| DP alt mode / link | `drivers/gpu/drm/apple/dptxep.c`, AFK/EPIC, connector/iomfb; `drivers/phy/apple/dptx.c`, atc.c; tipd HPD | dptxep is compiled, but its PHY/Type-C infrastructure and external topology are absent | CD321x DP status/HPD → Type-C mux → ATC lane/crossbar → dcpext dptx-port EPIC; firmware/link callbacks configure PHY; EDID/modes through display driver; KMS commit after link ready | large; both ports share one external display engine |
| ACIO / NHI | `drivers/thunderbolt/apple.c`, `nhi.c`, `ctl.c`, `tb.c`, `usb4.c` | no IOThunderboltFamily or Apple NHI port found | Port Apple ACIO power/tunables + M3 RTKit, child NHI DART, rings/interrupts and connection manager; retain Type-C out-of-band cable/orientation handoff | large; ACIO readiness is not dock authorization/enumeration |
| PCIe tunnel / dock USB and Ethernet | `drivers/pci/controller/pcie-apple.c`, Thunderbolt `tunnel.c`, `path.c`, `switch.c` | Wi-Fi bridge only drives apcie port 0; no external apciec tunnel host. Existing `PDFBSDem` source/personality includes I210 `15338086`, but is omitted from the M1 boot set | Add per-port apciec root/tunnel bridge, DART/RID/MSI, dynamic downstream scan/removal; then existing PCI xHCI and existing PDFBSDem/I210 after auditing its DMA path | large; hot-remove DMA, bridge resources; dock chips must be observed |
| DP/USB3 tunnels | Thunderbolt `tb.c`, `tunnel.c`, `apple.c` DPIN handshake; ATC tunnel paths | absent | DP source is still dcpext, transported over TB to dock DP output. USB3 tunnel differs from PCIe-attached dock xHCI; enumerate topology to choose | large; DP is not a PCIe framebuffer merely because dock has PCIe devices |
| desktop outputs | Chonkstep `crates/wm-darwin/src/{kms,server,glrender}.rs`; Linux `wm-wayland` multi-output policy | Darwin Kms holds one connector/CRTC/plane, selects first connected; server creates one `PDGOP-0`. Linux frontend has multi-output/hotplug machinery, not automatically used by Darwin | DESK4 owns change: enumerate cards/connectors, maintain per-output commits/flip events, publish/withdraw wl_output, Samsung primary, fallback panel on removal | medium/large; do not modify compositor in this stream |
| hotplug userspace | Linux kernel USB/TB/DRM events; desktop connector policy, network service, storage policy | pd-netd is Wi-Fi-oriented; pd-dhcp takes an interface; no generic USB storage automounter established | Own wired-interface DHCP lifecycle and explicit read-only removable mount policy here; kernel must first enumerate real devices | medium; route/resolver ownership with Wi-Fi must be explicit |

## J293 wiring and sequencing

`arch/arm64/boot/dts/apple/t8103-j293.dts`: Type-C 0 is **USB-C Left-back**,
Type-C 1 **USB-C Left-front**. `t8103-jxxx.dtsi`: CD321x 0/1 are I2C0
0x38/0x3f, sharing GPIO interrupt 106. Connector graph ports 0/1/2/3 connect
respectively USB2 DWC3, ATC SS lanes, ACIO Thunderbolt and dcpext routing.
Both routes target **one** dcpext: at most one external monitor on this M1.

Port 0/1 DWC3 bases are 0x382280000/0x502280000, AIC 777/857;
USB DART pairs 0x382f00000+0x382f80000 and 0x502f00000+0x502f80000;
ATC PHY bases 0x383000000/0x503000000. These are Linux DT facts, not fresh
MMIO observations. Resolve actual ADT ranges before access.

The installed kit packs `usb-nodes=remove0`. `b1/src/pdsf_boot.c` prunes port 0
USB, PHY, ACIO, apciec/DART, HPM and DFP nodes; this protects the bench port.
Keep this setting. Physical bench cable location still needs verification;
do not infer it solely from node pruning. Start on preserved **port 1** only.

Upstream DWC3 order matters: configure PHY for cable/orientation; initialize
core without starting xHCI; finalize PHY and route PIPE; then start xHCI.
On detach stop xHCI before PIPE returns to dummy, then reset/stop PHY. CD321x
resets the eUSB2 repeater on cable changes, requiring a new PHY/core cycle.
A generic xHCI root-port poll alone cannot implement this lifecycle.

## Dock identity and Linux observation

The first Linux survey was collected during tc01, without configuration changes
(`runs/m1-usbc/tc01-linux.txt`, private). The owner-connected dock identifies as
**CalDigit TS3 Plus**, Thunderbolt generation 3, `authorized=1`, on domain/port
1. Linux enumerates its PCIe tunnel and these endpoints:

| endpoint | PCI ID | Linux binding | SiliconForest reuse |
|---|---|---|---|
| two Fresco FL1100 USB controllers | `1b73:1100` | xhci_hcd | RavynXHCIPort, once PCIe tunnel/DMA exists |
| ASMedia ASM1142 USB controller | `1b21:1242` | xhci_hcd | same |
| Intel JHL6540 USB controller | `8086:15d4` | xhci_hcd | same |
| Intel I210 Gigabit Ethernet | `8086:1533` | igb | existing PDFBSDem source/match; not yet loaded/tested on M1 |

Thus this dock's Ethernet is **PCIe I210**, not an assumed USB Ethernet or
Aquantia chip. USB audio/hub/HID interfaces also enumerate behind the dock in
Linux. This does not establish any of those endpoints under Darwin.

Linux reports external `card3-DP-2` connected, with 3840x2160 among its modes,
and `card3-eDP-1` connected at 2560x1600; DP-1 is disconnected. The assignment
identifies the Samsung as the dock monitor; no EDID metadata was collected to
independently establish its model. NetworkManager and bolt.service were active,
systemd-networkd inactive. The Type-C port1 PD role was device/sink while its
PCIe tunnel was active: this is not a reason to start DWC3 as a peripheral.
CD321x's direct USB controller role uses DATA_STATUS and is NONE for this
TB-only connection (no USB2/USB3 connection flags).

No TS4 or identified Kensington was present in this capture. The Kensington
model remains unknown; no local doc names it. Future surveys should also save
EDID metadata and the network service's interface/route policy privately;
current evidence establishes service presence, not its complete hotplug policy.

## First implementation and real M1 evidence

- `d41d97274`: initial source survey; `32270c69d`: `m1-typec`, reusing
  `m1-sound/kernel/pd_i2c.cpp` (the Linux PA Semi port) and `pd_soc.cpp`.
- tc01 (01:07:57–01:10:42Z, gui.lock and bench queue): clean Darwin reboot,
  one Linux survey/install session, `sf-bootbin-install.sh`, one-time bless,
  M5 UART capture opened before the new boot with routing retried through reset.
  Linux `--show` verified the installed capsule hash:
  `f10ca750111b5dca507b2435799571d2c2d15186afa294ff8244455ac012a861`;
  `.old` is k2 `7ad3b4a9…`. `boot.bin` remains `81d58f1b…`.
- **Observed in Darwin:** `PDAppleTypeC` registered/matched; I2C0 revision 6,
  divider 15; HPM1 vendor register nonzero, firmware mode `APP `;
  `status=0x100cb20d`, `data-status=0x16110081`, cable present, normal
  orientation, Thunderbolt flag set, USB2/USB3/DP/USB4 flags clear. At the
  post-boot check: 62 samples, zero errors; later 612 samples, still zero errors. The sampler does not send PD
  setup/4CC/IRQ-clear commands and does not start a PHY or host controller.
- Capture `tc01-uart.txt`: new Darwin banner, no panic line through the
  captured first-boot interval. The compositor `.next` and inject directory
  were restored before releasing the lock. **Capture correction:** the owned
  reader initially remained open after release; stopped at 01:15:20Z. GFX2
  was notified that any intervening captures may have shared UART bytes;
  supplemental bytes are in `tc01-uart-final.txt`. The first tc01 boot
  predates that overlap. Future cycles must stop/reap their reader before
  releasing gui.lock.
- `7812ea1f5`: subsequent source-only change distinguishes the PD role from
  DATA_STATUS's USB role and publishes one immutable `port-state` OSData
  snapshot for kernel clients. Host framing/role tests and SDK build pass.
  **Not installed**: the disk still runs the `32270c69d` sampler.
- `9d07d0958`: `m1-usb-dwc3` paired USB1 DART domain/owned DMA buffers,
  ARM64 SDK objects built; reused table tests pass. **No DART hardware test,
  xHCI integration, DMA, USB enumeration or hotplug qualification yet.**
  The current xHCI stop/free path also needs worker joining before a PHY/core
  reset can safely remove and replace an entire controller.

What a person can do now: leave the TS3 attached and inspect its Type-C cable
status in Darwin using `/private/var/db/sftb/ioreg -rc PDAppleTypeC -l -w0`.
That private copy is the open-source PureDarwin ioreg build, staged/hash-checked
because the installed system did not have ioreg. They cannot yet use dock USB,
Ethernet or external display under Darwin. No physical plug/replug or HID
input was generated by this stream.

## Recovery finding and validation

The Linux SFBOOT listing still contains the original **14,423,700-byte**
`boot.bin.old` (`8f300c87…`), which stage 1 refuses. The old `--rollback` branch
blindly swapped both old files and would reinstate it. `553aae9d0` validates
**the complete resulting pair on a read-only mount before any rename**; it
refuses that old pair and preserves the current files. Normal installation
of the verified base kit's `boot.bin` plus `kernel.bin` remains the recovery
route to k2: use the installer with both files from
`runs/m1-usbc/kit-base`, copied to Linux. Do not use an older copy of the
installer's unvalidated `--rollback`.

Seven scratch-file tests execute the actual rollback branch (mount commands
recorded): new script 7/7, original script fails four refusal cases. Kit tests
11/11 pass against tc01. No real hardware rollback was needed or attempted.
Linux `e2fsck -fn` before tc01 returned 4 with existing crash/restart findings;
this stream did not repair the root. All six AGENTS.md repository checks passed again after the rollback/source changes;
focused SDK/host checks are recorded separately.

## Observable milestones

The owner revised the acceptance order on 15 November 2026:

1. **Dock PCI enumeration first.** Keep TS3 attached. Observe current TBT
   negotiation, acquire the shared ATC lease without taking the bench port,
   validate/power the ACIO1 M3/RTKit/DART/NHI chain and its interrupts, then
   enumerate/authorize the actual router. Extend the existing native bridge
   to the Type-C port-1 `apciec1` tunnel root, with separate ECAM, RID/DART,
   MSI and downstream removal ownership. Acceptance is Darwin ioreg/PCI
   inventory of the saved Linux endpoints: two `1b73:1100`, `1b21:1242`,
   `8086:15d4`, and I210 `8086:1533`. No PCIe devices have appeared yet.
2. **Dock Ethernet.** Audit/include the existing FreeBSD-derived
   `PDFBSDem` I210 personality (currently omitted from the M1 boot set),
   including its DMA behind the new root. Require the physical I210 to
   attach, report link, and obtain an actual DHCP lease through pd-dhcp.
   Check routing/resolver interaction with existing Wi-Fi. Port Linux igb
   only if the existing em/I210 path proves insufficient.
3. **Dock USB.** Bind the existing PCI-capable Ravyn xHCI/IOUSBFamily stack
   to the actual controllers, with their correct PCIe DART/MSI and drain
   lifecycle. Then ask for a USB keyboard in the TS3 Plus **front USB-A
   rectangular port**; require enumeration and real typing. Follow with a
   test stick in that same port and an identity-checked read-only mount.
   Do not route these controllers through the direct USB1 DWC3 DART.
4. **Samsung through the dock.** Bind the real ACIO DP-IN tunnel/clock and
   shared crossbar to dcpext/DART/RTKit and the built compositor half.
   Require HPD, EDID/preferred modes, 3840x2160 scanout on the Samsung and
   the second connected connector/CRTC under `--outputs`, keeping the
   panel running. K1–K13 apply; framebuffer DMA and DCP/PHY teardown must
   drain before a tunnel or backing allocation can disappear.
5. **Dock hotplug.** After static functionality, request ten controlled
   unplug/replug cycles of the TS3 cable on left-front Type-C port 1,
   captured on UART: no panic, stale DMA, stuck link, lost internal panel
   or permanently missing Ethernet/USB/display after reconnect.
6. **Later hardware.** Retain direct USB2/3/DWC3, USB-C DP adapter, TS4 and
   Kensington source paths. Inventory/qualify each only when available;
   these no longer delay dock bring-up or cause dedicated boots now.

Each kernel install is one logical change on the verified base, using only
`install/linux/sf-bootbin-install.sh`, with rollback, two-minute coordination
announcement, gui.lock and M5 UART capture before its first boot. Default
boot remains Linux; return the shared machine to SiliconForest. Track current
state in `runs/m1-coord/status-CODEX-TB.md`. Sources/captures/identifiers stay
outside git. Ported GPL/MIT notices are retained per file and in kext NOTICE.


## Historical next implementation boundary (tc01; superseded above)

The installed tc01 image is an experimental collection built by
`m1-typec/build-kc.nix`, not a change to the default rebase collection recipe.
A default kit rebuild currently **omits the Type-C kext**. To reproduce the
installed image, retain the immutable tc01 KC store path recorded above and
pack it with `SF_KC=<that path> SF_EXTRA_BOOTARGS=pdtypec=1`; the private kit
is already preserved. Building the current Type-C recipe instead includes
7812ea1f5's later, SDK-only snapshot change and is a new candidate.

Next is the actual host path: wire xHCI ring/context/transfer allocations to
the paired DART adapter (initial MSC traffic must bounce too), implement
T8103 USB2 ATC/DWC3 setup on the preserved port, and serialize/drain xHCI
workers before resetting that controller on cable removal. Then USB3 tuning
and physical acceptance. PD versus direct USB role must stay distinct: this
TS3 cable reports TB without a direct USB connection, so it must not start
DWC3. Direct keyboard/storage/Ethernet hardware and a USB-C display adapter
have not been observed attached; the status file requests them without
waiting for answers. Milestone 1 remains unfinished.

### xHCI DMA and removal source, 2026-11-15 02:30Z

`m1-usb-dwc3/build-xhci.nix` now SDK-builds a replacement RavynXHCIPort
with the paired USB1 DART backend. All thirteen xHCI allocation sites and all
buffer address sites use the adapter on Apple, including MSC bounce payloads.
The IOVA allocator also honors physical-mask alignment: a 64KiB-aligned CPU
buffer alone does not keep a translated single-TRB transfer within 64KiB.
PCI/non-Apple allocation paths remain the upstream path.

Removal closes new call admission and drains retained hotplug/bulk workers
and active class operations before requiring hardware halt/reset. Failed
drain/reset retains DMA; `PDAppleUSBDrain` returns busy and the parent may not
reset the PHY. Old USB bus/storage clients retain the controller until they
finish, and successful stop aborts transfers and terminates children. The
provider's `pd-usb-dma-live` property covers partial attachment too.

Host ASan/UBSan checks passed 100 concurrent removal cycles and 4,194,304
fragmentation/alignment cases. The ARM64 SDK bundle builds; six repository
checks pass. This source is **not installed** and no xHCI DMA, enumeration,
HID, storage mount, DHCP or ten-cycle USB hotplug has been observed. ATC/DWC3
parent lifecycle integration is the next step; this is not milestone 1 done.

### ATC USB2 and DWC3 lifecycle source, 2026-11-15 02:35Z

The experimental `m1-usb-dwc3/build-kc.nix` now builds the complete host
bundle plus the Type-C snapshot provider, replacing the original xHCI
bundle once. The collection passed its segment overlap check (zero pairs).
`pdtypec=1 pdusbhost=1` enables the port-1 USB2 lifecycle; the daily kernel
recipe is unchanged. This collection is not installed at this checkpoint.

The `m1-atcphy` port has external reset/clamp, dummy PIPE parking with
acknowledged live handoff, USB2/common power and status polling, T8103
post-tunable setup, lanes-OFF/orientation routing and shutdown. Its kernel
adapter validates the complete ADT common tunable group and actual register
layout before PHY writes, reads port-1 fuse fields, applies the original
common-a/AXI2AF/common-b order, prepares paired DART while DWC3 is reset,
initializes DWC_usb31 and publishes the prepared xHCI nub. USB2 HOST is
selected after core reset, as required by the current Linux Apple glue.
DWC3 identity/capability and reset timeouts are checked; gadget event DMA
is disabled. Removal refuses PHY reset until xHCI drain and synchronous
termination finish, including the failed-attachment DMA case.

Host ASan/UBSan checks cover 100 USB2 power/orientation cycles, failed PIPE
acknowledgement, power/tunable failures, 16,384 compact-record cases and
DWC3 core/reset/role ordering. These are register models, not hardware
observations. SuperSpeed, DP/Thunderbolt PHY modes and actual USB device
behavior remain unfinished.

At 02:32Z a read-only M1 sample still reported the dock's Thunderbolt-only
cable state, no USB2/USB3/DP flags, 3,217 samples and zero errors. The source
host lifecycle deliberately defers that state; dock USB requires NHI/PCIe
tunneling. Direct USB devices remain requested from the owner. Work proceeds
on the Thunderbolt and external-display source/host paths without waiting.

### USB02 first boot and packaging failure, 2026-11-15 02:44Z

USB02 capsule `b43f3cb93fe86b6e14d2da2f0a3296dc93b8c1bc8c1f635849f3b9c30c1d9b37`
from KC `/nix/store/9k2xml5v9xzyn9jw94p2p0hplzzl80ki-puredarwin-kc-arm64-0.1`
booted under gui.lock/bench after Linux installation. UART captured the Darwin
banner and no panic marker. The updated Type-C snapshot/USB-role source runs
on M1 and reports the same TS3-only state, USB role none, 62 samples/0 errors.
The xHCI/PDAppleUSB bundle **failed to load**: its new versioned dependency
on PDAppleTypeC could not be satisfied because that observer is not declared
a versioned library. No ATC/DWC3/DART operation ran from the host service.
The reader was explicitly reaped before releasing gui.lock (02:43:58Z).

`638e824ef` removes that unnecessary dependency: the host uses IOService
properties and links no Type-C class symbols. USB03 collection/kit and 11 kit
checks pass; the hardware retry is pending DESK4's announced session/reboot
window and its explicit request to wait for its done line. `b3d366f26` also
fixes the synthetic nub's name/resource overrides before any host attachment:
its parent is PDAppleUSB, so IOPlatformDevice must not cast that parent to
IOPlatformExpert. The rollback baseline remains the verified tc01 kit;
the default boot remains Linux. Nothing was pushed.

### External-display / Thunderbolt source backends, 2026-11-15 02:59Z

`m1-atcphy` now carries directly extracted T8103 DP AUX, four DP link-rate
PLL/lane programs and the distinct Thunderbolt DP pixel-clock program,
with original SPDX/copyright and a reproducible extraction tool. Native
callbacks replace register access/delay; the caller must hold the PHY lock
and already own a powered/calibrated mode. Failed tunnel-clock APB shutdown
retains ownership and refuses retuning. Host ASan/UBSan covers rates/PMA
masks, repeated-rate behavior, shared-PLL refusal and command/lock/stop
failures. The DP programs SDK-compile; none has trained a link on M1.

`m1-thunderbolt` retains upstream DP-IN state/token/provider code and its 35
tests byte for byte (128,371 assertions through a host fixture adapter,
not Linux KUnit/hardware execution). Its SDK objects additionally port Apple
NHI ring budgets/layout/PDF, atomic VALID/E2E setup, descriptor encoding,
RTKit SRAM bounds, the M1's single-budget HPD/INACTIVE_ACK handshake and
ACIO startup/unwind order. Native platform/RTKit/DART/control-channel/CM and
PCIe-C bindings remain missing; there is no attached Thunderbolt driver.
Register-model cases cover failed disables and every startup/teardown step.

`m1-dptx` validates external dcpext/dispext register resources, mailbox IRQs,
carve-outs 73/74, firmware compatibility and the port-1 routing plan; its
223 missing/truncated-property cases and overflow/ABI refusals pass. It has
no MMIO or synthetic-tree publication yet. T8103 uses the already compiled
normal IOMFB pipeline, rather than the later-chip `dcpext_scanout` path.
Two integration gaps were confirmed in the existing DCP adapter: it binds
only the first display `iommus` entry, and its DMA free paths disregard TLB
invalidate failure. A second output requires shared framebuffer IOVAs in
both display DARTs and retained backing on failed unmap/flush, along with
real PHY/crossbar/Type-C providers and the external RTKit mailbox binding.
No HPD/training/EDID/mode/second-output or TS3 tunnel success is claimed.

### USB03 captured boot: lifecycle service attached, 2026-11-15 03:06Z

USB03 capsule `7ae7d4240745c5450886ce40adbdb276572a3313a6fab253e7ae86131c0913f6`
from KC `/nix/store/sh3yw7qb1zh3ixwnx8xkpvxppka3iqrb-puredarwin-kc-arm64-0.1`
booted after DESK4's release and PERF's guarded libobjc install/checks. The
cycle held gui.lock and bench, installed through the validated Linux script,
used one-time boot, captured UART before first boot and stopped/reaped its
owned reader before lock release at 03:06:08Z. Darwin banner was captured;
no panic marker was found. The .next/inject desktop returned.

Both PDAppleTypeC and PDAppleUSB are now registered/matched on real port 1.
The unchanged TS3-only cable state reports USB role none, host-active No,
alternate-mode-deferred Yes, removal-blocked No and zero sampler errors
(61 samples at capture). The packaging dependency failure from USB02 is
resolved. This establishes driver loading and deferred-mode behavior only:
there was no direct USB device, no ATC/DWC3 host initialization or controller
DMA, and no enumeration/HID/storage/DHCP/hotplug proof. The first direct
plug remains a captured risky test to coordinate through the status file.
The newer six-mode/calibration source work after this boot is not installed.

### Six-mode ATC source and runtime calibration, 2026-11-15 03:16Z

The ATC backend now ports USB2, USB3, USB3+DP, TBT3, USB4 and DP mode
programs in both orientations. Runtime ADT calibration grouping follows
m1n1: USB groups on the selected PMA plus DP TX on the other, both DP TX
groups for DP, and both sets of CIO groups for TBT/USB4. Entire bundles
are preflighted before PHY power writes and revalidated before applying;
calibration failure blocks lane routing and reset release. No calibration
payload is committed. TBT3 keeps PIPE dummy, distinct from USB4's backend.

ASan/UBSan passes the twelve mode/orientation programs, ten calibration
bundles, 56 missing properties, changed-program/late-malformed refusals,
and existing power/DP cases. SDK object output is
`/nix/store/71bbh5x17137bx12ks9k4vk0wcbsmb01-puredarwin-atc-objects-0.1`;
the existing USB2 native bundle still builds with the shared power program
(`/nix/store/4ksp2ljj2fv153asgk0xa7zdyjl52cy6-puredarwin-apple-usb-host-0.1`).
All six repository checks pass (274 Python tests, 77 C groups). USB03
remains installed; native lane-calibration selection and safe USB3 PIPE
routing are the next attachment work. This is source/SDK evidence only.

### Direct USB3 native attachment prepared, 2026-11-15 03:22Z

The USB adapter now selects USB3 from the Type-C snapshot, copies and
retains its runtime calibration before PMGR/PHY writes, programs the
orientation, initializes DWC3's host role and routes PIPE before publishing
xHCI. USB2/USB3 changes drain the previous session. T8103 skips host BIST
as the qualified Linux workaround does. Shutdown uses stopped parking
only after the existing controller drain and synchronous child termination.
Failed handoff completion retains PHY ownership. The child iterator is
released after retaining its client, avoiding a reference leak each removal.

Host tests pass 100 PIPE cycles, live lock retries and field restoration,
clockless stopped parking and failed-completion retry, plus all existing
USB DMA/lifetime and ATC tests. The native bundle and USB04 collection build;
USB04 KC is `/nix/store/1z98228ka748b3ak671wadlwbrskhsnh-puredarwin-kc-arm64-0.1`,
capsule SHA256 `03d39b8176db0069c397e2d9f979a087f853b25626c4aadec408148c26f7febf`.
Its unchanged boot.bin hash is `81d58f1b40e8139dc797ceac8314bb74a6308ccf18b28e42f1c902c7b4912e3e`;
all 11 kit checks and six repository checks pass. Installation is announced
for after PERF's measurement (no earlier 03:31Z), subject to a fresh log
check inside gui.lock. USB03 remains installed at this checkpoint; neither
controller DMA nor direct-device enumeration has been observed.

### ACIO tunables, CM core and native RTKit source, 2026-11-15 03:28Z

ACIO's runtime decoder now validates the actual 24-byte records and all
eleven RC/NHI/PCIe-adapter groups before any write. It preserves Linux's
independent clear/OR-set semantics and skips unchanged registers. ASan/UBSan
passes 110 late missing/malformed refusals and all eleven base mappings;
SDK objects are `/nix/store/qkqxcpbyg4npxmfqn7caial2hvihzjbp-puredarwin-thunderbolt-objects-0.1`.
No firmware/calibration/device-identity bytes are committed.

The actual pinned Linux CM core (ten control/path/tunnel translation units)
SDK-compiles against LinuxKPI, with endian-array/CRC32C wire helpers and
tracepoint instrumentation disabled in the staged copy. Output:
`/nix/store/mk0abkh7l44yl8alnnn2pwx3hy7akzy7-puredarwin-thunderbolt-cm-objects-0.1`.
This is not linked or attached. Full-CM compile attempts remain failures:
LinuxKPI router bus/device/PM/uevent/property/crypto/cleanup APIs still need
native implementation. Logs remain private; no missing backend is replaced
with a success stub. The known wire vector and 514 array cases pass.

Native RTKit platform initialization now serializes allocation/publication
and retains its host anchor; failed allocation cleans up and permits retry.
The resolver supports ACIO1's M3v2 mailbox directly at reg0 (no ASC 0x8000
offset), while preserving ASCv4 for internal/external DCP. Port0 M3 remains
refused. Layout tests pass 48 IRQ permutations, every undersized window,
overflow and malformed IRQs. The complete PDDcp bundle SDK-builds as
`/nix/store/6wjx0zwiv0h2nbcf2cp05yw0nz8h5vfv-puredarwin-dcp-0.1`.
These newer RTKit/DCP changes are not part of USB04 and establish no M3
boot, external DCP, trained link or scanout. DESK4's K1-K12 interface request
has been read and answered; actual connector evidence must precede its
second scanout integration request.

### USB04 captured boot, 2026-11-15 03:34Z

The frozen USB04 capsule `03d39b8176db0069c397e2d9f979a087f853b25626c4aadec408148c26f7febf`
was installed after PERF finished and TERM6 released its hold; the log was
re-read inside gui.lock before the cycle. Linux normal installer and one-time
boot were used, with UART active before the new kernel's first boot. It
returned to SiliconForest and .next/inject by 03:34:02Z. The owned reader
was stopped/reaped at 03:34:03Z before lock release. UART records the Darwin
banner and no panic(cpu) report. The pre-existing read-only fsck findings
match the documented 21:06Z remnants (inode 65144, blocks 636291–636295,
group 17 and total free-count delta 3); no filesystem repair was performed.

PDAppleUSB is registered/matched and reports `super-speed-capable=Yes`.
The unchanged TS3-only cable remains deferred, host-active No, removal-blocked
No; Type-C sampler 61 samples and zero errors at capture. This proves new
bundle loading/deferred behavior only. No ATC USB host initialization,
controller DMA, enumeration/HID/storage/DHCP/ten cycles, NHI/tunnels or
external output has been observed. The separate RTKit/hotplug source after
this collection build is not installed by USB04.

### Darwin hotplug observer source, 2026-11-15 03:37Z

The DCP hotplug worker now queues a dedicated native cache refresh after
real upstream notifications, including route removal. It refreshes modes,
then publishes cached connector status/name/preferred mode and a monotonic
read-only `kern.pd_dcp.hotplug_seq`; `kern.pd_dcp.connectors` serves the
snapshot. Sysctl reads hold only a short metadata mutex. No event, EDID or
connector is fabricated. DRM ioctl latency remains subject to DRM locks
and must be measured on hardware; no 50 ms guarantee is claimed. The
complete DCP bundle SDK-builds; this code is not in installed USB04. Native
hotplug/connector/scanout evidence remains outstanding.

### DCP DMA lifetime prerequisite, 2026-11-15 03:46Z

The DART backend now provides checked retirement with preflight table
walks and real invalidate error propagation. An unreachable table or
failed invalidate poisons its stream; any poisoned DCP/PIODMA/display
stream closes allocation/map/IOVA-reclaim across the backend because they
can share backing. Caller producer/in-flight quiescence remains separate.
Owned allocations retain pages/views/IOVA/device refs on failed cleanup.
PRIME retains exporter DMA-buffer refs and pins on failed import cleanup
or destruction, including failures before the import GEM exists. The
physical-page unmap API now returns failure instead of silently freeing.

The native template tracks PIODMA ownership on T8103's older firmware as
well as later firmware. Failed first-segment rollback is flushed too.
Declared-but-unavailable IOMMUs fail closed instead of falling back to
physical DMA, and live bindings cannot change streams. Generic SG use in
this driver is backed by owned DCP allocations; future arbitrary borrowed
callers still need their own exporter lifetime contract.

ASan/UBSan passes existing table checks and 512 retirement cases including
timeout/poison retry, unreachable tables and invalid ranges. Full PDDcp SDK
output: `/nix/store/fakwnsdwpmqgh4iqfmgs1bcyyav9abma-puredarwin-dcp-0.1`.
This newer source is not installed in USB04. Mirrored framebuffer IOVAs in
both display DARTs, external topology, native PHY/mux/Type-C and actual
external DCP/link/scanout are still required.

### Mirrored framebuffer DMA source, 2026-11-15 03:54Z

The display DMA adapter now consumes one or two complete T8103 IOMMU
specs, reserves the same IOVA in both native bitmaps under one lock, and
maps/flushes/unmaps the full domain for coherent, physical-page, SG and
resource paths. It publishes only after both invalidates succeed. A peer
map failure preserves a pre-existing firmware PTE; outer allocation
cleanup clears only successfully mapped pages. Either stream's retirement
failure preserves all backing and both address reservations. PRIME's
translation check compares both banks. Capacity covers four DART blocks
and eight streams; no TTBR or firmware IOVA is invented.

ASan/UBSan passes 524,288 paired and 2,048 single-bitmap cases, actual table
pairs, foreign-PTE-preserving rollback and both-stream map/flush/retire
failure cases, plus the earlier table/512 retirement cases. Full SDK output:
`/nix/store/7dcmfyxzsdzvgmam6w5ai5x1zmms35z6-puredarwin-dcp-0.1`.
All declared DART streams must be valid/powered and retain their tables
while shared buffers exist. These are source/SDK checks, not live external
DMA. The current tree still publishes only the internal display path;
external topology, real PHY/mux/Type-C providers and external DCP/link/
scanout remain next work. USB04 remains installed.

The initial mirrored-binding parser was too narrow (SID-only); source
review caught the existing display builder's five argument cells before
installation. It now accepts the actual one/five-cell forms and consumer
aperture override, checks every property cell to reject malformed tails,
and refuses unsupported windows rather than allocating beyond them. A
host integration test passes all three bindings emitted by the actual
internal display builder. The corrected SDK attempt also exposed a missing
LinuxKPI u64-array reader; the final adapter uses its real u32-array reader
and combines the pairs. The failed build receipt remains a failure.

### T8103 DP/DP-IN crossbar source, 2026-11-15 04:09Z

`m1-dptx` now ports the upstream T8103 crossbar register bodies for the
physical DP PHY and both Thunderbolt DP-IN outputs. The reproducible
extractor retains source provenance/notices and T8103's two-input and
cycle-slip policy. The program includes duplicate-input refusal before
writes, DP-IN preselection, link clock stop/start and disconnect with
unrelated fields preserved. The caller still owns native power, mux lease,
shared ATC/crossbar serialization and prior-user drain; init proves no
hardware idle fact. Gate status remains diagnostic, matching upstream.

ASan/UBSan passes 192 PHY/DP-IN select/link/disconnect cycles and conflict,
preselection, shared-field, gate-order and stuck-status cases. SDK objects:
`/nix/store/iz2ln1z80d4krl854krjrlfzshavyafn-puredarwin-dcpext-plan-objects-0.1`.
Six repository checks pass. No crossbar writes, HPD, link training, NHI
traffic or scanout have run from this source on M1. USB04 stays installed;
external topology/provider and router-device integration continue.

### Router device/reference adapter, 2026-11-15 04:33Z

`m1-thunderbolt` now SDK-compiles nineteen upstream manager files plus two
native registry/device adapter objects. The recipe asserts the full/core
object count (21/12), presence/absence of `domain.c.o`, and emits defined,
undefined and remaining external symbol manifests. An evidence correction:
initial full attempts through `tb-cm-full-08` only compiled the ten core
files because the inherited configure phase ignored `cmakeFlags`. These
receipts are not full-manager proof. Passing the option in the actual
configure command exposed additional errors; the failed receipts remain.
The corrected full native SDK build is
`/nix/store/yma5kcgm68n6vpb0l03xk60lacynqzr5-puredarwin-thunderbolt-cm-objects-0.1`;
core-only is `/nix/store/fspxvd6p2dmrhrg1ah2jczjfzr27m2yf-puredarwin-thunderbolt-cm-objects-0.1`.

The native adapter supplies real metadata operations and reference-safe
bus/child lookup, publication rollback, type release and bounded events.
An iterator pins both child and parent; each child's separate parent
reference lasts until final release. Structural changes require native
owner serialization. Runtime-PM calls bypass the SDK's success/no-op
helpers: actual native callbacks are mandatory before domain init, errors
block new power/publication operations, and teardown traversal remains
possible. OS information also requires a real native snapshot. Optional
XDomain service-driver registration fails unsupported rather than reporting
an unimplemented binding. Private staged LinuxKPI layout additions retain
notices. The x86 Intel Apple NHI resume-link function is excluded on ARM;
protocol/control bodies and runtime callback pointers remain intact.

ASan/UBSan passes the production registry and device adapter using host
reference/lock/publication fixtures: 100 self-removing trees with final
parent lifetime, eight concurrent iterators, 1000 remove/re-add cycles,
capacity/overflow/reference failures, watermark/reverse lookups, rollback,
event bounds and native PM failure followed by cleanup. Logic/registry SDK:
`/nix/store/wrjjx0s261vvxj2pc6kkz315qrxf7vvi-puredarwin-thunderbolt-objects-0.1`.
All six repository checks pass (274 Python, 77 C groups, six mutations).

This is not a linked/attached CM or full dependency closure. Native IORegistry/
PM callbacks, USB4 port-device/quirks sources, crypto/UUID bindings, Apple
NHI control DMA/interrupts, ACIO M3/RTKit/DART and PCIe-C remain. No ACIO
writes or tunnels have run. USB04 is still the installed capsule and TS3
remains deferred; no hardware milestone is marked complete. Continuing
native attachment and external provider work without another reboot yet.

### Manager source dependencies and crypto, 2026-11-15 04:43Z

The pinned source set now includes USB4 port devices, dock quirks, SHA224/
256/HMAC, constant-time comparison, UUID/GUID helpers and Linux component
handling. The full SDK target verifies 27 objects (21 manager, four library,
two native), and no manager/USB4/crypto/UUID functions remain unresolved
between those objects. Kernel services and four transactional devres group
APIs remain required. In particular, the existing DCP group compatibility
functions report success without rolling back failed-bind resources; they
are not used as implementations here. No native aggregate binding or full
kext/link closure is claimed.

Full object build:
`/nix/store/ksda3m9pgimr6chc4600i8b6xnrimfjc-puredarwin-thunderbolt-cm-objects-0.1`.
Core build: `/nix/store/953fygchfy63lh2rqz7mc4g3hsihrajq-puredarwin-thunderbolt-cm-objects-0.1`.
The byte-exact source/header hashes and original notices are retained;
SDK/host preparation now shares `tools/prepare_cm.py`. It fixes an upstream
USB4 registration error path which otherwise unregisters/frees the device
and continues with PM calls. A host test compiles that actual staged add
function against the production native registry/device adapter: component
failure returns the error, frees once, restores parent references and makes
no later PM calls. Busless USB4 port children and separate wake capability/
wake policy are covered too.

ASan/UBSan also passes 1000 independent Python hashlib/hmac comparisons of
the unchanged C crypto bodies, including SHA224/256, both HMACs, empty and
long keys, padding/block boundaries, unaligned buffers, incremental updates
and context clearing. Comparison tests vary differing positions/lengths/
alignment; UUID tests cover order and malformed text. This is functional
and memory evidence, not timing/FIPS validation or dock authorization.
All six repository checks pass; failed initial compile receipts are kept.

USB04 remains installed, without a new boot or direct device attachment.
The TS3 is still deferred and no NHI/tunnel/external display milestone has
been demonstrated. Continuing resource-group rollback, shared native ATC/
PHY ownership, ACIO and external provider integration.

### Transactional component resources, 2026-11-15 04:49Z

The CM target now provides the four devres group APIs, using the pinned
Linux marker/rollback design on its private BSD LinuxKPI layout and newest-
first list. It retains the BSD file's notices and the Linux design's GPL/
SUSE/Tejun notices. Closed groups release only their bounded interval;
open groups roll back to their marker, in reverse allocation order. Marker
removal keeps resources attached; allocation failure leaves previous
resources alone. Callbacks run after list extraction and outside the lock.
Crossing closes are refused before list changes and fault the native
adapter. This implementation requires proper nesting, the subset used by
component.c. It does not establish DMA/IRQ/producer quiescence: callers
must stop and join those users before resource retirement.

ASan/UBSan tests run the actual group include with a host BSD-layout and
allocation fixture: 500 nested rollback/reverse-order cycles, closed/open
groups, removed markers, failure of each allocation, crossing-close refusal,
full teardown and callbacks which reenter allocation. The native PM suite
also refuses an invalid (>1) suspension-query result and still allows
metadata teardown after fault. All prior host tests and six repository
checks pass (274 Python, 77 C groups, six mutations).

The verified full target has 28 objects, including the same private
LinuxKPI devres unit a future kext layer must compile once. A future kext
must not link a second copy of that runtime. Four group APIs are now absent
from the remaining-external symbol list. Full SDK:
`/nix/store/mjlq420i01yl36dgx9r8gpcv8g5021c1-puredarwin-thunderbolt-cm-objects-0.1`.
Core-only (13 objects):
`/nix/store/c8sxr85bsh596fmaa4c7ws7ik1z53yf9-puredarwin-thunderbolt-cm-objects-0.1`.
This remains an object target, without native ACIO/PHY/PM/backend binding.

Read-only USB04 observation at 04:46:51Z still shows TS3-only Thunderbolt,
4422 samples, zero Type-C read errors, host inactive, alternate-mode deferred
and removal not blocked. No direct USB2/3 or DP flag is present. No new boot,
controller DMA, NHI traffic or external output is claimed. Continuing with
shared native ATC/PHY ownership and ACIO/external provider integration.


## Dock-first replanning and idle comparison (15 November, 05:44Z)

The acceptance order is now dock PCI functions, I210 link/DHCP, dock PCI
xHCI with physical keyboard/read-only test media, Samsung DP tunnel, then
ten dock-cable cycles. The existing em personality includes I210, and PCI
Ravyn xHCI exists; both still need PCIe-C DMA/MSI and lifetime auditing.
No dock function has yet enumerated in Darwin. Direct-host sources remain
useful for shared PHY ownership, but require no further direct-only boots.

One announced, UART-captured, gui.lock/bench-controlled k2/USB04 A/B restored
the exact USB04 kernel through the normal installer. Relevant userland hashes
were identical across the two boots. The original 120-second k2 probe hit
its 120-second wrapper timeout: that full run FAILED. Its recovered first
100 seconds are useful partial evidence; the restored USB04 90-second probe
completed. Kernel_task used 2.42% of one CPU on k2 and 1.94% on USB04.
The dominant thread was the same preexisting PDAppleNVMeRW polling worker
(1.83% versus 1.28%). Stackshot symbolication identified its timed lock sleep;
NVMe and all kernel modules except the USB/Type-C additions have identical
module UUIDs. No new NHI/controller DMA was running. Aggregate idle was
97.87%/98.26%; load near one also existed on k2. A governor-period test did
not remove the NVMe cost; its original setting was restored.

Warm fixed-work timing was 83.7 ms on both kernels; 100,000 getpid syscalls
took 14.66 ms on both. The reported AppKit 1.5–2x slowdown was not reproduced
by those probes and its cause is unresolved. NVMe's unchanged worker explains
the largest measured kernel idle cost, not that reported application slowdown.
No NVMe code, write gate or fence was changed. Avoid claiming a regression fix.

A separate read-only 20-second compositor sample found pdhid-reader CPU
0.361% of one core, main 3.496%, and Shm dropping thread 1.951%. Whole-task
interrupt wakeups were 8,819 (441/s), platform-idle wakeups 6,650; timer bins
2,565 and 4,218 are reported separately, not attributed solely to HID.
DESK5 confirms 1 ms active/2 ms intermediate/4 ms quiet HID polling because
the queue nodes have eno_select. This is a wakeup reduction opportunity;
it does not establish the AppKit slowdown's cause. DESK owns that input loop.
Its SIGSTOP finding is specific: minimal threaded/poll/read tests stop on
USB04, but the live compositor continues accumulating CPU after STOP.
The broader claim that all SiliconForest processes ignore STOP is unsupported.

Source hygiene removes the USB lifecycle's redundant 100 ms snapshot poll:
Type-C publishes a property-change message, the consumer queues a one-shot
on its own workloop, and stop closes admission before cancelling the timer.
The Type-C producer retains its slow 1 Hz observation. Both actual SDK kext
builds pass. This change is uninstalled and is not a claimed cause/fix; fold
it into the next dock kernel rather than spend a direct-only boot.


## Shared native PHY ownership for the dock (source only, 05:50Z)

The resumed broker work is now integrated with event-driven snapshot delivery.
An IOCommandGate serializes native TBT/USB4/DP claims and one-shot observation;
revoke callbacks run on a separate thread call outside the PHY gate. A changed
cable retains the owner's configured PHY until actual client drain and PHY stop
succeed. Token/epoch overflow closes admission; failed hardware cleanup keeps
the owner quarantined. No native NHI client is attached yet.

A checked, read-only PMGR extension rejects an unknown, active, clock-gated or
automatically waking ACIO1/M3 before PHY clocks/mux are changed. DWC3's stopped
view alone was insufficient to establish that prior ownership. The helper is
staged privately, without changing sound's pd_soc.cpp. Separate error properties
accompany aggregate ACIO and CPU gate values; zero raw data on an error is not
off evidence. A poisoned prior USB DART also prevents alternate-mode admission.
DP AUX partial-start cleanup and the upstream zero-rate no-op are preserved.

The native host bundle SDK-builds; ASan/UBSan covers 10,000 lease transitions,
actual PMGR refusal/parser paths and all existing ATC/DWC3/DMA lifetime suites.
This is not installed and proves no dock operation. Next is the native ACIO
RTKit/NHI attachment, its SID1 DART/interrupts and real CM/root-router discovery;
then apciec1 brings the dock's PCI endpoints into IOPCIFamily.


## Native ACIO1 service candidate (06:06Z, execution pending)

PDAppleCIO now links as a native kext. With `pdcio=1`, it consumes the shared
TBT/USB4 lease, refuses a bound apciec1 host/DART, resets that PCIe-C block,
powers ACIO and boots M3 using the existing M3v2 RTKit transport. It uses
runtime immutable tunables and only the validated ADT SRAM __DATA window.
It refuses RTKit's polling mode. One attempted startup per cable epoch and
producer epoch-change notifications avoid a retry/event-query busy loop.
RTKit/power-off errors preserve the PHY and resources rather than acknowledging
a false drain. Actual native PMGR-extension fault/timeout tests and SDK build
pass. Two early packaging builds failed (permissions/header staging); corrected
builds pass. No target execution is asserted in this paragraph.

The first collection candidate reuses USB04's frozen inputs, replacing only
Type-C and RavynXHCIPort and adding PDAppleCIO. UUID comparison preserves all
other modules, including internal DCP, GPU, NVMe and Wi-Fi; boot.bin reproduces
byte for byte. Common LinuxKPI weak USB imports remain unreachable in this
non-USB service, like the existing DCP/AGX copies: pd_fbsd_init only initializes
the required taskqueues and does not call the USB attachment helpers.

The firmware-stage properties deliberately distinguish RTKit-ready from
NHI-ready (false), domain/router/authorization and dock PCI enumeration.
A separate single-DART/SID1 native NHI DMA adapter compiles but is not activated
in this stage. Its root must be reprogrammed after ACIO power cycles; suspend
refuses outstanding mappings or a failed invalidation. NHI interrupts and
connection-manager attachment are the following implementation step, followed
by PCIe-C enumeration and the owner's prepared dock keyboard/LAN connection.


### CIO01 target observation (06:11Z): preflight failure, no RTKit start

CIO01 was installed normally and booted with internal desktop/Wi-Fi and the
standard .next/inject state restored. UART reader was stopped/reaped before
gui.lock release. Capsule SHA-256:
`c28cdae9f5fa019e77b09d03167f24c3a99ffedd616bc327f5fa8450a433e87d`.
PDAppleCIO matched, but returned ENODEV (19) before acquiring PHY; phy-owned
and quarantined are No, RTKit-ready/NHI-ready are No. This startup FAILED;
no M3/host-router/dock PCI progress is claimed. No panic was observed.

The CPU-compatible preflight used IODTMatchNubWithKeys, which calls the bare
node's name comparison rather than directly comparing its compatible data.
The CPU node is named acio-cpu1. Source now uses IODTCompareNubName and exposes
the current resource stage; this correction is not yet target-tested. Initial
ACIO gate state was not reached by that failed startup and remains unobserved.

Installed producer/consumer evidence: Type-C samples 61, snapshot checks 62;
consumer is event-driven, has one initial check, then follows the 1 Hz producer.
TBT-only state, zero Type-C errors, no active direct host or PHY lease persist.
The separate native NHI/DART interrupt resolver (24 actual ring IRQs + DART)
SDK-builds using the existing masked-AIC engine; not installed or registered.


### Preflight correction from actual ADT metadata (06:24Z; not installed)

Read-only IOKit XML verifies the M3 compatible bytes (19 including NUL),
__TEXT;__DATA names (14), SRAM metadata (64), and actual PMGR table. ACIO1's
power-gates are physical ATC1_CIO / CIO_PCIE / CIO_USB; the clock-gates list
also contains virtual CIO1_RECONFIG-V, with no parents or state register.
The prior source's rejection of all virtual clock gates would therefore
refuse every claim even after the name check was fixed. No such gate read
was reached in CIO01. The corrected off check uses all three physical power
gates at the qualified T8103 addresses; ATC1_CIO covers M3. It never treats a
virtual reset vote as an off reading. Reverse shutdown follows the same
physical list and checks every mapping before any write. PCIe-C reset targets
physical ATC1_PCIE, not either virtual clock vote or the shared AON parent.

Host tests execute those corrected native extensions, require power-gates
rather than clock-gates, and reject unknown/active/all-ones state. The native
client also keeps a lease token returned on a failed PHY setup, so cleanup
cannot lose an owner when the PHY provider retained a partial session.
CIO02 is the next announced dock startup boot. NHI DMA remains disabled in
this firmware-stage test; no direct USB/adapter boot is planned.


### CIO02 target FAILED; UART diagnosis and USB04 recovery (06:35Z)

CIO02 capsule:
`4433cfc7328b7e585a7150927962d89f030cc59a06298e5ca0042ca0d97e0af9`.
The boot panicked at 06:30:09 with ESR alignment fault. Symbolication of its
exact collection resolves kernel memmove+0xa4, caller apple_rtkit_memcpy+0x160,
then apple_rtkit_syslog_rx_log and the RTKit RX workqueue. The I/O source is
four modulo eight, copying a 16-byte SYSLOG context twelve bytes into SRAM.
LinuxKPI defines memcpy_fromio as ordinary memcpy, which issued an unaligned
device-memory access. This is a real native ACIO bug, not an NHI/DART fault.

The trace establishes progression through PHY/power startup into actual M3
SYSLOG reception. It does not establish completed RTKit-ready state, NHI up,
host-router discovery, any tunnel or dock PCI function. No such result is
claimed. The owned cycle was interrupted to recover promptly; M5 reset and
the normal installer restored USB04. Standard .next/inject desktop/Wi-Fi were
confirmed at 06:35:15; UART reader was stopped/reaped before locks released at
06:35:17. The main CIO02 run remains FAILED; recovery passed. Rollback Type-C
reads still report no errors and direct host inactive/alternate-mode deferred.
The event-driven consumer and broker are therefore uninstalled again.

The source fix ports Linux's aligned I/O-copy sequence, preserving the Kalray
GPL-2.0-only notice/provenance. Only normal RAM is passed to libc; all I/O
reads are byte or aligned 64-bit. ASan/UBSan instruments 263,168 alignment/
length combinations and the exact fault shape; ARM64 codegen confirms the
access widths. Native SDK/collection builds pass; target retry is pending.
Other USB04 modules remain frozen for that retry.

TERM6 separately reports an application-side idle CPU/RSS regression caused
by WindowServer patch 0124 discarding frames and Activity Monitor recreating
8 MB buffers. It installed correction 0129 at 06:26. That reported finding
is separate from the unchanged NVMe worker measured in the kernel A/B; it is
not a claim that our CPU-only probes established the AppKit timing cause or
that a USB kernel change fixed those application timings.


### Dock-first source progress and install transport (07:11Z)

CIO03 and CIO03B did not reach installation: the former's Linux upload was
refused; the latter's clean Linux transition did not answer. CIO03B captured
m1n1 through the next-stage handoff, without a captured panic. The external
reset recovery reached Linux and the normal installer confirmed unchanged
USB04; SiliconForest desktop returned at 07:04:24 and the owned reader was
reaped before lock release. This does not test the SRAM-copy fix. CIO03C is
retrying the same frozen capsule with one bounded external-reset fallback;
new source changes below are excluded from that capsule. Transport cause
remains unestablished; no Linux SSH/firewall/boot configuration was changed.

The private CM coherent/pool DMA adapter now requires real native allocation
and checked DART retirement callbacks. It accepts only ACIO1 SID1's qualified
IOVA window, rejects unsupported masks before allocation, zeroes payloads,
and retains failed/malformed mappings. Fault injection and eight concurrent
allocation threads pass under ASan/UBSan. Native PDCIODMABuffer has explicit
retire(), leaving its descriptor/backing owned when invalidation fails. The
DMA inventory is not a controller/IRQ/worker drain proof.

The Type-C source reads packed Intel VID status 0x59 for TBT, publishing its
raw device/enter VDO with status/data in one prefix-compatible immutable
snapshot. Missing cable fields refuse a TBT claim. Cable speed/active/link
training/generation/legacy changes revoke the shared lease. The current TS3
status implies 0x411 before the as-yet-unobserved legacy-adapter bit, rather
than guessing the cable value from USB flags. 128 protocol combinations,
invalid-state and v1/v2 parsing checks plus native Type-C/USB SDK builds pass.
This observer still runs at 1 Hz; no new periodic timer is added.

The native NHI bridge now SDK-compiles with the actual Linux ring engine and
software manager (30 full-target objects). It uses Apple's ring/PDF layout,
AIC callbacks, W1C status and throttle registers, and explicit native DMA.
It avoids generic Intel reset/interrupt setup, requires 12 pristine rings,
valid host DROM and Apple's VSE capability, unlocks ports and programs the
supplied cable value. PCIe/DP activation explicitly refuses until the actual
host backends exist. Private ring teardown checks VALID/IRQ readback before
clearing addresses, joins IRQ and worker before retirement, and quarantines
all mappings on failure. Domain removal owns its release; no second put is
issued. Native IORegistry/PM provider and combined kext linkage remain work
in progress; these compiled bridge objects are not an installed NHI or dock.


### CIO03D observed firmware-ready; next native CM build (07:34Z)

The unchanged CIO03 capsule was finally installed in CIO03D. Normal Linux
transition/upload/install succeeded without preinstall UART routing; capture
started after the install checkpoint. This supports avoiding the earlier
routing loop but does not prove its role in the refused/unreachable attempts.
The owner now separately authorizes removal of Linux SSH lock-outs at the
next normal visit, with no extra reboot; findings/changes will be in SF-R.

At 07:26:09 the M1 reports RTKit ready, firmware state 0x01000000, twelve NHI
rings and interrupt-enable zero, with stage error zero, no quarantine and
owned TBT PHY. The event-driven snapshot consumer made 62 checks for 61
Type-C samples. Standard desktop/Wi-Fi/inject are present; the owned UART
reader was stopped before releasing locks. This is actual M3 firmware-stage
success and hardware qualification of the SRAM-copy correction. It is not
NHI DMA, host/dock-router enumeration, authorization or a tunnel.

A 60-second read-only probe gives printed non-idle kernel-thread deltas of
2.70% of one core and aggregate CPU idle 97.83%. The dominant 2.08% worker
is symbolicated against unchanged module UUIDs as PDAppleNVMeRW's timed
lock sleep, the same worker identified in k2/USB04. Warm fixed work remains
83.7 ms and 100k syscalls 14.5 ms. The printed-thread threshold omits tiny
individual deltas; this is not a precise all-thread task CPU total. Load
average still around 1.1; the original AppKit timing regression is not claimed
resolved, and NVMe gates/fences were not changed.

The next source target links native IORegistry publication/final release,
actual runtime OS sysctls, native AIC/DART fault handling, and coherent DMA
callbacks into PDAppleCIO. Its cable-held power policy tracks usage/disable
state and records autosuspend hints without reporting hardware suspend.
The production policy passes failed-XDomain cleanup, 10,000 balanced router
sequences, suspend refusal, under/overflow and wake-policy host tests.
Raw ADT DROM bytes are copied at runtime into a private OF cache to bypass
LinuxKPI's numeric endian restoration; no DROM or identity is shipped.

The larger collection first exceeded the existing KC reservation. Only this
CM variant now dead-strips unreachable functions, with every BSD linker-set
member explicitly protected. Binary comparison retains all 29 SYSINIT and 11
SYSUNINIT targets in order, seven C++ initializers and the native metaclasses.
The collection fits its original address range and passes the overlap check;
boot.bin is unchanged. Generic compatibility UUID constants are privately
namespaced to avoid a duplicate symbol. The firmware-only build interface
is retained. Native CM is an explicit pdcio_cm=1 opt-in and has not run on M1.


### CIO04CM loader rejection; no NHI execution (07:47Z)

The first combined-driver capsule was installed, but native stage 2 rejected
its 619-byte command line before XNU: revision 2 permits fewer than 608 bytes.
This is a capsule-preflight mistake, not an NHI/DART result or kernel panic.
The owned cycle was interrupted promptly for guarded CIO03 recovery. Removing
only redundant pdrtkit_irq=2 (already the required native default) gives 605
bytes. The corrected capsule has exactly the same compressed collection and
boot.bin; the wrapper now checks the native limit before taking a test slot.
No NHI or dock operation is established by either capsule build.

During that normal Linux visit, the owner-authorized SSH lock-out change was
completed and eight rapid key-authenticated sessions passed. UFW remains on,
with both port-22 LIMIT rules replaced by ALLOW. PerSourcePenalties is off;
key-only authentication was explicitly enforced after discovering passwords
had been allowed. Exact before/after values, backup files and revert steps
are in SF-R's reaching-Linux note and the coordination log. No extra reboot
was used for this change; prior 9-second/60-second SSH workarounds are relaxed.


### CIO04CMB setup refusal and OS-info blocker (08:03Z)

The corrected 605-byte capsule booted without panic. Its extended 24-byte
Type-C snapshot is valid/error-free and derives TBT cable-info 0x411. M3
reported firmware ready, but CM creation returned a generic EIO before DART
preparation was logged. Cleanup completed: no retained PHY ownership or
quarantine. The guard restored CIO03 at 08:02:26; UART was reaped before
releasing locks at 08:02:28. NHI rings/CM and dock PCI remain unobserved.

Source inspection finds a definite OS-info blocker: XNU's kern.hostname MIB
lacks CTLFLAG_KERN, so kernel_sysctlbyname rejects that read with EPERM. The
other three OS MIBs permit kernel callers. Native creation previously hid the
underlying return as generic EIO; the target did not expose its precise stage.
The correction reads the actual exported hostname with a bounded copy (no
compatibility-layer homonym), retains real sysctls for the other fields, and
adds creation/start stage+errno and one-time power/ring/IRQ snapshots. No new
periodic poll is introduced. Native build/collection retry is in progress.

PCIe-C survey refinement from native DT plus the pinned Linux DTS: endpoint
DART is 0x521008000; raw ADT names mapper SID15, but this does not
establish Linux's endpoint domain assignment (see live-DT correction below).
It is separate from NHI's 0x501a80000/SID1. MSI base is
818/count32; the root interrupt is 850. Exact resource names come from DTS:
config 0x530000000, RC 0x520000000, port0 0x521000000, fabric 0x520004000,
debug 0x520200000. Linux additionally names LTSSM 0x521010000 and OE-fabric
0x520008000 beyond the five raw ADT entries. Informal PHY/port labels from the
earlier survey must not be used to map these windows. The pinned
`t8103-pciec-tunables.dtsi` supplies the additional OE-fabric configuration;
no captured calibration values are being added to source. Native endpoint
bridge/DMA and PCI xHCI/I210 binding are still unimplemented.


The pre-retry startup audit also catches the missing NHI tunable application:
pinned apple_nhi_probe applies apple,tunable-nhi before rings. The native
firmware stage loaded/validated those eight fabric/management groups but
intentionally did not apply them. The CM entry now requires HOP_COUNT 12,
IRQ enable zero and every ring VALID clear, then applies PD_CIO_NHI before
DART/rings/manager startup. This remains a source correction pending target
qualification, with no new timer and no effect on the firmware-only build.


Native failure closure is also wired through to actual DMA admission: a
faulted device/PM adapter now makes the native owner unavailable, rejects
new Linux ring enqueues before descriptor writes, and masks/quarantines a
ring that interrupts after the latch. Metadata teardown remains available;
a failed controller drain retains backing and the PHY lease. This closes a
source gap between the previously tested metadata fault latch and the new
native ring engine; it is pending hardware qualification with that engine.

### CIO05 hardware result (15 November, 08:23–24Z)

Capsule `6768dfa61566…`, source `7109a8243` + `6d50a5fe6`, boots the
internal desktop and passes actual native connection-manager creation
(`cm-create-stage=complete`, errno 0). All three CIO power gates read `0x3ff`;
NHI fabric tuning and DART preparation complete, and the control TX/RX
interrupts register. Domain startup then returns errno 12 alongside DART
fault `0x8f0f0001`. The pinned Linux decoder identifies SID15/no-TTBR; the
NHI device tree assigns SID1. This is evidence of a failed hardware DMA
startup, not dock enumeration. Fault admission closes, NHI readiness stays
false, and the owner retains PHY/power in quarantine. The guarded cycle
restored the known firmware-only CIO03 at 08:26:52Z; its UART reader was
reaped at 08:26:54Z. Next work captures DART state and
fault address and compares Linux before changing stream ownership.

The Linux SSH lock-out removal also persisted at this normal Linux visit;
see the exact before/after and rollback in SF-R. No further SSH policy edits
were made.

The follow-up diagnostic capsule `d3aeebc0f120…` adds bounded before/after/fault
DART register snapshots and control-ring DMA/configuration evidence. It changes
no stream assignments and adds no timer. The native SDK/collection build has
zero overlaps, the other 56 module UUIDs are unchanged from CIO05, boot.bin is
identical to CIO03, and its 605-byte command line fits the native 608-byte
capacity. All six required repository checks pass (274 Python tests, 77 C
groups, six mutation checks). Hardware outcome remains pending.

### PCIe-C port sequence, source only

`m1-thunderbolt/src/pd_pciec_port.c` now adapts the pinned Linux T8103 cold
reset table, all 64 RID2SID clears with readback, debug/fabric/port/OE/root
tuning order, bounded RUN/link-idle waits, and post-tunnel reset release and
LTSSM/link-up wait. Its access callbacks must enforce live ownership and
ordered nonposted root mappings; endpoint BAR mappings must be posted on
T8103. Failed accesses stop immediately, stale requester entries fail closed,
and timeout tests cover RUN, busy, tunnel reset and link-up under ASan/UBSan.
These are host register-model results. The source is not in the installed
kernel, has no IOPCIFamily bridge or SID15 endpoint DMA mapper, and does not
enumerate a dock. Cable reprobe/removal and ten-cycle evidence remain open.

### Live Linux PCIe-C topology correction (08:33Z)

The same normal install visit captured the public live Linux properties for
`pcie@530000000`: bus range 0..127, seven resource names
`config,rc,port0,fabric,debug,ltssm,oe-fabric`, requester mask `0x3f`, and
`iommu-map` mapping requester IDs 0..63 onto DART stream IDs 0..63. This
corrects the earlier inference from the native ADT mapper's SID15: Linux
does not assign all dock endpoints to SID15. The pinned `apple,t8103-usb4-dart`
variant supports 64 streams and places TTBRs at `0x400`, unlike NHI's
16-stream DART with TTBRs at `0x200`. The endpoint mapper must account for
that variant and the native bridge's actual requester assignments; the NHI
mapper cannot simply be reused. Linux still reports the authorized CalDigit
TS3 Plus as generation 3. Native endpoint enumeration remains absent.

The optional Linux MMIO snapshot was skipped because the NHI's runtime-PM
status did not establish active power; no unqualified MMIO reads occurred.
The public live-DT snapshot was saved privately.

### CIO06 diagnostic result and full-bank reset correction

CIO06 repeats the failure with no panic. Before preparation, every one of
the NHI DART's 16 TCRs reads `0x00080080` and every root is zero. The old
adapter changes only SID1 to its new root/TCR, leaving SID15 translating
with no root. The first fault is again `0x8f0f0001`, at VA `0x00100100`,
before the control-ring configuration diagnostic is reached. Native fault
admission closes and NHI readiness remains false. CIO03 was restored at
08:37:05Z, and the owned UART reader was reaped at 08:37:07Z.

The source correction follows the pinned Linux full unlocked-bank reset:
disable all 16 streams, clear all 64 TTBRs, enable streams globally with
isolation controlled by TCR, acknowledge pending errors, flush all streams,
then attach only SID1's root. It adds readback refusal for stale TCR/root/error
state. Failed reset/invalidation retains the bank and backing through native
stop, even if preparation failed before descriptor allocation. No SID15
mapping, bypass, PCIe-C DART write or polling timer is added. The production
reset helper is exercised by ASan/UBSan register-model tests covering all
access failures, stale SID15/root/error state, and retained TLB timeouts.
Native SDK/collection and all six repository checks pass for the final
correction (274 Python tests, 77 C groups, six mutation detections); the
collection has zero overlaps. Hardware qualification remains pending.

### Endpoint DMA audit before enabling I210

The existing `PDFBSDem` glue includes both `em` and `igb`. FreeBSD iflib
reaches `fbsdkpi/haiku/freebsd_network/bus_dma.cpp`, whose per-device tags
call `pdos_dma_map_phys`. The XNU seam in `fbsdkpi/xnu/pd_fbsd_os.cpp` already
uses `IOMapper::copyMapperForDevice` and `IODMACommand`; absence of a mapper
can still return a physical address. Its `pdos_dma_unmap` currently ignores
the result of `clearMemoryDescriptor(true)` and releases command/descriptor
ownership. Therefore an endpoint mapper alone is not lifetime qualification.
Before I210 binding, the tunnel path must refuse physical fallback and
retain DMA-owned backing on failed retirement, with actual device/IRQ/work
drain before DART or PHY shutdown. An owned bounce path can prevent a stale
translation from targeting recycled caller storage. This remains source
audit and a requirement, not an installed fix or Ethernet observation.

### CIO07 hardware qualification (08:48Z)

The reset correction starts the real software connection manager and host
router on M1 with error0 and quarantine clear. This removes the observed
CIO05/06 failure; no SID15 mapping or bypass was needed. Live Linux register
comparison during the same normal install visit confirms SID1 TCR `0x80`,
all unused TCRs zero, and TX/RX control flags `0xc0000000` with IRQ mask
`0x1001`. Native startup reaches the matching ring/IRQ state. No dock-router
child or PCI endpoint has appeared yet. Next work checks the trained link
and discovery before authorization and PCIe host activation. The guarded
60-second idle profile completes with NHI ready and quarantine clear throughout.
Printed non-idle kernel threads total 1.85% of one core (tiny omitted per-thread
deltas are not counted), aggregate CPU idle averages 98.43%, and warm fixed
work is 83.674–83.676ms for 50M operations and 14.620ms for 100k syscalls.
The dominant unnamed thread254 accounts for 1.24% of one core; previous
baselines identified thread254 as NVMe, but current-boot symbolization is
pending. This does not reproduce the reported AppKit slowdown and does not
claim its cause resolved. Current tool/userland hashes remain with the
private profile, not an assertion that every userland file matched the older
A/B. No added busy loop is evident in this NHI-up measurement.

### Next NHI link observation, source/SDK only

The native CM now has a bounded PHY-capability query through the real NHI
control channel. One separate one-shot timer, one second after successful
CM startup, samples physical port state and cached peer model IDs under the
domain lock and publishes them in `physical-port-state`. It exposes no UID,
DROM bytes or DMA addresses, performs no authorization or port configuration,
and never rearms itself. `link-query-count` records actual invocations; stop
cancels the timer before releasing resources. This distinguishes training/link
state from later router discovery without adding an idle polling loop.

Native SDK and both capacity checks pass, with zero collection overlaps. The
optional-AVE collection changes only CIO's UUID; the other 57 modules remain
identical to the frozen CIO07+AVE candidate. All six repository checks pass.
This diagnostic code is not installed; the active CIO07AVE cycle adds only
the requested lazy INFO module and preserves the qualified CIO07 NHI binary.

The next normal Linux visit observes a Keychron Q1 Pro `3434:0610` at USB
device `1-1`, including a HID boot-keyboard interface (class03/subclass01/
protocol01). The dock's four PCI xHCI endpoints and eight root hubs remain
visible in Linux. No additional owner keyboard action is needed; leave it
attached while the native PCIe path is brought up. This remains Linux
evidence, not Darwin USB enumeration or physical typing.

The native mapping audit confirms the XNU interfaces for PCIe-C ordering:
`kIOMapInhibitCache` selects Device-nGnRnE for device memory, while
`kIOMapPostedWrite` supplies Device-nGnRE. Root controller/configuration
accesses and tunneled endpoint BARs must use their appropriate distinct
settings, consistent with the pinned Linux T8103 warning. No endpoint BAR
has been mapped by this work yet.

The requested additive AVE INFO test used the exact frozen CIO07 collection
plus one diagnostic module, with all 57 existing UUIDs unchanged and zero
overlaps. The kernel and NHI boot passed, but root INFO returned ENODEV19
with flags0, caps0, soc8103 and no segments/identities. This is an early
inventory failure, before TEXT mapping; no DATA, AVE power/MMIO or encoder
was enabled. The older actual DT capture has a 256-byte padded
`firmware-version` property with the first NUL at byte16; the AVE reader's
whole-property-size guard rejects it before copying the bounded string.
The exact current failing property was not instrumented, so this source
blocker was relayed for the media agent to correct and qualify. The guarded
cycle restored NHI-qualified CIO07 at09:03:02Z and reaped its UART reader at
09:03:03Z. The next CIO08 link-diagnostic candidate omits this failed module
and changes only CIO against the qualified baseline.


### CIO08 observed, AVE paused, next PHY/link comparison (09:20Z)

CIO08 is installed and verified on the M1: NHI and the host router are ready,
RTKit is ready, and quarantine is clear. The once-only physical-port query
ran exactly once with error0. Both physical ports1/2 report state7
(UNPLUGGED), with no remote router. Dock authorization, tunneled PCI devices,
Ethernet, USB HID, and external display have **not** been observed on Darwin.
The next step is link training before router authorization and PCIe activation.

The owner paused AVE on2026-11-15. Source and its pause handoff are preserved
in SF-AD; all AVE requests are withdrawn. CIO08 is the known-good non-AVE
collection (capsule `c63179b7b9f2…`): manifest57/noAVE, live service/device-node
absent, no `pdave` argument. Installation verified09:08:35Z, UART reaped09:08:36Z.
No further AVE builds, boots, probes or firmware/kernel-image extraction.
The preceding paragraphs describing the AVE attempt and then-pending CIO08
are historical; these observations supersede their active-state wording.

CIO09 adds only bounded read-only diagnostics to the leased PHY/PIPE and
actual host-router Apple VSE cable word. The existing one-shot startup query
requests them while its owner/token and power proof are live; it never rearms
and makes no mode, rate, authorization or tunnel change. Native SDK/collection
build and all six required repository checks pass, with zero overlaps and
exactly the USB-provider and CIO module UUIDs changed against CIO08; other55
remain fixed and no AVE is included. This candidate is not yet installed.
The normal Linux installation visit will read matching ATC/PIPE offsets and
the active port1 host-router registers, guarded by a bound NHI, an authorized
up TS3 Plus and the actual I210 endpoint. Raw captures remain private.


### CIO09 observed comparison and next Type-C initialization (09:36Z)

CIO09 (`09c6b7c6b`, capsule `4c10751c6847…`) returned to the standard
SiliconForest desktop at09:31:33Z; the UART reader was reaped09:31:34Z.
NHI/RTKit remain ready, quarantine is clear, and the leased PHY query and
NHI link query succeeded exactly once. The collection has57 modules, noAVE
and no `pdave` argument. Both physical lanes still report UNPLUGGED/state7
and no remote router. No downstream dock or endpoint functionality is claimed.

The active Linux TS3/I210 capture and native diagnostic match all ten ATC
core registers, including lane/crossbar, power/reset, lane FSM and PLL clock
controls, and both read back Apple cable word `0x411`. PIPE mux/request/ack
also match. PIPE AON+0x1c differs in bit5, and NONSELECTED+0x20 in bit15.
These differences are observations, not a cause: the pinned Linux code
explicitly leaves dummy-PHY enable unchanged on T8103, and the AON bit is not
identified by that driver. No speculative write to either bit is planned.

An independent missing initialization step is now ported from the pinned
`cd321x_switch_power_state` and command helper: on port1 with APP firmware,
read SYSTEM_POWER_STATE; already-S0 performs no command; otherwise send the
one-byte S0 `SSPS` task and require its completion/result and actual S0
readback before publishing Type-C state. A busy foreign command is refused.
The one-second deadline plus iteration bound, sleeping between reads, and
no periodic retry prevent an idle command loop. Any error prevents PHY/ACIO
admission. Host ASan/UBSan exercises the actual helper, including every I/O
failure and frozen-clock bound. Native SDK/collection and all six repository checks pass, zero overlaps,
only TypeC UUID changed and other56 fixed; this step is not yet installed and is not claimed as the missing
link's cause. The next collection freezes every CIO09 module except TypeC.


### CIO10 S0 hardware observation; explicit late read candidate (09:44Z)

CIO10 is installed/verified09:44:03Z, UART reaped09:44:05Z. The real
CD321x power register starts at7, the bounded SSPS task completes in two
checks, and final readback is S0/0 with error0. NHI/RTKit and the internal
desktop remain healthy, quarantine clear. Both physical ports are still
UNPLUGGED/state7 with no remote router in the startup snapshot; no dock
router or endpoint appeared. Thus S0 initialization is a real missing step
now corrected, not a claim that the remaining link failure is fixed.

The next source/SDK candidate permits one explicit read-only snapshot via
`IORegistryEntrySetCFProperty`, using the actual calling-task administrator
check and existing CIO command gate. Only the one-key boolean
`pd-cio-read-snapshot=true` is accepted. No addresses, register writes,
authorization commands, timer or worker are added; the existing startup
snapshot still runs once, and subsequent reads require an explicit request.
Stop/admission/power/quarantine checks precede hardware access. The native
CLI also has a privilege-drop negative test requiring rejection and an
unchanged query count; a root request must complete and increment the count
exactly once. This will distinguish a later trained link from the early
startup state without ongoing polling. Native driver/tool/collection builds,
zero overlaps and all six repository checks pass; only CIO UUID changes
against CIO10, other56 fixed. Hardware qualification is pending.


The pinned PCIe-C DART probe explicitly skips parameter-register reads because
those reads can raise asynchronous SError during tunnel activation. Native
code must use the fixed T8020 USB4 topology (16KiB pages, IAS32/OAS36,
64 streams, TTBR+0x400, no bypass), the mapped register extent and ordered
nonposted access with completion barriers. The NHI's safe parameter reads
must never be copied onto this separate controller. The 64-stream global
reset also writes enable+0xfc and +0x100, the latter overlapping the TCR0
address in this register layout; its actual active Linux state needs to be
compared before adapting the bank-reset helper. No PCIe-C parameter probe or
DART write has been performed by this stream.


### CIO11 explicit read qualified, active Linux PCIe-C state (09:53Z)

CIO11 returned to the standard desktop09:53:22Z, reader reaped09:53:24Z.
The actual UID65534 request was rejected with NotPrivileged and count1->1;
root succeeded with count1->2. The late physical-port capability reads still
report UNPLUGGED/state7 on both lanes, error0 and no remote router. NHI/RTKit
remain ready, quarantine clear. The main block is link training, not merely
an early snapshot or a missing discovery event. Dock router/authorization,
PCI endpoints, Ethernet, USB and external display remain unobserved in Darwin.

During the same normal Linux visit, a bound controller and authorized active
TS3/I210 guarded the PCIe-C capture. A root-port UP check preceded ordered,
nonposted DART reads; parameter registers were excluded. The active64-stream
bank has TCR0/8/16/24/32 at `0x80`, others zero, no ERROR.FLAG, global enable
at+0xfc `0xffffffff` and+0x100 `0x80` (the latter is also TCR0). RID2SID
entries prove the five bus3..7/device0 endpoints use SID0 and intermediate
bus2/device1..4 bridges use SID8/16/24/32. Thus a native mapper must account
for bridge streams as well as the shared endpoint stream. Raw TTBRs and all
register captures remain private. No Darwin PCIe-C DART write has occurred.

The next USB-provider-only source candidate reads at most512 calibration
registers derived from the retained validated common/CIO lane groups, plus
the analog registers already used by fuse-derived setup. It preflights the
complete bounded list before reads, maps no new register window and never
reads the fuse controller or firmware storage. One snapshot runs after PHY
configuration with prior ACIO proved off and before client-lease commit;
the other runs through the existing live owner/token query. Independent
pre-ACIO/later counts and errors make partial results explicit. Linux will
read the matching live-DT register list under the established active-dock
guard, with masks/expected values kept private for comparison. Differences
may be consequences of firmware training and must not be blindly written
back. Native SDK/collection, ATC host suites and repository checks pass;
this extension is not installed yet. No timer or mode/rate write is added.


### CIO12 diagnostic failure and guarded restoration (10:06Z)

CIO12 booted with NHI/RTKit ready, no quarantine and the desktop running,
but both new calibration observations returned ENOENT2. The record-capture
qualification failed before the manual read. No panic occurred. The guard
restored qualified non-AVE CIO11 at10:06:38Z and reaped its UART reader at
10:06:39Z. The failed diagnostic remains a failure; no native calibration
record comparison has been obtained. Linux successfully captured261
register-list observations and the corresponding private expected masks.

Source audit finds the readback rejected a present zero-byte common group,
which the existing mapping/application contract permits. The correction
shares this presence/empty/lane validation between the real native adapter
and an ASan/UBSan regression test. Missing properties and empty lane tuning
remain failures. Group-specific failure metadata and empty-group reporting
will identify the actual table on the retry; the first failing table was
not instrumented, so it is not yet claimed as confirmed. Corrected native
SDK/collection, focused ATC tests and all six required checks pass; the retry
is not installed. No PHY configuration, rate or timer change is made.


### CIO12B readback qualified; next documented PIPE initialization (10:27Z)

CIO12B is installed and qualified10:18:06Z, UART reaped10:18:08Z. The empty
optional `tunable_CIO_CIO3PLL_TOP` is now identified, confirming the prior
ENOENT diagnostic cause. Pre-ACIO and leased reads both return250 records,
error0, with174 unique register addresses: the exact same address set as
Linux's261-list capture. Every native masked tuning value agrees with Linux's
expected configuration. Native pre-ACIO and late values are identical; six
full-register values differ from active Linux (offsets0x2088,0x2a28,0x9000,
0x90ac,0x10000,0x100ac). Raw masks and values remain private. This rules out
missing/misapplied retained tuning bits in the observed list, not all possible
PHY failures. The privileged read and actual nonprivileged rejection pass.
NHI/RTKit and the standard desktop are healthy; physical ports still report
UNPLUGGED/state7 with no remote router and no downstream devices.

The known PIPE difference at NONSELECTED_OVERRIDE bit15 has direct source
provenance: m1n1 `usb.c` at121fcbf35d45 writes the `0x9332` parked baseline
before Linux, including the documented DUMMY_PHY_EN bit. Native boot reaches
`0x1332`. The next TBT-only initialization enables that one bit and preserves
all other PIPE/AON fields. It requires a fresh physical ACIO-off check under
the exclusive uncommitted broker lease, a completed dummy park/ACK, and actual
DWC3 reset/clamp/native-reset/power-state readback. DWC3 reset is never
released. Invalid proof or failed readback aborts before NHI admission.
Already-enabled state sends no write. There is no new timer or retry loop.

The real helper's sanitizer tests cover refusal without writes, inaccessible
registers, missing stop/park/reset proof, failed enable readback, idempotence
and preservation of unrelated fields. Native SDK/collection and all six
repository checks pass, zero overlaps, only the USB-provider UUID changed
against CIO12B, other56 fixed. This functional step is not installed or
hardware-qualified yet; the other differing AON bit remains untouched.

### CIO13 observed: parked dummy PHY enabled, dock link still absent

At10:38:13Z the M1 booted the non-AVE CIO13 collection successfully; UART was
reaped at10:38:14Z. The guarded one-bit PIPE bootstrap completed with error0,
NONSELECTED override4914->37682 (bit15), and AON48 unchanged. The explicit
late root snapshot still reports both physical ports state7/remote0; no dock
router or PCI function appeared. NHI/RTKit, desktop/Wi-Fi, privileged query
and calibration checks passed. This corrects the documented parked-PHY
bootstrap difference; it does not establish a trained link. Next diagnostic:
bounded real NHI reads of lane-disable and adapter-lock bits.

### Native PCIe enumeration adapter and next lane diagnostic: source/SDK only

The new unwired `PDCIOPCIHost` compiles against the actual IOPCIFamily SDK.
It validates apciec1 resources, real physical RUN/link and retained owner
power, serializes/revokes config access, accepts subordinate multi-device
bridges and always strips command bus-master enable. No personality starts
it. Cold-init/tunnel callbacks, actual endpoint mapper/MSI, posted BAR mappings,
endpoint-personality exclusion and real child/DMA drain are still required.
No dock PCI or DMA evidence follows from this build.

The next read-only NHI diagnostic includes real LD (lane disabled) and LCK
(adapter locked) bits with flags-valid/error propagation. Its actual inline
reader passes ASan/UBSan input and both read-failure cases and64 field
combinations. SDK and all six local checks pass; this diagnostic is not yet
installed. It uses the existing startup/administrator query, with no recurring
timer or writes. CIO13 remains the observed non-AVE installed kernel above.

### PCIe-C native cold-init/link adapter: SDK and host evidence only

`PDCIOPCIController` maps the actual apciec1 root/controller windows and
uses nonposted accesses with dsb/isb ordering, actual ATC1_PCIE PMGR reads
and retained power/PHY-owner callbacks. It requires host/DART registry
children absent at initialization; it never touches PCIe-C DART parameters.
It adapts the existing bounded cold-init/link helper and43 published T8103
tuning records. All42 root records match the active Linux capture; the
remaining port record is the pinned t8103.dtsi constant. SDK and sanitizer
host tests pass (every tuning I/O failure, critical masks and idempotence).

This adapter remains unwired. No native cold-init/link or PCI enumeration
is claimed. Real CM tunnel callbacks/deferred activation, endpoint DART/MSI,
posted endpoint BARs and child drain are outstanding. Any attempted programming
pins its owner/mappings until reboot; revocation only blocks access and is
not hotplug teardown. CIO14's separately frozen diagnostic is the queued
hardware candidate; this controller is not in that collection.

### CIO14 observed: both lanes enabled and unlocked, still UNPLUGGED

At10:51:12Z CIO14 passed its actual native NHI/PHY/calibration, privilege,
standard desktop and Wi-Fi checks; UART was reaped at10:51:13Z. Both actual
NHI reads report PHY capability17, flags-valid1, lane-disabled0 and
adapter-locked0, yet state7/remote0. The same Linux visit reports capability17,
LD0/LCK0 and UP/state2 on both lanes. Disabled/locked adapters therefore do
not explain this particular link failure. No dock router/auth/PCI/Ethernet/
USB/DP has been established on Darwin. The installed collection remains
non-AVE,57 modules/no pdave/605 arguments, and excludes the newer unwired
PCIe controller adapter. Next source comparison: remaining cold PIPE AON
baseline difference; no register meaning is inferred from that difference.

### Next functional hypothesis: exact cold PIPE AON baseline

The public pinned m1n1 usb.c writes the whole AON word1; Linux atc.c then
clears DWC3 RESET_N and sets CLAMP, giving0x10. Darwin's observed cold/parked
word is0x30. Bit5's meaning remains unknown. A separate guarded experiment
accepts only these exact two words, with current ACIO-off and drained,
parked/reset/clamped DWC3 proof and dummy PHY enabled. It writes only the
held-reset word0x10, verifies readback, and rejects every other AON state.
It never releases reset. This is a cold-baseline hypothesis, not a claim
that bit5 explains training. The same bounded lease snapshot includes13
additional documented MAC/BIST/PLL registers for the Linux comparison,
without recurring polling or writes. CIO14 remains installed until a
separately validated and announced candidate is run.

### TS3 authorization and tunnel hooks: source only

An explicit native NHI authorization entry point and optional all-or-none
PCIe backend hooks now build in the SDK. There is no installed caller or
backend; absent hooks refuse authorization/activation. The real domain lock,
actual root/remote relation, direct03d:0011 model/route and fresh physical
UP read precede the actual CM approve call. Keyed, unplugged or ambiguous
peers are refused. Authorization/event publication occurs only after real
CM success. The bounded snapshot can report actual remote authorization.
The host allowlist test is metadata evidence only; no dock has been authorized
on Darwin. These source changes are excluded from the frozen CIO15 candidate.

The actual native apciec1 DT capture also confirms all33 available root/port
tuning records match the published native constants; the ten OE-fabric
records come from the pinned public DTS and match active Linux. Raw data
remains private. Linux runs cold-init from its deferred post-activation
worker, so both native coldInit and linkStart now require real activated-
tunnel proof outside the CM domain lock. The pre hook reserves prerequisites
without PCIe-C MMIO; no native backend is installed yet.

A native read-only PCI inventory CLI now SDK-builds for stage(a). It follows
actual IOPCIDevice ancestry to PDCIOPCIHost and prints only public PCI IDs/BDFs.
Acceptance requires all four xHCI controllers and the I210; root-only/partial
inventories fail acceptance. No native invocation or PCI hardware evidence
is claimed until it is run on the M1.

### CIO15 observed: cold AON hypothesis did not train the link

At11:04:51Z the actual CIO15 kernel passed NHI/PHY/calibration, root versus
unprivileged query and standard desktop/Wi-Fi checks; UART was reaped at
11:04:52Z. The exact guarded AON normalization completed48->16 with error0.
All23 expanded PHY/MAC/BIST/PLL registers match active Linux, as do the
five sampled PIPE registers. Both physical lanes remain enabled/unlocked
but UNPLUGGED/state7, remote0. Thus this cold AON change alone did not fix
training; bit5's function remains unknown. No dock router/authorization,
PCI, Ethernet, USB or DP has been observed on Darwin. The installed
collection has57 modules, no AVE module or pdave argument, and unchanged605
boot arguments. AVE remains paused.

The real native PCI inventory CLI ran on CIO15 and returned2 with
functions0/xhci0/i2100, correctly failing the stage(a) registry acceptance.
That negative execution qualifies the tool's empty-domain path only.
The positive four-controller/I210 path remains unobserved. No further reboot
will interrupt the announced Firefox30-minute stability slot; source/host
work proceeds on combined Type-C control/NHI lane diagnostics and endpoint
DMA preparation.

### Combined next diagnostic: source/SDK only, CIO15 remains installed

With23 PHY/MAC/PLL and five PIPE registers matching Linux but lanes still7,
the next read-only candidate batches the remaining control boundaries:
Type-C event1/mask1/power/status/data via a real serialized port1 control
reader, root/adapter/PHY headers over NHI, actual wrapping24-handler IRQ
counts, live M3/IRQ state and256 ACIO tuning register observations. Every
tuning address/count is preflighted before reads (capacity512); no firmware/
SRAM/DART parameters or caller-provided addresses are read. The native
Type-C helper has input and every-read failure tests; failed reads leave
output unchanged. Its query shares the existing sampler's workloop and is
rejected after stop. No IRQ acknowledge, mask/role/mode write or new recurring
poll is introduced. Source/SDK evidence remains separate from hardware.

The Linux comparison is prepared for the next ordinary visit: fixed port1
I2C_RDWR register reads (bus DT i2c@235010000/usb-pd@3f, actual bound driver,
J293), with the Linux adapter serializing each address-prefix/read transfer;
no I2C_SLAVE_FORCE, command task or IRQ/mask write. ACIO samples use the
same256 validated addresses and active-NHI/authorized-dock guard. Raw
snapshots remain private. No extra visit/boot interrupts FX's stability slot.

### I210 driver preparation: separate SDK artifact, not installed

The existing PureDarwin em/igb source includes I2108086:1533. A dedicated SDK
variant now builds it with its45 clean source files pinned and checked. Its
private fbsdkpi patch requires the actual native PCIe-C mapper, rejects physical
fallback and validates full owned spans within nonzero32-bit IOVA space.
The named mapper is not implemented/registered yet, so this is intentionally
not DMA admission. Transfers bounce through distinct page-owned descriptors;
failed prepare/segment/sync/retire keeps command/backing and poisons further
mapping. Posted PCI BAR mappings are confined to this Ethernet artifact;
the native root/config path stays nonposted. No tunnel-compatibility bypass
or boot-collection addition is made.

Exact frozen XNU source confirms clearMemoryDescriptor(true) discards errors
from complete(). The adapter calls/checks complete() explicitly and clears
only an inactive descriptor before releasing backing. The actual callback-
ordering helper passes failure/no-release sanitizer tests; this is not real
DART TLB or device/IRQ/work-drain proof. Those native mapper, MSI/teardown and
positive DMA/link/DHCP qualifications remain outstanding. The uninstalled
Ethernet driver cannot be the installed kernel's idle-load cause.

### CIO16 observed readback; failed desktop qualification and CIO15 recovery

Source2614eb5d7/capsule9b9baa004c1b… booted at11:42:05Z. The actual startup
snapshot completed once with NHI/RTKit ready and no quarantine. All256 ACIO
tuning registers, including full words and intended masks, match active
Linux. All five host-router header words also match. Both lanes still report
UNPLUGGED/state7, enabled/unlocked, no remote router. Lane basic-header DWORD4
differs at bit30; PHY control DWORD18 differs at bits5/18/21/26/28. These
include dynamic lane fields; no speculative write is justified by a diff.
Type-C event1 differs at bits0/1/8/10; mask1 at bits0/2/8/9/54/55/59. Power,
status and data-status match Linux. Raw snapshots and comparison remain private.

The75s desktop/inject check failed at11:43:21Z. The standard recovery restored
qualified CIO15 through the normal Linux installer, verified the desktop and
inject directory, then stopped/reaped the owned UART reader before releasing
gui.lock at11:45:56Z. No panic was captured. This is a failed collection
qualification, not a working CIO16 deployment. Its late/root/unprivileged
queries and planned60s idle/IRQ comparison did not run. Retained session logs
show no CIO16 compositor-start record; the UART has no first SF_SESSION marker.
The early launcher cause is unknown; capture did not include its process tree.
CIO16B repeats the identical image with that missing evidence and a bounded
255s session deadline; it changes no source, kernel module, argument or AVE state.

Pinned Linux initializes the CD321x interrupt mask and acknowledges EVENT1
through CLEAR1; native startup currently ensures S0 and observes state only.
The observed mask/event mismatch identifies missing lifecycle work, not proof
that this alone prevents physical training. A native implementation must
serialize reads/acknowledges, handle reset/setup and failure, and avoid enabling
an unhandled GPIO interrupt or adding a fast polling loop. T8103 also has
lstx_explicit=false: its Linux M3-control RMW is already reproduced; the literal
write3 used on newer T6030 is not a T8103 fix.

### Port1 event acknowledgement prepared; source/SDK only

A later Type-C variant follows Linux cd321x_interrupt ordering: read eight
EVENT1 bytes, skip writes on zero, otherwise write exactly the observed eight
bytes to CLEAR1, then sample negotiated status. It shares the existing1Hz
workloop and changes no interrupt mask or GPIO/AIC enable. Failed reads do
not acknowledge; ambiguous failed writes are counted separately and publish
an unavailable sample, never an invented unplug. Actual-helper sanitizers
exercise every one of64bits plus zero/read-error/write-error cases. The native
Type-C SDK and all six required checks pass. No hardware event-clearing or
link improvement is claimed. This variant is excluded from frozen CIO16B.
Linux's mask/setup-reset recovery remains outstanding; acknowledging events
alone does not establish that lifecycle or prove the source of the training
failure.

### CIO16B qualifies the identical image; actual idle/IRQ result

The same capsule repeated successfully at12:03:54Z, after its first boot request
at12:01:35Z. The75s process/launchd snapshot shows sf-session and the standard
.next compositor alive, with the inject directory present. All native control,
PHY, ACIO observation and UID65534/root denial/acceptance checks pass. The
post-idle query completes with count3/error0. Owned UART was stopped/reaped
at12:03:55Z before gui.lock release. The installed manifest has57modules and
no AVE; an actual registry/boot-argument check also finds no bound AVE service
or pdave argument. The failed first attempt stays recorded and unexplained.

During a quiet60s sample, visible nonidle kernel thread work totals2.201445%
of one CPU, aggregate host idle is98.1876%, the dominant unnamed thread uses
1.55995%, and PDM1SPIHID0.33516%. The24 NHI handler counters advance only9TX0
and9RX0, including diagnostic control traffic across the measured boundary;
other22 remain unchanged. No NHI interrupt storm is observed. The parser
excludes the exact frozen XNU TH_FLAGS_IDLE=0x2, rather than counting idle
threads as work. Warm50M integer-mix runs take83.676–83.678ms and100k system
calls14.496ms, matching earlier fixed-work baselines. The cold first round
was104.548ms and is not substituted for a warm comparison. The prior k2/USB04
A/B remains2.42%/1.94% one CPU with the same NVMe timed sleep. These results
still do not identify the reported AppKit slowdown or qualify an idle fix.

CIO17/source83786dc6e/capsulefcf8f72d697e… is built and announced for12:08Z
or later under the usual locks. Only Type-C changes from qualified CIO16B;
other56module UUIDs,605boot arguments and boot.bin remain fixed,57modules/noAVE,
zero overlap. It tests exact observed EVENT1/CLEAR1 acknowledgement on the
existing1Hz sampler, with no interrupt-mask/enable or role/mode write. Actual
acknowledgement count, error/uncertainty, unchanged mask, standard desktop and
NHI health must qualify before accepting it. CIO16B is the rollback. No event
clearing or link improvement from CIO17 has yet been observed.

### Native requester-map backend prepared, not wired or installed

The unwired PDCIOPCIController now reserves actual dynamic BDFs in the64entry
T8103 RID2SID table, using the pinned PCIe-C iommu mask0x3f. The native method
requires current activated-tunnel/power and physical RUN/link readbacks; an
all-ones read is rejected. The actual reservation helper checks an empty
hardware slot, reserves it before the write, checks full readback and poisons
on transport failure, stale occupied hardware or mismatched duplicate. No
slot is recycled or retargeted. Error outputs remain unchanged. Multiple
endpoints legitimately share SID0 as on Linux; this is not per-device DMA
isolation. Intermediate bridge device numbers produce the observed8/16/24/32.

Bounds, table capacity, shared SID, duplicate/no-rewrite, stale slot, every
read/write failure and readback corruption pass actual-helper sanitizers.
The native CM SDK and all six checks pass. This source backend is excluded
from frozen CIO17; no mapping register is changed on the M1. It supplies no
DART table, MSI, endpoint driver/BME admission or hot-remove drain proof.

### CD3217 startup-mask preparation, excluded from frozen CIO17

A later Type-C source variant implements the remaining tipd_init startup
MASK1 step after APP/S0 checks and before service/cable publication. It uses
exact Linux tipd_cd321x_data bits1/9/10 (plug/power-status/data-status), not
STATUS_UPDATE bit8 or the newer SN201202x DP-SID bit. It reads first and skips
the write when already correct; a changed mask requires one full64-bit write
and exact checked readback. Read failure, ambiguous write or mismatch prevents
PHY admission. Delivery still uses the existing1Hz polled EVENT1/CLEAR1 path;
no GPIO/AIC interrupt is enabled. Startup and acknowledgement have separate
properties, so the previous mask is preserved as evidence.

The actual inline helper passes already-correct/no-write, upper-byte clearing,
each I/O failure and readback-corruption sanitizer tests; the native Type-C
SDK and six required checks pass. This is not installed, is excluded from
frozen acknowledgement-only CIO17 and has no M1 mask-write/link evidence.
This startup step does not implement live controller-reset recovery or the
consumer teardown/revalidation that such a reset needs. Those remain required
before claiming the complete Type-C/hotplug lifecycle.

### CIO17 observed: event acknowledgement works, link still down

CIO17/source83786dc6e/capsulefcf8f72d697e… qualified at12:20:27Z, UART
stopped/reaped at12:20:29Z before lock release. The standard .next desktop,
inject directory, Wi-Fi, RTKit/NHI, shared PHY and root/UID65534 snapshot checks
pass. The actual Type-C acknowledgement count is1, errors0, uncertainty false,
EVENT1 reads zero and MASK1 is unchanged. Both lanes are still7/remote0. This
qualifies the native clear-event path and disproves acknowledgement alone as
the link fix. No dock router/auth/PCI/Ethernet/USB/DP observation is added.
The installed57module collection has no AVE; actual boot arguments have no
pdave. CIO18's startup-mask-only change is announced for12:25Z or later, with
qualified CIO17 rollback. Other56 UUIDs/605arguments/boot.bin are fixed.

The parallel quiet61s measurement reports nonidle kernel2.13797% of one CPU,
aggregate host98.29896% idle, SPIHID0.34219%, and18 NHI interrupts (9TX0/9RX0
including diagnostic traffic at the boundary; other22 unchanged). Warm50M
integer runs83.677–83.767ms and100k syscalls14.692ms match prior measurements.
The actual five-thread compositor uses3.48451% of one CPU: Shm dropping thread
2.10937%, main0.93908%, pdhid-reader0.43311%, agx-bo-cache0.00296%, disk worker0.
Actual TASK_POWER_INFO rates are interrupt428.197/s, platform-idle363.869/s,
timer bins115.393/s and206.918/s. The observer uses real SDK MIG/task ports,
checks the compositor PID and reads all13 thread/time samples; it sends no
input or signal. The named HID reader is not consuming a core. The cleanup
thread is the compositor's largest observed CPU consumer; this still does
not explain the reported1.5–2x AppKit slowdown. Findings were recorded for
DESK in the coordination answers/log; no desktop code or policy was changed.

### First-enumeration source prerequisites: isolated USB profile and DART blocker

An audit of the actual frozen bundles confirms RavynXHCIPort retains Generic
XHCI/IOPCIDevice matching class0c0330 alongside PDAppleUSB's native Type-C
personality. A new explicit build profile removes that PCI personality from
a copied USB bundle. Native build verification confirms only Generic XHCI is
removed; the five other personalities, top-level metadata and Mach-O bytes
are identical. It changes no installed bundle or default build and adds no
tunnel-compatibility/DMA flag. This is a first-enumeration profile ingredient,
not evidence of a safe complete collection or endpoint admission.

The unwired PCIe controller also gains blockEndpointDMA(), intended after real
activated-tunnel/RUN/link and before publishing the enumeration bridge. It
requires the actual DART DT node to have no bound clients, checks fixed bank
521008000/size, and rechecks physical power/RUN/link before every DART access.
An explicit register whitelist excludes PARAMS0/4. The actual helper checks
unlocked configuration/idle command, disables and verifies all64 TCRs before
clearing/verifying all256 TTBR words at0x400, selects both32-bit stream banks,
invalidates and requires bounded completion, then verifies all TCRs remain
zero. It never enables translation/bypass/global gates/IRQs or clears faults.
Any failed attempted operation poisons the retained owner; it proves no child,
IRQ/work or backing-memory drain. All712 modeled I/O failures plus lock,
readback and100us busy bounds pass sanitizers; the native CM SDK and six checks
pass. This code is not wired/installed and is excluded from CIO17/CIO18.
No PCIe-C DART register has been touched on Darwin by this preparation.

### CIO18 observed: Linux mask and events match; physical link remains absent

CIO18/source1613d830d/capsule211cae69e8a0… qualified at12:35:07Z, after
waiting for TERM7's visual slot. UART was stopped/reaped at12:35:09Z before
lock release. The actual mask-write-attempted flag is true, both before/after
reads valid, exact Linux bits1/9/10 read back, mask error0. Actual event ack1,
error0, EVENT1zero. Standard desktop/.next/inject, Wi-Fi, NHI/RTKit, shared PHY
and root/UID65534 snapshot controls pass. Both lanes are still7/remote0. All
five Type-C control values now match the same-visit active Linux sample;
all256 ACIO full words/masks and five root-header words match as well.
No router/auth/PCI/USB/Ethernet/DP evidence is added. This remains a57module,
non-AVE collection with605arguments and no pdave argument. AVE stays paused.

Quiet61s: nonidle kernel2.48174% of one CPU, aggregate98.05677% idle,18 NHI
interrupts including diagnostics; warm50M integer83.682–83.809ms and100k
syscalls14.610ms. Actual compositor pdhid-reader0.42421%, shared-memory cleanup
2.33342% of one CPU. These do not establish the reported AppKit slowdown's
cause. Cold first-round timing is retained separately and not substituted
for the warm baseline. No idle fix is claimed.

A full-word comparison, rather than only intended masks, now covers all174
unique PHY tuning addresses. Six differ outside programmed masks:2088,
2a28,9000/90ac/10000/100ac. The2088 register is AUSPLL_FREQ_DESC_C and Linux
has an active DP tunnel; the other differences are not treated as permission
to force unknown bits. Native firmware syslog reaches lane-init Phase2 after
Phase1, without a captured assert. Prior Linux cycles wrote full dmesg on
Linux but did not stream that file back to the host; the next ordinary visit
will capture it to compare actual firmware version/phases. No firmware or
kernel-image extraction is involved, and no extra AVE work is authorized.

### Explicit TS3 approval and enumeration profile: source/SDK, not installed

The private enumeration CM build now wires the actual three native PCIe hooks.
Pre-activation reserves the real owner/maps without PCIe MMIO. Post-activation
records completion only. The administrator-only exact request
pd-cio-authorize-ts3 calls the real direct-TS3 allowlist/approval helper. Only
after real tb_domain_approve_switch and the post callback return and release
the domain lock does the native owner cold-init/link PCIe-C, block all64 DART
streams and start the native IOPCIFamily host. Each controller access checks
its current activation lease and physical power. This source is not in CIO18.

Before activation and again before host publication it reads the actual
IOCatalogue, permitting only the real PCI bridge class/bundle and the exact
existing Wi-Fi match among IOPCIDevice personalities. Startup also reports
the actual profile result without failing NHI startup. It does not trust a
registry marker for driver exclusion. The copied frozen-base profile removes
endpoint PCI matches from16plists while preserving all72 code files and all
non-PCI personalities. Three PCI bridge personalities remain; the separate
unchanged PDBwfm supplies Wi-Fi. The separate native USB profile removes its
Generic XHCI match. Both ordinary and explicit CM SDK variants, the native
authorization CLI, actual artifact verification, host helpers and six required
checks pass. The first enum SDK attempt failed on a copied read-only CMake
file; that receipt is preserved, the copied file permission fixed, and the
final builds pass. No target execution or positive approval is claimed.

BME stays unconditionally stripped; no endpoint mapper/MSI or driver admission
is supplied. Failed/removed programmed hosts block config and pin resources
until reboot. CM stop explicitly refuses after an enumeration attempt because
the Linux void domain-removal API cannot propagate a failed native child drain.
There is no hot-remove success claim; the ten physical cycles still require
real child/IRQ/work/TLB teardown. The new CLI's successful return would mean
approval/host startup only; the independent PCI inventory must still observe
all four xHCI controllers and I210 before stage(a) passes. Its UID65534 denial
mode and real-catalogue result await the next suitable native boot.

### CIO19 qualification and phase2/3 boundary (13:09Z)

The explicit enumeration image qualified with the actual catalogue and
administrator checks described above. No approval, PCIe MMIO, endpoint DMA,
Ethernet, dock USB or display success is claimed. Source/SDK evidence in the
preceding section was prepared before this boot; only its runtime negative
path and driver-exclusion profile are now qualified.

The full Linux dmesg was streamed to the host during the ordinary installation
visit, without firmware/image extraction. Linux and Darwin report the same
ACIO firmware version and cable-info0x411. On the actual physical port1 Linux
reaches lane-init Phase2 at5.062551s, Phase3 at5.097641s and Connected at5.100003s.
Darwin reaches Phase2 and does not capture Phase3/Connected. This narrows the
observed divergence to the low-speed lane-init boundary; its cause is not yet
proved. Linux probe unconditionally disables DP AUX before its first mode,
just as the native stop sequence does. Linux also powers USB2 in TBT mode.
Neither is evidence for removing those operations. Unknown PHY bits remain
unchanged pending source or measured evidence.

The same guarded quiet60s measured kernel2.331255% of one CPU, aggregate
98.09594% idle and18 NHI interrupts including diagnostic request boundaries.
The actual compositor HID reader used0.408775%, cleanup2.320012%, main0.903145%
of one CPU; total3.634607%. Interrupt wakeups434.2/s, platform-idle364.2833/s,
timer bins117.7/210.65/s. Warm50M integer83.676–83.686ms,100k syscalls14.508ms.
The cold first round173.772ms is recorded separately. The reported AppKit
slowdown remains unresolved; these observations do not establish an idle fix.

Source follow-up adds six bounded, read-only AUX/USB2 control words to the
existing lease-checked PHY snapshot (two AUX and four USB2 registers). No
new timer or mode write. The native USB SDK and all six repository checks
pass; this diagnostic source has not yet run on the target.

### CIO20 AUX/USB2 comparison and SID0 source checkpoint

CIO20 adds six bounded reads to the existing lease-checked PHY snapshot;
programming, IRQs and timers are unchanged. Five complete words match the
same boot's Linux capture: both AUX controls and USB2 RUN/CTL/SIG. USB2
MISCTUNE differs only at bit11; the pinned power sequence changes only bits29/30
there. That unknown bit is preserved, not forced. All174 native PHY tuning
words remain unchanged between the pre-M3 and late snapshots. Neither AUX
power-down nor USB2 startup explains the measured phase2/3 divergence.
The identical-version/cable firmware still reaches Phase2 only on Darwin.
No dock/router/PCI/USB/Ethernet/DP success is added. Loadavg1.18/0.50/0.20 at
about80s after boot is retained as startup evidence, not a quiet idle sample.
The reported AppKit slowdown is unresolved.

A separate, uninstalled SID0 DART transaction helper now checks the actual
64TCR/256root blocked bank before setup, validates a16KiB/36-bit owned root,
uses TTBR0 at400 and both invalidate selectors34/38, preserves global gates
while enabling bit0, and writes TCR0 exactly80 without bypass/remap. In
particular it does not copy an all-ones upper enable mask into100, the SID0
TCR. A native owner must prove real tunnel/power/RUN/link and blocked endpoint
BME, serialize access, publish actual retained table pages, and retain all
backing after any uncertain write/flush. Retirement verifies the same root
and isolated bank; endpoint/IRQ/work drain remains a separate prerequisite.
No parameter read, fault acknowledgement or IRQ enable is added.

Host ASAN/UBSAN tests pass all350 setup and349 retirement I/O failures, all
64 stale TCR and256 stale root preconditions, foreign-root rejection, preserved
gate bits, ordered publication/flush, readback failures and100us timeout bounds.
All six repository checks pass (274 Python/77 C groups; six mutations caught;
plans remain dry runs). This is a source/host component, excluded from CIO20;
there is still no native endpoint IOMapper or DMA admission on the M1.

### Native I210 IOMapper: source/SDK only

The explicit build-pci-dma-kext variant now compiles a concrete IOMapper for
I210 SID0. Its native initializer walks the actual PDCIOPCIHost descendants,
requires four xHCI and one8086:1533 I210, rejects every set endpoint/bridge BME,
reserves the actual BDF->SID0 entry, and verifies the real blocked DART bank
before installing its own retained root. Each MMIO callback checks the actual
controller power/RUN/link/tunnel and exclusive mapper identity. The ordinary
and enumeration CM variants exclude this mapper; all three SDK builds pass,
and their actual ARM64 symbols verify that separation. No boot collection
or CM factory call contains this new variant.

Mapping accepts only complete16KiB-page IOBufferMemoryDescriptor bounces with
independently checked contiguous physical spans below36bits. It constrains
IOVAs below32bits, validates alignment/address-bit requests, rejects duplicate
live descriptors and associates retirement with the actual descriptor and
IODMACommand identities. Backing and IOVA reservations are retained before
PTE writes. Any PTE, hardware-state or flush failure poisons the owner and
retains backing/reservations; only an exact successful unmap plus completed
flush releases a mapping. There is no physical-address fallback or generic
page insertion. A real self-reference pins the mapper, controller, endpoint
and tables before the first possible TTBR write, including partial-start
failure. Stop revokes access and retains resources until reboot; no false
endpoint/IRQ/work-drain or hot-remove success is returned.

The controller refuses its whole-bank blocker while a real mapper owns the
bank. The object has no personality, no automatic registration, no BME/MSI
admission and no caller in the installed CM. Source/SDK and required repository
checks pass; native mapping, I210 DMA/link/DHCP and teardown remain unobserved.

### Bounded peerless retry: source/SDK, target test pending

The explicit enumeration CM now accepts one exact administrator-requested
peerless retrain per boot. It first reads the real domain-locked NHI snapshot,
requires both actual physical lanes1/2 UNPLUGGED with valid/unlocked/enabled
controls, no remote, and the captured TBT cable word411. The native PCI owner
must have no reserved controller, host or attempted publication. A live broker
lease/unchanged cable epoch is required. The ordinary CM variant refuses the
operation. No arbitrary register/address/cable-word input or automatic retry
is exposed; UID65534 denial occurs before any counter/action.

The accepted action uses the existing real stop path: Linux domain/worker/ring
removal, joined native IRQs, NHI DMA drain and DART suspension, RTKit poweroff,
physical ACIO poweroff, and broker-verified PHY release. Only after success does
it query the same cable epoch, reacquire/calibrate the PHY and restart the same
pinned RTKit/CM owner. Any failed stop quarantines and preserves resources;
a failed restart attempts the same real cleanup and cannot retry its epoch.
The existing startup snapshot remains a single timer event, not a poll loop.
A second root request is denied without changing the accepted count. New
bounded M3 control observations retain the initial and most recent values
around the existing preserve-bits START operation, without changing its value.

The CLI, ordinary/explicit native CM SDKs, read-policy ASAN/UBSAN checks and
all six required repository checks pass. This is not yet installed or exercised;
no positive drain/restart, dock discovery or physical hotplug claim is made.
The target slot will capture both UART startups, require actual stop/reacquire/
NHI checkpoints and an unchanged desktop, test UID/budget denial, rerun actual
TS3 approval/inventory if a peer appears, and restore qualified CIO20 on failure.
The separate I210 mapper remains excluded from that candidate image.

### CIO21 runtime qualification and separate PCIe-C power guard

The source/SDK peerless retry above is now observed on the M1, with all actual
stop/reacquire/NHI and negative privilege/budget checks passing. No second
system boot was required for the runtime retry. Initial firmware syslog reaches
Phase2. The later post-runtime dmesg capture was too late: the131KiB ring begins
at uptime79.7s and contains mainly sandbox/code-signing messages, with neither
CIO startup retained. UART captured the system boot but does not contain those
firmware syslog strings. Therefore the post-retry firmware phase is unverified;
the actual restarted hardware still reports both lanes unplugged. This gap is
not relabeled as observed Phase2 after the retry.

After the new lifecycle behavior, quiet60s measured kernel2.229853% of one CPU,
aggregate98.15885% idle and18 NHI interrupts including query boundaries. The
compositor used3.374861% of one CPU: HID0.426192%, cleanup2.028593%, main0.917225%.
Wakeups431.9167/s interrupt,362.6/s platform-idle, timer bins115.9833/209.0/s.
Warm50M integer83.679–83.761ms,100k syscalls14.654ms; cold147.643ms kept separate.
The reported AppKit slowdown remains unexplained. The later loadavg1.49/1.43/0.82
was collected during other agents' GUI work and is not a quiet idle measurement.

A separate source fix gives the native PCI host the same always-on PCIe-C PMGR
gate proof already used by its controller backend. ACIO/PHY power alone does
not prove the PCIe-C domain accessible. Host startup and every config access now
check that gate before reading PCIe-C RUN/link, reject reset/disabled/off/all-ones
states, and reject all-ones RUN/link too. No register programming is changed.
Native enum SDK and all six repository checks pass; this guard is not installed
in CIO21 and has no actual PCI hotplug qualification yet.

The I210 interrupt audit confirms the existing private fbsdkpi path counts real
IOPCIFamily interrupt indexes marked PCIMessaged. It must be supplied an actual
AIC-backed MSI route; the Wi-Fi bridge's custom MSI function alone is not that
integration. No fabricated interrupt index or successful MSI stub is supplied.

### Focused USB4 control observation: CIO22 qualified

The existing root/physical-port snapshot now also reads exactly ROUTER_CS5/6/26
(sleep, TBT3 capability and router-operation status) and the actual primary
port's USB4-capability-relative PORT_CS18/19 (link mode/configuration). These
five public control words are already in the saved Linux debugfs trace. UID
registers7/8, operation payload9..24 and sideband payload windows are excluded.
The secondary lane legitimately has no USB4 capability and reports unsupported
for that optional section, rather than fabricated data. Reads use actual NHI
config requests under the existing domain lock; there are no new writes,
thread or timer, only three additional packets per explicit/startup snapshot.

Ordinary/enum native SDKs and all six required repository checks pass. The first
SDK attempt failed because the native setProperty data overload takes a mutable
pointer; the local offsets array was corrected, the failed receipt retained,
and the second build passes. CIO22 now observes these controls on Darwin.
Linux's root sleep/TBT3-disable flags are clear; its primary port reports TBT3,
bonded/configured, no reset or initialization-disable request and no training
in progress. Differences will be interpreted against source, not forced blindly.
The installed image includes the separate PCIe-C host power guard and excludes
the endpoint mapper. CIO21 remains the qualified non-AVE rollback baseline.

CIO22/source455ea303f/capsule8f3800a8bf4c installed14:29:43Z and qualified
14:31:04Z; its UART reader was reaped before lock release14:31:06Z. Standard
`.next` desktop/injection, Wi-Fi, NHI/RTKit/owned PHY, catalogue and UID checks
pass. Snapshot count is1 at startup, unchanged by UID65534 denial, then2 after
the explicit root query. Firmware start count1/retrain count0 are unchanged.
Both initial and explicit snapshots agree:

| Public control | Active Linux | Darwin | Meaning supported by the pinned source |
| --- | --- | --- | --- |
| ROUTER_CS5 | `0x0` | `0x0` | Sleep/TBT3-disable and tunneling flags agree. |
| ROUTER_CS6 | `0x01000000` | `0x01000000` | Router-ready set; sleep/TBT3-not-supported flags clear in both. |
| ROUTER_CS26 | `0x11` | `0x10` | Last operation differs (allocation versus query); neither operation-valid/unsupported flag set. Not a stable configuration mismatch. |
| Primary PORT_CS18 | `0x3710` | `0x410` | TBT3 mode and bonding clear only in Darwin; bits12/13 also differ. Training-in-progress is clear in both. |
| Primary PORT_CS19 | `0xe` | `0x6` | Configured bit3 clear only in Darwin. No reset or initialization-disable request in either. |

PORT_CS18 CPS means CLx support, not cable power. PC can reflect successful
peer discovery; it is not evidence to force configuration on an unplugged
lane. No status bit was written. The secondary lane correctly reports no USB4
capability (unsupported optional controls), while its base PHY read succeeds.
Native firmware logs show the real cable parameter411, Phase1 then Phase2,
without Phase3. Both lanes remain state7/remote0. TS3 approval returns native
ENODEV19 (CLI exit1), and independent PCI inventory returns2 with zero
functions/no native host. No dock PCI, Ethernet, USB or DP success is implied.
This narrows the next investigation to the low-speed lane-init boundary;
matching root controls do not explain that failure.

This Linux visit again sees the dock and Keychron3434:0610. Its interface0
is HID boot-keyboard class/subclass/protocol03/01/01; two other HID interfaces
are03/00/00. This is Linux evidence only. No owner change is needed.

### Native I210 MSI transport: separate SDK variant, not installed

`build-pci-msi-kext.nix` now builds a concrete
`PDCIOPCIMSIController`, using the frozen IOPCIFamily's actual
`IOPCIMessagedInterruptController` ABI. The native bridge supplies that object
through the family's standard platform functions before publishing children.
It verifies the actual PCIe-C MSI configuration, device-tree vector base818,
32 AIC vectors and the programmed doorbell. The real family allocator creates
the interrupt indexes marked PCIMessaged that fbsdkpi requires. Admission is
limited to one MSI for an actual I210 below this native host; no fabricated
indexes or successful interrupt stubs are used. The family prefers MSI when
the I210 has that capability, so this path does not map an MSI-X table.

Parent AIC interrupts dispatch the real family handler. The frozen AIC masks
on event read without recording a hard-disable flag; a software event on a
workloop therefore joins/re-registers the actual parent before rearming, as
the existing RTKit transport does. This is interrupt-driven, with no timer.
An explicit mapper query checks the actual retained SID0 translation root,
TCR/global gate, fault status and live power/tunnel ownership before interrupt
registration. A separate factual query checks an actual registered/enabled/
armed route. Neither query grants bus mastering or proves endpoint DMA.

The final MSI SDK, ordinary SDK and enumeration SDK all cross-build. Actual
Mach-O symbols prove that both concrete mapper and MSI classes occur only in
the explicit variant, and its required MSI-family imports are supplied by the
frozen IOPCIFamily binary. All six repository checks pass (274 Python tests,
77 C test groups, six detected mutations). An initially mistyped enumeration
build filename failed before building anything; the correct target passed.
The first MSI SDK compiled; the second includes the AIC rearm correction.

This variant has no endpoint personality or CM mapper factory and is absent
from CIO22. Every bus-master request remains blocked. Partial parent setup,
revocation or ambiguous ownership retains the real objects until reboot;
nonblocking revocation is explicitly not an IRQ/work/endpoint drain. No MSI
interrupt, I210 DMA, Ethernet link/DHCP or cable teardown has been observed on
Darwin. Those remain hardware milestones after dock router/link discovery.

### Dock USB storage write barrier: separate SDK, not installed

The existing PCI xHCI driver is RavynXHCIPort, with an IOUSBFamily bus and
mass-storage child. Its generic PCI path still uses physical DMA addresses;
the direct DWC3 lifetime/DART variant does not supply the dock's shared
PCIe-C SID0 domain. The enumeration profile therefore still excludes the
generic PCI personality. The I210-only mapper/MSI preparation above does not
admit any USB controller. No dock keyboard or stick is observed on Darwin.

`build-dock-usb-readonly.nix` prepares a separate SDK variant over the existing
USB lifetime patch. Its storage patch reports write protection, completes block
writes with NotWritable before preparing memory or submitting a transfer, and
also rejects writes at the lower SCSI READ/WRITE helper. Exact application to
the pinned PureDarwin source passes with zero fuzz; the first handwritten hunk
failed in a temporary copy and was replaced with a generated exact diff.
The separate native SDK now builds; it is not installed. It does not claim
mapper, MSI, bus-master admission, a read-only mount, or endpoint teardown.
One source-staging attempt was interrupted after a shell failed to stop at a
refused lock-wait cancellation; it did not complete an SDK. That coordination
error is recorded in log.txt. The successful build ran after CIO22 ended and
its UART was reaped. No target action or AVE work came from this USB variant.

### Load-average interpretation follow-up

Pinned XNU `osfmk/kern/sched_average.c:compute_averages` samples the runnable
bucket minus the maintenance thread, then passes that sample to BSD's
five-second `compute_averunnable` update. A thread merely sleeping in the
NVMe timed wait does not itself count as runnable. Thus the old NVMe stack
sample alone proves neither the elevated load nor spinning. The measured
quiet CPU/NHI and desktop-thread accounting above remain the evidence; the
reported AppKit slowdown and elevated load have no confirmed common cause.

### T8103 USB2 cold baseline: CIO23 observed, training unchanged

The remaining USB2 MISCTUNE bit11 difference has a published bootloader
initializer: pinned m1n1 `src/usb.c:usb_phy_bringup` writes `0x008c0813` before
Linux, while CIO20/CIO22 actually read `0x008c0013`. Its bit function is still
unknown. This establishes a concrete bootstrap baseline, not a proved cause
of the Thunderbolt low-speed failure. The prior compact-tunable hypothesis was
also checked: all387 records in the saved25-group ADT audit have no set-value
bits outside the clear mask, so masked versus independent OR-set semantics
does not explain this machine's observed result. No calibration bytes are
included here.

The shared PHY now has a TBT-only cold initializer, before normal mode start
and before publishing an NHI client. The native broker freshly proves ACIO
physically off while retaining the exclusive uncommitted lease. The helper
independently checks actual dummy PIPE/ACK, DWC3 reset/clamp, USB2 isolation/
SIDDQ/reset/APB reset and both clock gates. It accepts only the two known
MISCTUNE words with gates asserted, sets the one missing bit11, and requires
exact readback. Other state refuses without writes; the existing cleanup
retains ownership on failure. Normal mode start still controls clock release.
No new timer, periodic work, DWC3 host publication or status-bit force is added.

Focused ASan/UBSan tests pass both baselines/idempotence, every foreign tuning
bit, all seven inaccessible-register cases, power/reset/isolation/PIPE refusal
and a failed write/readback. The native USB SDK and all six repository checks
pass. CIO23 installed this code through the normal installer14:45:56Z, qualified
14:48:18Z and reaped its UART before lock release14:48:20Z. The actual tuning
changes `0x608c0013` to `0x608c0813` with both clock gates retained, then reads
`0x008c0813` after normal mode start. All56 other module UUIDs, including
CIO/Type-C, are fixed;57 non-AVE modules, four PCI personalities, identical
605-byte arguments/boot.bin and no overlap. Endpoint mapper/MSI/driver admission
remain excluded. The observed baseline change did not fix lane training:
firmware still logs Phase1, cable411 and Phase2 only; both lanes remain7,
remote0, TBT3/bonded/configured flags clear, TS3 approval ENODEV19 and inventory
exit2/functions0. It is retained as source-backed bootstrap alignment, with no
claim about bit11's function. CIO22 remains the qualified non-AVE rollback.

The same slot's quiet60s measured kernel2.88868% of one CPU, aggregate97.71052%
idle and24 NHI IRQs (12TX0+12RX0, all others zero) including the new query's
three extra control transactions. Compositor4.37102%: cleanup3.01954%,
main0.94481%, HID0.40363%; interrupt wakes436.383/s, platform-idle351.333/s,
timer bins118.967/211.2/s. Warm50M83.677–83.757ms,100k syscalls14.640ms;
cold118.140ms is separate. No interrupt storm or fixed-work slowdown appears;
the reported AppKit slowdown and idle-load cause remain unresolved.

### Native PCI scan settling and MSI link gate: SDK only

The native first-enumeration path now waits1000ms after actual PCIe link-up,
outside the CM domain/controller locks, before blocking endpoint DMA and
publishing the native host. This ports the pinned `pcie-apple.c`
`APPLE_PCIE_TUNNEL_READY_MS` requirement: the TS3's FL1100 and ASM1142 respond
later than its I210/Alpine Ridge, behind switch ports without hotplug support.
An early first scan can miss those functions permanently. Actual tunnel and
physical-power/RUN/link checks follow the sleep; elapsed time is not link proof.
This is a single thread sleep, with no polling loop or periodic timer.

The separate MSI variant also checks actual PCIe-C RUN/link before dispatching
an endpoint ISR, because cable loss can precede the CM revocation callback.
It still has no endpoint/BME admission or true interrupt/work/child drain.
Ordinary, enumeration and MSI native SDKs and all six repository checks pass.
Neither change is in installed CIO23, and neither has executed against a dock
on Darwin. No separate hardware boot is justified for this currently uninvoked
PCI path. The next link investigation first checks whether firmware-log or
IOReport acknowledgments/mailbox transport are stalled; it will expose control
metadata only, with no firmware-byte extraction or guessed PHY programming.

### RTKit ACK and mailbox observation: observed CIO24

The installed firmware logs repeatedly stop after Phase2. The next observation
tests whether the native RTKit transport itself is stalled, rather than
assuming every missing firmware message describes a PHY failure. In CIO's
private staged RTKit copy, two exact syslog/IOReport ACK sites now record the
result after the existing real `apple_rtkit_send_message` call. Contents,
ordering, ACK policy and retries are unchanged. Per-engine counters record
attempts, failures and the first nonzero return; init messages preceding handle
publication are excluded. No message contents or firmware bytes are captured.

The existing startup/privileged snapshot now reports protocol, actual AP/IOP
power/running flags, syslog ring geometry/mapping state, those ACK results,
and actual mailbox IRQ registration/depth/taken/dispatch counters. IRQ counts
are atomic; registration/depth are read under the existing lock. These are
point-in-time facts, not handler completion or a drain acknowledgment. The
native owner retains/serializes the live M3 throughout the read. Three ordinary
M3 mailbox controls (A2I50/I2A80/IRQ_ENABLE48) are read through the actual bound
transport mapping; SEND/RECV message and IRQ ACK registers are excluded.

The transform verifies both exact source sites before changing the private
copy, leaves each real send call present once and leaves shared RTKit sources
unchanged. Ordinary, enumeration and MSI native SDKs and all six repository
checks pass. Final observation omits an unsynchronized rearm-pending flag;
its eight IRQ words are RX then TX, each registered/depth/taken/dispatched.
No new timer, periodic worker or NHI transaction is introduced. This code is
installed and qualified in CIO24; CIO23 remains the non-AVE rollback.
The candidate also carries the previously SDK-built PCI scan settling
delay, which is uninvoked without a peer, and excludes endpoint mapper/MSI/
driver admission. A normal Linux installation visit will read the same three
mailbox controls against its active authorized TS3, without extra boots or
firmware extraction.


CIO24 observed protocol12, AP/IOP power32, running/active and no crash/shutdown,
with an8-entry80-byte-message syslog ring in4096 mapped bytes. Five recorded
syslog ACK attempts returned0; IOReport attempts0 and errors0. Initial messages
before publication are excluded, so zero counts cannot prove no earlier ACKs.
RX IRQ registration/depth/taken/dispatched is1/0/8/8; TX is1/1/0/0. Both
startup and explicit snapshots agree. Native A2I50=`0x2bb01`, I2A80=`0x2dd01`
and IRQ_ENABLE48=`9` exactly match the same Linux visit; both FIFOs report
empty, not full. This weighs against a blocked mailbox/log ACK explanation;
it does not prove firmware progress or handler completion. Training still
reaches Phase2 only, both lanes7/remote0; actual approval returns ENODEV19
and independent PCI inventory exit2/functions0, with no native PCI host.

### ACIO1 default pinctrl gap: observed fix in CIO25

The pinned Linux T8103 DT assigns ACIO1 `pinctrl-0=<&acio1_pins>` with AOP
GPIO41/function1; `apple_gpio_pinmux_set` also enables input. The native path
had no corresponding operation. The actual saved J293 ADT identifies
`/arm-io/aop-gpio`, compatible `gpio,t8101`,42 pins, physical0x24a820000
span0x4000, matching Linux's AOP pinctrl. GPIO39 belongs to port0 and remains
untouched. The first candidate's actual measurements and link result follow below.

The candidate applies mask0x260/set0x220 only to GPIO41 at offset0xa4, under
the broker gate with an actual exclusive PREPARING TBT/USB4 lease and fresh
physical ACIO1-off proof, before PHY clocks and lease commit. It validates
the actual resource and refuses a bound GPIO bank, inaccessible register,
locked mismatching configuration or failed readback. All other fields are
preserved; DATA may change asynchronously. Already-correct settings need no
write, including a locked correct setting. No IRQ configuration, timer,
port0 operation, M3 control guess or USB4 status-bit force is added. As with
Linux default pinctrl, no speculative pin reset on release is introduced.
The next normal Linux installation visit will compare this one public control
under bound-pinctrl/ACIO and authorized-TS3 guards, without an extra reboot.
ASan/UBSan checks pass all mux/input combinations, unrelated-bit preservation,
locked mismatch/correct idempotence, inaccessible/stuck reads and asynchronous
DATA changes. The actual native USB SDK (`gdxa8is6…`) and all six repository
checks pass (274 Python tests,77 C groups,6 mutations). The57-module non-AVE
collection builds with no overlap; actual image inspection and the target
qualification described below subsequently pass.


CIO25's normal Linux visit measured GPIO41=`0x0087622f`, function1/input1/
lock0. Native startup measured `0x0087600e`→`0x0087622f`, error0; only
function bit5/input-enable bit9 and asynchronous DATA bit0 changed. The
firmware then logs Phase3 at10.625s, Connected10.628s, lane0 CL0 at11.236s
and lane1 CL0 at11.245s. Both actual lane states become UP2. M3 control
clears to0 as an outcome; no M3 clear was required. Primary PORT_CS18 now
matches Linux0x3710; PORT_CS19 remains6 versus Linuxe (configured bit3).
Both lane remotes remain absent and TS3 authorization returns ENODEV19;
PCI inventory is still empty. This fixes the observed low-speed/link-start
prerequisite, not discovery or a PCI tunnel. GPIO41 is also explicitly named
by the saved actual ADT ACIO1 `gpio-lstx` property. Quiet60/normal desktop
and all existing ABI/UID checks pass; no endpoint DMA or AVE is admitted.

The next source audit found an optional NVM framework mismatch: native
CONFIG_NVMEM is disabled, and the actual built `tb_nvm_add_active` returns
-EOPNOTSUPP102. Pinned `tb_switch_nvm_add` propagates that error after router
publication and removes the router. This is a concrete possible explanation
for a trained link with no retained remote, not yet a captured target return.
The native CM also has no newbus `bsddev` for its Linux device logging calls.
The next candidate will disable optional NVM export/upgrade explicitly at
initialization while retaining DROM/normal router setup, and route bounded
actual CM warnings/errors to IOLog. No fabricated event, forced rescan,
NVM write, firmware update or raw firmware capture is planned.


The optional NVM change now passes ordinary, enumeration and explicit MSI SDK
builds, plus all six repository checks (274 Python tests,77 C groups,6
mutations). Native `tb_switch_nvm_init` skips NVM state allocation/version/
export when CONFIG_NVMEM is disabled and marks upgrades disabled. It retains
the real DROM parse, port and link setup, publication/error/teardown logic;
no missing service is replaced with a successful registration. Actual built
`tb_switch_add` no longer calls NVM allocation/version functions. Retimer
NVM services remain unsupported and are not falsely registered.

Actual CM warnings/errors/notices/info now use a native IOLog sink rather
than a newbus device pointer. Its saturating64-message boot budget and
256-byte formatting bound introduce no timer or periodic work. Upstream
verbose debug/packet/firmware dumps remain disabled. Actual ARM64 symbols
confirm mapper/MSI absent ordinary and enumeration, present only in the
separate uninstalled MSI variant. The installed known-good image remains
CIO25; no NVM-policy hardware result or retained dock router is claimed yet.


### CIO26 partial discovery and IDA range audit, 15:45Z

CIO26/source383d9724a/capsule583f99d4c95e… installed15:39:59Z and booted
normally with standard desktop/Wi-Fi/NHI/RTKit/PHY/catalogue/UID/snapshot
checks. At11.407s the actual CM logged CalDigit TS3 Plus, vendor003d/device0011.
Both live physical ports report UP2, remote1, matching vendor/device and
remote-authorized0. Thus the dock router was really discovered and retained;
this is distinct from the prior host-only registry. The root authorization
request advanced0→1 but returned ENOMEM12. No native PCI preparation stage
was published, and independent PCI inventory was not run after the strict
qualification gate failed. No new quiet60 result exists for CIO26.

The later `failed to add NVM devices: -102` message matches retimer.c's
optional retimer NVM path, not switch.c's distinct message. Retimer NVM remains
unsupported and does not explain the retained TS3 router's authorization
failure. Existing standard rollback ran immediately: CIO25 capsulebe6732426e81…
was installed and verified through the normal installer; standard.next was
restored15:43:53Z and owned UART reaped15:43:54Z before lock release. Both
candidate and rollback contain57 non-AVE modules and no pdave argument.
CIO26's only changed code UUID was CIO; its other56 stayed fixed, including
CIO25's GPIO41 change. Failed qualification is preserved, not relabeled.

The next source audit found a separate ABI defect: pinned Linux
`ida_alloc_range` includes its maximum, but the frozen native wrapper passes
that maximum unchanged to `ida_simple_get`, whose end is exclusive. Thus a
fixed PCI HopID request8–8 cannot succeed. Upstream `tb_path_alloc` returns
NULL for any HopID allocation failure; `tb_tunnel_pci` reports that as ENOMEM,
so the top-level error does not by itself establish exhausted memory. Actual
CIO26 ARM64 code confirms the unadjusted `ida_simple_get` call. The individual
runtime HopID return was not captured, so this remains the next source-backed
candidate rather than a claimed hardware fix.

A private CM-only range adapter and boundary tests are drafted, covering
inclusive maxima, singleton8/0, signed-ID limits, invalid ranges and output
preservation. The shared LinuxKPI is unchanged. Actual HopID failure logs
will retain direction/minimum/maximum/port limit/error. These changes are
UNBUILT/UNTESTED/uncommitted and not installed. Builds and target probes are
deferred during FX's actual GPU stability hold starting15:45:10Z, until its
actual release; source preparation continues. The next cycle must also save
post-authorization dmesg before any rollback, since CIO26's ordinary dmesg
capture preceded the failed approval call. No fabricated hotplug/rescan or
forced status/credit setting is proposed.


### Explicit I210 mapper factory draft during the stability hold

Source now drafts a one-attempt administrator request in the explicit mapper
variant only. After actual host publication it finds a unique real I210,
refuses an existing iommu-parent, attaches/starts the concrete mapper, and
requires actual retained DART readiness. The mapper start already revalidates
all four xHCI plus one I210, every BME clear, real host ancestry/RID→SID0,
power/RUN/link and owned table pages. It binds the actual IOMapper object in
iommu-parent and verifies that the kernel's copyMapperForDevice returns that
same object; no fabricated phandle or registry-marker proof is used. A fresh
readiness query follows binding. Partial attempts retain/revoke/pin until
reboot; CM revocation synchronizes the retained mapper pointer under the existing lock;
it revokes/pins and does not establish work/IRQ/DMA drain.
There is no bus-master grant, endpoint personality or driver admission.

The command remains excluded from ordinary/enumeration-only builds by an
explicit compile-time feature. Its native CLI includes the same real UID
negative control as authorization. This factory/CLI is UNBUILT/UNTESTED,
uncommitted and not installed. The existing SDK builds preceded this draft;
no actual mapper, native PCI inventory, MSI/DMA or Ethernet claim follows.

I210 source audit also corrected a call-order assumption: iflib's bus-master
request appears later in the source file but occurs at attach entry, before
the registration/IRQ setup path it calls. PDFBSDEthernet makes another early
BME request. Both must stay refused or be removed in the private driver,
with a checked request only after actual mapper and registered/enabled/armed
MSI readiness. A vector allocation alone is insufficient. Native interface
publication happens after FreeBSD attach returns, which gives a possible
late admission point; em_reset/admin work still needs a DMA-enable audit.
No BME admission implementation or target change was made from this plan.


### Actual-resource external mailbox association draft

The DCP pre-probe path is drafted to select `/arm-io/dcp` or `/arm-io/dcpext`
from the consumer's actual named coproc resource, using T8103's two fixed
physical bases. Before binding, it resolves the native ADT mailbox and checks
ASC/mailbox base and span, ASC-v4 type and real send/receive AIC roles against
that selected instance. It refuses a mismatch instead of binding external
DCP to the hardcoded internal mailbox. The existing shared RTKit platform
initialization and per-mailbox IRQ/resource records remain in use; no firmware
bytes, power change, crossbar operation, output publication or new poll is added.

Host metadata tests are drafted for both resources and wrong base/span/IRQ,
overflow and unchanged-on-error cases. This is UNBUILT/UNTESTED/uncommitted,
not installed and not a native external DCP bind. Future hardware use still
requires actual external power/topology, mirrored DART integration and DPTX/
DP-IN/shared-TBT-clock providers. The pinned tunnel path keeps ATC in TBT mode;
it must not fall through to a direct-DP PHY mode request. Its pixel-clock
callback needs the live CIO-owned lease and actual DP tunnel generation.
The next CIO kernel continues to freeze the previous DCP artifact and excludes
this independent display draft.


The next CM draft also logs actual unreachable path endpoints and PCI credit
shortage values, because those refusals are otherwise collapsed to the same
NULL/ENOMEM result. No path walk, credit limit, allocation/cleanup or status
programming changes accompany these diagnostics. They remain unbuilt during
the stability hold. A source-only K5 audit confirms the current DRM atomic
core already writes the real CRTC ID into flip events and IOMFB keeps its
swap state per DCP; this is not a two-output hardware result or a reason to
change the shared DRM core.


I210 admission must also account for actual ancestor PCI bridge bus mastering:
the current inventory profile strips BME everywhere. That forwarding chain
must be derived from real PCI topology and verified before permitting the
I210, with every xHCI endpoint still blocked. No bridge/endpoint BME grant
is implemented or executed by the mapper draft. The next hardware priority
remains the real PCI inventory after the IDA correction.


Ethernet userland audit: pd-dhcp already accepts an interface name and uses
that device's BPF path; pd-netd is Wi-Fi-specific. Its unmodified lease daemon
is unsuitable as a second-interface supervisor: deconfigure deletes the
lease gateway's default route even if netsetup had found the pre-existing
Wi-Fi route (EEXIST). A dock-specific interface-only configuration/cleanup
path or actual scoped-route ownership is needed before persistent Ethernet
and cable cycles. No DHCP exchange, address, route, resolver or target
network configuration changed during this source audit.


### 2026-11-15 16:22Z: source validation after the stability hold

The private CM inclusive IDA correction is committed as `16293a7f0` and
the explicitly separate I210 mapper factory as `5842e5b46`. The previously
drafted source is now SDK-built: ordinary, inventory-only, mapper-only,
and mapper+MSI CIO variants, the administrator mapper CLI, and full DCP.
TB ASAN/UBSAN tests include 98,432 IDA range cases; DP ASAN/UBSAN includes
the internal/external mailbox association cases. All six required repository
checks pass (274 Python tests, 77 C groups, six mutation detections). These
results supersede the earlier UNBUILT/UNTESTED labels for these source drafts;
none is installed or a hardware mapper/external-display result.

CIO27 will carry the private CM range correction and bounded refusal
diagnostics only, with the qualified CIO25 USB/Type-C/DCP artifacts frozen.
The mapper factory is absent from that profile. CIO25 remains installed
without AVE or a pdave argument, following CIO26's failed authorization and
normal rollback. Hardware retry and post-authorization diagnostics are next.


### 2026-11-15 16:29Z: first dock PCI inventory observed on Darwin

CIO27 (`efd06345b`, capsule
`1925387085058fc4e4c542cc72cf0470a99b4215bc393d5cd24426fd4a963c24`)
is installed and qualified. Root TS3 authorization returned success, count
0 to 1, error 0. The actual PCIe-C cold startup, link, one-second endpoint
settle, endpoint-DART blocking and native PCI host publication completed.
The independent native inventory returned success with 12 PCI functions:
seven bridges, four xHCI controllers (two FL1100, one ASM1142 and one Intel
Alpine Ridge), and one Intel I210 `8086:1533`. Darwin assigned the I210 bus3;
Linux assigns bus6, so downstream code must use actual ancestry and identity.
This satisfies dock stage(a), registry enumeration. No endpoint BME, driver
admission, DMA, Ethernet packet, keyboard input or external-display success
is claimed. The private inclusive HopID correction resolves CIO26's
authorization ENOMEM. DP tunnel activation attempts still fail; external
DCP/PHY integration is pending. Optional retimer NVM export remains unsupported.

Actual collection checks: 57 modules, only CIO code UUID changed, other56
fixed including CIO25 GPIO41/Type-C/DCP, four existing PCI personalities,
no endpoint mapper/MSI, no AVE module or pdave argument, unchanged605-byte
configuration and boot.bin, zero segment overlaps. Standard.next/inject and
Wi-Fi restored; UART reader reaped before lock release at16:29:05Z.
The qualified rollback is CIO25. No AVE builds, probes or extraction occurred.

The quiet60 result is kernel2.22915% of one CPU and host97.95542% idle,
NHI24 interrupts (12TX0+12RX0 including explicit-query boundaries; other22
lines zero). Compositor3.24709% (cleanup1.96448%, main0.87197%, HID0.40798%);
warm50M work83.676–83.682ms, 100k syscalls14.476ms. There is no NHI storm
or reproduced fixed-work slowdown; the reported AppKit timing regression
remains unexplained. Next: real I210 mapper/MSI/checked driver admission,
Ethernet link and interface-owned DHCP, then the attached keyboard.


### 2026-11-15 16:42Z: MSI variant failed, recovered; setup correction built

CIO28 installed capsule
`17bc66193140c2ec8d111fc27d025312063b85196fa82a8d7bf20d0c4754c436`,
reached the real router and activated the PCI tunnel, then failed native host
startup with ENODEV19 before enumeration. The mapper request and independent
PCI inventory were NOT RUN. Post-authorization dmesg/registry and the
failure-before-recovery capture are preserved privately. Normal rollback
restored the exact qualified non-AVE CIO27 capsule at16:40:07Z; UART was
reaped and locks released16:40:09Z. The recovery boot has not repeated
authorization; the positive12-function inventory belongs to the earlier
qualified CIO27 trial. No endpoint BME or DMA was enabled.

Source establishes a missing setup step: the installed cold reset explicitly
zeros MSI config/base/address, while the MSI variant merely required their
already-configured values. It did not restore them. The new helper follows
the pinned Linux mask/ack/address/base/enable sequence, checks native retained
ownership and physical power/RUN/link before every operation, then verifies
all three configuration words before constructing native AIC parents.
Before/after words, completion counts and errors are published for the next
actual boot; CIO28 did not capture those register values, so they are not
claimed as observed. Host sanitizer tests cover all12 I/O refusals plus
inaccessible and mismatched readbacks. Native enum and MSI SDKs and all six
repository checks pass. This correction is built, not installed or hardware
qualified. Next retry still grants no BME and adds no endpoint driver.

The first CIO28 collection was rejected for0x2ab4 overlap before installation.
Removing CIO's local symbols alone recovered space: all17 non-debug section
addresses, sizes and contents matched the input exactly, other56 code UUIDs
were fixed, and the final installed trial had zero overlaps. No kernel
address change or protected code change was used. AVE remains paused.


### 2026-11-15 16:56Z: real I210 mapper and MSI parent setup observed

CIO29/source `4c99f7cce`, capsule
`a149e487218ba3e37b2300c9c0762c837de5a010a3d022ad21df836085a33d31`,
is installed and qualified. The actual MSI words were all zero before setup
and config/base/address became `0x51 / 0 / 0xfffff000` afterward, with both
counts3/error0. This confirms the source-identified missing restore step
behind CIO28's host failure. Native parent setup completed with32 masked
AIC lines beginning818. TS3 authorization and independent inventory again
returned success:12 functions/four xHCI/one I210.

The administrator mapper request returned success/count0 to1/error0; its
real UID-denial control left count0 unchanged. The owned native SID0 domain
setup returned error0, bound the actual IOMapper object to the actual I210,
verified native lookup identity and freshly checked owned TTBR/TCR/gate and
physical power/link. No endpoint BME or driver was admitted, and there is
no Ethernet packet, IRQ delivery, USB input or external display result.

The collection has57 modules/no AVE/no pdave, only CIO's code UUID changed,
other56 artifacts fixed, unchanged605-byte configuration and boot.bin, four
PCI personalities and zero overlaps. Standard.next/inject/Wi-Fi restored;
UART reaped before release16:50:56Z. CIO27 remains the qualified rollback.
Quiet60: kernel2.25162% one CPU/host98.25875% idle/NHI24=12TX0+12RX0,
other22 zero; compositor3.68258% (cleanup2.31785/main0.93868/HID0.42307).
The first50M CPU-work sample was157.034ms with156.951ms CPU time; three
warm samples were83.677–83.687ms.100k syscalls14.682ms. This cold/warm
anomaly is retained as a possible scheduling/clock clue, not diagnosed or
fixed. The reported AppKit slowdown remains unresolved.

A separate I210 network admission and private FreeBSD driver variant now
compile: exact I210 personality only; real mapper required before attach;
three early BME requests removed (native wrapper, iflib entry and PCI BAR
helper); late admission requires the actual armed MSI route, owned mapper,
complete blocked inventory and live physical link. It derives and enables
only the I210's real bridge ancestry and endpoint, keeping four xHCI blocked.
Partial/ambiguous startup retains IRQ/context/backing until reboot; this is
not hot-remove support. Interface-only DHCP tools `8d6775a26` also compile,
with no shared Wi-Fi tool changes. These network variants remain uninstalled
and await the first link/DHCP hardware test.


### 2026-11-15: CIO30 driver match reached; BAR resource publication missing

CIO30/source `737adc701`, capsule
`aa1572a6efe1c43906c8376d363250cd8f43e5429a04540741bd1d7638d10b06`,
booted without a panic. Real TS3 authorization, independent12-function PCI
inventory, MSI setup and actual I210 mapper binding passed again. The exact
private PDFBSDem driver matched, then refused startup with
`PDCIOI210StartError=BAR0-resource-refused`, BAR0 physical0/bytes0. Bus
mastering was not admitted; no Ethernet interface, link, DHCP packet or USB
input was observed. Quiet60 was not run after this failure. Captures retain
actual post-authorization, mapper, driver and failure-before-recovery states.

Pinned IOPCIFamily source explains this refusal: `isDTNub()` is always true,
`getNubAddressing()` is a stub, and `getDTNubAddressing()` returns success
without constructing device memory when `assigned-addresses` is absent.
The dynamic dock nubs have enumeration size tuples in `reg` but no assigned
address property. The next correction publishes actual memory descriptors
from live BAR values and those native sizes, translated through the exact
apciec1 address windows; it must refuse unassigned or out-of-window BARs.
This draft is not yet built or observed and performs no BAR/config writes.

Normal recovery restored qualified non-AVE CIO29 at17:49:01Z; its reader
was reaped before lock release17:49:03Z. Current recovery boot has not
repeated authorization. The installed57-module manifest contains no AVE
kext, and its unchanged605-byte configuration has no pdave argument.
AVE remains paused: no AVE build, boot, probe or extraction.

CIO30 also validates the collection capacity solution at boot:58 modules
(old57 plus PDFBSDem), only the old CIO code UUID changed, all other56 fixed,
all57 input bundles retain non-debug section bytes/addresses/sizes, unchanged
boot.bin/configuration, zero overlaps and five actual PCI personalities.
Prelink metadata was reduced160790 to137330 bytes solely by removing XML
structural whitespace: parsed values and every other file byte are identical.
Earlier rejected capacity/tool attempts remain failures.


### 2026-11-15 18:03Z: BAR correction built; runner preparation failed

Source `9b0936512` compiles in native network and enumeration SDKs and passes
all host sanitizers and six repository checks. Its58-module collection has
zero overlaps, no AVE/pdave, unchanged605-byte configuration and boot.bin;
only the old CIO code UUID changes. Capsule
`88fa3687762365c0af29552cdee4fe7f02da8ee70e0ec5dbeafab299854b6276`
has **not** reached hardware: CIO31 rebooted to Linux, then the runner failed
on a missing local auxiliary capture file BEFORE installation. Original
failure is preserved. Normal recovery restored the exact qualified CIO29
at18:03:17Z and reaped the reader before release18:03:18Z. The retry uses
the unchanged capsule; all21 local upload inputs are now read and hashed
before any target action. This preparation failure is not a BAR-fix test.

An independent read-only placement probe on CIO29 found defaultQoS work on
E cores0–2/cluster0 taking121.17ms warm, versus83.68ms on P cores4–6/cluster1
after interactiveQoS returned success. Cold runs after1s sleeps were152–175ms
E and113–156ms P. CPU time nearly equals wall time, so this is not primarily
descheduling. This establishes scheduling/clock effects in this probe, not
the cause or a fix for the reported AppKit regression. The earlier baseline
A/B and steady kernel/IRQ measurements remain unchanged.


### 2026-11-15: CIO32 exposed real unassigned BARs and aperture starvation

The unchanged BAR-descriptor capsule reached hardware as CIO32: boot without
panic, real TS3 authorization,12-function PCI inventory, MSI setup and actual
I210 mapper binding passed. The six live I210 BAR words were exactly
`[0,0,1,0,0,0]`; resource publication refused ENXIO6 and the mapper command
reported that actual error. No endpoint driver retry, BME, interface, link,
DHCP or USB input was admitted. Normal recovery restored qualified non-AVE
CIO29 at18:10:36Z; reader reaped before release18:10:38Z. The candidate
therefore disproved the assumption that native enumeration assigned BARs.

Native trace shows TB prefetchable windows taking PCI00100000..7fffffff
while ordinary memory windows stay zero. Our host advertised all three
apciec1 windows as ordinary memory; pinned Linux DT explicitly marks the
40000000..7fffffff and580000000..5ffffffff windows prefetchable. Native
`bridgeGetRange(PrefetchMemory)` falls back to the ordinary list when no
prefetch list exists, letting TB maximize its prefetch window over the whole
merged low aperture and starving the non-prefetchable I210 BARs. The source
correction uses `addBridgePrefetchableMemoryRange` for those exact two ranges,
keeping00100000..3fffffff for ordinary memory. No guessed BAR address or
global allocator flag is introduced.

The host regression compiles the actual frozen SDK's IOPCIRange implementation
with sanitizers: it reproduces the old full-aperture prefetch allocation and
failed ordinary-memory allocation, then derives this host's declarations and
verifies separate prefetch placement plus nested bridge memory and I210
BAR0/MSI-X allocations. This is actual allocator host execution, not PCI
configuration or hardware qualification of the correction.

Separate, uninstalled source preparation now SDK-compiles: a dock xHCI
IODMACommand buffer helper (object only, no controller binding/shared-domain
registration yet) and native raw-HID select/FIONREAD readiness. The latter
would let DESK replace250Hz idle HID polling after real select tests and a
consumer change; it establishes no idle-regression fix. Neither enters the
next dock collection.


### 2026-11-15 18:25Z: real I210 driver and checked bus mastering reached

CIO33/source `04794e6a2`, capsule
`c201fe77abaa0284303660b4f75e03b0d75868ecc1613d0921d441ce643802e2`,
booted without a panic and repeated real TS3 authorization/12-function
inventory/MSI setup. The corrected native aperture types produced BAR0
PCI00100000, BAR3 PCI00180000 and actual BAR0 memory descriptor
CPU600100000/size80000. The existing descriptor passed checked translation;
resource-error0. This confirms the source/host-test aperture diagnosis.

The actual IOMapper bound, the native MSI route armed, checked BME admission
returned error0 for only I210 and its three real ancestor bridges, and the
FreeBSD PDFBSDem driver attached. All four USB controllers remain blocked.
A real IOEthernetInterface is present under the actual driver, but has
IOInterfaceState0 and no BSD Name. The runner failed this requirement at
18:23:00Z BEFORE interface-up, link/DHCP or quiet60. No Ethernet packet or
actual MSI delivery is qualified. Normal rollback restored non-AVE CIO29
at18:25:35Z; UART reaped before release18:25:36Z.

Source explains the next missing step: PDFBSDEthernet publishes its interface
but issues no IONetworkStack registration request. This system's Wi-Fi
driver explicitly sends RegisterInterfaceAll because InterfaceNamer is absent.
A userland helper is being prepared to verify unique actual I210 identity,
BME admission and Ethernet child, then send the same normal registration
request and require a real non-Wi-Fi BSD name. It chooses no fixed unit and
sets no interface-up/address/route/resolver/DHCP state. The next trial can
reuse the exact non-AVE CIO33 capsule, without another collection build.


### 2026-11-15: native I210 en1 naming observed; command inventory corrected

CIO34 used the byte-identical CIO33 capsule. Real authorization, PCI
inventory, BARs, mapper/MSI/BME and driver attachment repeated. The new
helper passed actual UID65534 denial, then registered the actual I210 as
`en1`. The harness immediately failed rc127 in its following inspection
command: `ifconfig` is absent. Interface-up and DHCP were NOT RUN. This is
a runner preparation failure, not a demonstrated link or packet failure.
Normal recovery restored qualified non-AVE CIO29 at18:36:27Z and reaped the
reader18:36:29Z. No AVE code/argument was installed or probed.

A read-only target inventory confirms PATH already includes sbin directories
and ifconfig/netstat/route are absent from the standard locations. The new
`pd-dock-netstatus` uses native getifaddrs, media/MTU ioctls, NET_RT_IFLIST2
64-bit counters and NET_RT_DUMP2 IPv4 route records. It follows XNU's
ROUNDUP32 sockaddr layout, handles compressed masks, bounds messages and
never prints link-layer addresses. Native SDK compilation and the actual M1
Wi-Fi control pass (real address/counters/media and15IPv4 routes). All six
repository checks pass. This read-only tool sets no network configuration.

The next runner removes the absent commands and requires the real status
binary hash and live Wi-Fi query BEFORE any reboot/cycle, under normal locks.
A failed preflight exits without installing or starting a recovery reboot.
The actual I210 DHCP attempt still awaits this corrected run, using the same
non-AVE CIO33 kernel rather than another collection build.

### Dock USB requester/resource factory — source only, 2026-11-15

The detached factory selects the Keychron branch's unique FL1100 by its
nearest actual Alpine Ridge downstream bridge slot0, without assuming Linux
bus numbers. After the I210 is active it registers that actual USB requester
with the existing shared SID0 mapper, binds the native mapper object, checks
native lookup/readiness, publishes live 32/64-bit BAR CPU descriptors, and
allocates one MSI through the retained native per-port controller. It does
not reset the DART root or allocator. No command calls this factory yet;
there is no USB driver matching, BME grant, controller start or hardware claim.

The first SDK build failed because resolveMSIInterrupts is private in the
actual IOPCIFamily. That failure is preserved. The corrected source uses the
public allocateDeviceInterrupts method called by that family, checks actual
MSI type, and refuses an already allocated interrupt. Native ARM64 SDK
xqc8d9di0pw55w1sla20dy0xwmfm6970 and the updated host sanitizer suite pass;
all six required repository checks pass. These checks establish compilation
and host logic only. The factory is excluded from frozen CIO35, which still
reuses CIO33's kernel and tests I210 DHCP with the native status tool.

### Dock USB access and late DMA admission — source only, 2026-11-15

The detached factory now also binds a retained access identity for the selected
FL1100. Its guard uses the same live physical power/RUN/link predicate as
I210. A separate late USB DMA request requires the actual bound shared mapper,
armed native MSI, live endpoint/bridge identity, all four expected dock USB
controllers, and the active I210 with exactly its existing bridge grants.
It unions the selected USB ancestry with the existing grants (bounded16),
retains new identities before writes, and enables bridges root-to-leaf before
the endpoint. It preserves I210 grants and keeps the other three xHCI devices
blocked. Failed new writes revoke configuration/interrupt admission and pin
ownership; this is quarantine, never a successful DMA drain.

Native SDK 0w40v8nrmwfl93pyi3ah89sj3v740fhc passes. The actual two new admission
methods run under ASan/UBSan in host-test/usb-admission.py: identity/inventory,
mapper/MSI/power/IRQ-context refusals, grant preservation, write ordering,
repeat refusal, and new bridge/endpoint write failures. The initial fixture
incorrectly expected a failed rewrite of already enabled BME to clear it;
that failed receipt remains and the corrected fixture explicitly faults new
grants. All six repository checks pass. No driver calls the late request yet;
no USB controller was started and CIO35 excludes this source entirely.

### CIO35 first interface-up panic and IRQ-safe fix, 2026-11-15

CIO35 reused CIO33's exact non-AVE kernel. TS3 authorisation,12 PCI functions,
actual mapper/MSI/BME, FreeBSD I210 attach and root interface naming repeated.
Native status observed en1 down/not running, link inactive, zero packets.
The first interface-up request at19:04:01Z caused a kernel panic before DHCP:
"Attempt to take mutex ... with preemption disabled (2)". The original UART
and stackshot remain private; this is a real failed hardware check.

Exact collection layout plus the unstripped matching CIO SDK symbolise the
chain as MSI primaryInterrupt -> ready -> interruptPower -> enumeration
tunnel/power -> CM power -> PDCIODart::healthy. healthy took IOLockLock,
which is a sleeping mutex and cannot run in the primary interrupt context.
The reported module base is its TEXT_EXEC base; SDK symbolisation includes
that segment's actual offset. No DMA/link/packet success follows from this
first interrupt, and DHCP was never attempted.

The fix publishes DART ready/poison transitions atomically and reads the
cached health predicate without a mutex or MMIO. Suspend revokes readiness
before disabling translation. It preserves the existing physical PMGR,
RUN/link, CM fault and generation checks through the rest of the chain.
Native SDK mh7ph2a8qqdb5pw5zirrrhd2sfnzpbwm was built from CIO35's exact frozen
source tree with only the DART cpp/header delta; USB/DP/HID preparations are
excluded. Actual healthy/suspend methods pass ASan/UBSan host fixtures for
hard-IRQ calls, atomic poison, busy refusal, and revoke-before-disable on
success/failure. All six repository checks pass; a new hardware trial is
still required. This panic is separate from the earlier unresolved idle-load
and AppKit timing report.

Qualified non-AVE CIO29 was restored19:06:17Z; UART was reaped19:06:18Z before
lock release. Its directory-only inject check passed, but the actual process
was the daily compositor: that was insufficient restoration. DESK6 restored
the real .next process at19:13Z and again during its19:25Z install. CIO36
adds a strict actual-process/socket/inject preflight and conditional normal
.next relaunch before releasing its recovery lock. Default boot remains Linux. No AVE module, boot argument, probe or extraction was added.

### Dedicated FL1100 controller preparation — uninstalled, 2026-11-15

A separate SDK-only driver variant now connects the real PCIe-C mapper to
all13 xHCI allocation sites, actual native BAR posted-write mappings, retained
per-MMIO physical guards and checked MSI-before-late-BME startup. Extended
capabilities and BIOS handoff had raw accesses outside the inline register
helpers; these are guarded too. Any inaccessible/all-ones read closes
admission and stops workers, and cannot be mistaken for a halted controller.
The generic BAR sizing/address fallback and client-page DMA path are excluded.

Every ring/payload allocation stays in a retained controller ledger until
real whole-controller halt/reset and explicit DMA completion/clear. Ordinary
release sites therefore cannot free backing after a control timeout. BOT
cache ownership uses explicit failed-transfer state instead of native retain
counts, which include command/mapper ownership. Initial qualification retains
all mappings while running: bounded4096 ledger entries,2048 shared-mapper
slots and256MiB IOVA; allocation exhaustion refuses work. It is not unlimited
storage operation. Read-only storage rejection remains in the dedicated patch.

Native ARM64 SDK7wckrvc74sln33c48bhb36f5a8zcgr5y and all six repository checks
pass. Actual transformed methods run under ASan/UBSan for guards, protocol-cap
parsing, BIOS handoff, inaccessible status, active worker/operation and reset
timeouts, and partial mapping-retirement failure. No real IRQ/work joining,
hardware halt, USB enumeration, physical typing or storage result is claimed.
Factory/catalogue command wiring remains; this driver is excluded from CIO36.

### Opt-in FL1100 profile and administrator request — source only

The separate build-pci-usb-kext.nix profile now accepts exactly one native
FL1100 PCI personality and exposes pd-cio-prepare-dock-usb through the existing
administrator-only serialised command gate. It requires the active I210,
selects the actual keyboard branch, binds mapper/resources/MSI/access, and
retries matching for that endpoint. The driver then requests checked late BME.
The request counter and binding checkpoint are historical request evidence;
CLI success alone cannot establish controller startup, USB/HID enumeration or
physical input. Default/network-only profiles continue rejecting PCI USB.

Native CIO SDK cr0yv2h4lnn4wrg6ygh5vk3xba589f7s and command SDK
xf37dpx3f4plpiflx7ch2mjjf1s97hrj pass. Actual catalogueSafe runs under
ASan/UBSan in both build modes: network-only rejects the USB personality;
the opt-in profile requires exactly one correct FL1100/tunnel match and
refuses broad, duplicate, wrong-ID or extra personalities. All six repository
checks pass. No collection selects this pair; CIO36 remains frozen with only
the interrupt-context health fix, and real Ethernet qualification comes first.

### CIO36: actual 1 Gb/s link; DHCP still fails, 2026-11-15

The atomic-health kernel survived interface-up without CIO35's mutex panic.
Native status observed en1 UP/running, active link, and IORegistry reported
IOLinkSpeed1000000000. This is real negotiated link evidence, not throughput.
The DHCP client made four initial BPF writes that returned EPERM, then
accepted DISCOVER writes on attempts5/6, but received no OFFER/lease. Counters
reported zero packets and2048 output bytes; they do not establish completed
hardware transmission. Captured stdout and stderr were concatenated, so their
file order must not be mistaken for chronology. No IPv4 was applied.

Qualified non-AVE CIO29 was restored19:34:52Z and UART reaped19:34:53Z. This
time the actual .next process, socket and inject directory were all checked.
The earlier idle/AppKit report remains unresolved; the DHCP failure prevented
the queued quiet60 measurement. No USB/display/hotplug success is inferred.

### I210 coherent rings and DMA copy ordering — native build, rerun pending

Source inspection found that bus_dmamem_alloc marks coherent ring maps
BUFFER_PROHIBITED. The compatibility sync function returned before native
mapper synchronisation for those and BUFFER_NONE maps, while the dedicated
adapter had copied their contents into separate owned backing. CPU/device
therefore did not share the coherent ring object. A second defect published
the old Haiku bounce bytes to native backing before the CPU->Haiku copy.

The private Ethernet staging fix rounds native contiguous allocations to
whole16KiB owned pages and maps the exact retained allocator IOBuffer object
for eligible coherent memory. The real IOMapper/IODMACommand remains required;
there is no physical-address DMA fallback. Unowned packet fragments keep
distinct owned bounces. Every map retains its actual backing, including after
caller release, and ambiguous completion/clear still pins it. Coherent maps
now forward native sync calls; bounced writes publish CPU->Haiku->native in
that order. Cumulative direct/bounce map counters report successful mapping
setup only, never packet/DMA completion.

Executing the actual old sync functions reproduces four FAIL results (native
sync skipped twice, stale second-stage copy twice). These failures remain in
the receipt. Corrected functions and the exact retained whole-page lookup
pass ASan/UBSan host cases. Native SDK3nr6p1v95bbgiqv1z1c5p4i0lskqq3h8 and
all six fresh repository checks pass; only pd_fbsd_os.cpp and bus_dma.cpp
differ in the native compatibility sources. Initial transformation/fixture
and diagnostic-counter compilation failures are retained and corrected. The
next trial keeps frozen CIO36 and replaces only the private Ethernet bundle;
no hardware packet fix or completed DHCP milestone is claimed yet.


### CIO37NET: native coherent mappings executed; DHCP still fails

The Ethernet-only candidate retained the exact CIO36 module and changed only
PDFBSDem's coherent allocation and DMA synchronization seam. On the M1,
router authorization, the twelve-function native inventory, mapper/MSI/BME,
and en1 UP/running with a 1 Gb/s link succeeded. There was no mutex panic.
Two direct coherent mappings and 258 owned bounce mappings were published by
the actual successful map paths. Those counters establish allocation paths,
not NIC descriptor consumption or packet completion. DHCP received no OFFER
or ACK and applied no IPv4 address. The first four BPF writes returned EPERM;
tries five and six were accepted. BSD packet counters remained zero, with
2048 output bytes; acceptance and byte accounting are not wire evidence.
The failed lease gate prevented the subsequent quiet sixty-second observation.
Private before-recovery captures are preserved; the normal CIO29 rollback
completed at20:00:11Z with the actual.next/socket/inject checks passing;
the UART reader was stopped before unlock at20:00:12Z. Ethernet remains unqualified.

USB composition review also found a pre-hardware defect: the detached PCI USB
SDK inherited build-xhci rather than the combined build-host bundle, losing
the PDAppleUSB shared PHY broker, and replaced the broker's personality.
That SDK was never installed. Preparation now preserves the broker and prepared
direct-host personalities alongside exact FL1100 matching, requires actual
broker source/CMake inclusion before mutation, and limits PCI access and late
BME checks to the PCI branch. The actual-source composition fixture preserves
all other files and rejects missing broker code/personality or direct-host
personality before mutation. Native rebuild and hardware qualification remain
pending; this is not USB input or storage evidence.


### Preserved USB broker and bounded Ethernet diagnosis, source/native only

The corrected dock USB variant now native-builds from the exact installed
combined USB source. Only controller/header, plist/CMake and read-only storage
changes plus the owned DMA helper differ; all broker/ATC/PMGR/common files are
byte-identical. Its composition and actual transformed controller fixtures
pass, as do all six required repository checks. Commit5ce5045d9 retains the
broker and prepared direct-host personalities. This bundle is uninstalled;
there is still no USB enumeration or Keychron input evidence.

An opt-in Ethernet trace SDK native-builds with eight-record saturating
budgets at existing TX encapsulation, doorbell, completion-query and RX-query
sites. It reads guarded, non-destructive queue/control registers and adds no
MMIO writes, timer or worker. Native source differs from CIO37NET only in
igb_txrx.c and the diagnostic budget header. The actual concurrent budget
fixture passes ASan/UBSan; the candidate collection changes only PDFBSDem's
code UUID among58 executable modules, with zero overlaps. These are build
results, not a hardware trace or packet result. The purpose of the next trial
is to distinguish native packet submission, real tail/head advancement and
CPU-visible completion before choosing another functional change.


### PCIe-C CPU cache mapping follow-up, uninstalled candidate

Pinned Linux T8103 DT marks the internal PCIe controller dma-coherent but
omits that declaration on apciec1. This is a source clue, not a readout of the
running Linux device's DMA flags. The exact installed kernel source was located
from its native derivation: IODMACommand, IOBufferMemoryDescriptor,
IOMemoryDescriptor, ARM pmap and proc_reg sources match the inspected copies
byte for byte. IODMACommand's mapped synchronize path walks copy buffers or
returns for a cursor; it does not perform CPU cache maintenance. Thus retaining
native synchronize calls alone does not establish noncoherent DMA visibility.
The cached shared-ring and bounce mappings remain a plausible packet failure
cause; the bounded CIO38TRACE control is unchanged and has not run yet.

A separate private Ethernet SDK requests kIOMapWriteCombineCache for both
allocator-owned coherent storage and native bounce buffers. In this frozen
XNU pmap, WC DRAM uses normal noncacheable memory, permitting packet byte
copies; inhibit-cache DRAM uses device memory. The candidate keeps complete
owned pages, real IOMapper/IODMACommand translation and all existing refusal
and retention paths, and adds explicit DSB SY ordering around DMA copies.
Its property labels the requested CPU cache policy, not a measured PTE mode.
Only pd_fbsd_os.cpp differs from the trace SDK source. The native SDK and
actual allocator fixtures for flags, whole-page zeroing, identity and failure
cleanup pass; all six repository checks pass. No collection, boot, page-table
readout or Ethernet fix is claimed. The initial source-shape refusal is retained
in private receipts. This candidate is excluded from the pending trace boot.


### PCI USB normal-NC buffers and event waits, native SDK only

The prepared FL1100 bundle now requests normal noncacheable CPU mappings for
all actual owned ring/payload buffers through PDPCIUSBDMABuffer. Native
IOMapper/IODMACommand translation, full-page ownership, zero padding and
ambiguous failure retention remain intact. The actual allocation method is
exercised with explicit host IOKit fixtures: native mapper selection, mapping
flags, nonidentity IOVA and failed preparation retention pass ASan/UBSan.
This is requested mapping policy, not hardware cache or USB DMA evidence.

The admitted PCI branch also uses the real MSI event-generation wait with a
one-second recovery backstop instead of two-millisecond fallback wakeups;
its non-isochronous completion worker uses the same slow backstop. Default
PCI interrupt reads wait up to one second, return immediately on actual
completion, and honor a caller's explicit completion timeout. The existing
HID stop path still joins its reader before aborting the persistent TD, so
that join can include the longer default read. Direct prepared-host timing
and active isochronous service timing retain their existing branches.
Actual sleep-function fixtures verify the slow PCI wait, caller deadline
clamping, changed-generation refusal to sleep and original non-PCI wait.

The combined native SDK builds, broker/ATC/PMGR/common source bytes still
match the installed combined USB bundle, and actual allocator/controller/
composition fixtures plus all six repository checks pass. This supersedes
the earlier cached/two-millisecond detached USB candidates. No USB collection
or boot has occurred. These uninstalled changes cannot explain the currently
reported idle-load regression; that cause remains unresolved. CIO38TRACE
remains unchanged and waits behind TERM8's guarded core pipeline.


### Dock DP integration: preserve TBT mode and distinguish route indices

Pinned dptxep.c's activate callback explicitly keeps the ATC PHY in TBT/USB4
mode for a tunnel. It wakes DP-IN through dcp_tunnel_dpin_activate only when
DCP sends ACTIVATE; waking it earlier is documented there to hang the machine.
Thus a dock DP trial must not switch the shared physical PHY to direct DP
alt mode. The matching callback must carry the real CM generation and queue
DCP/provider work outside the Linux domain lock, as the prepared native NHI
callback contract already requires. Those owner callbacks remain uninstalled.

The reference's dcp_dptx_connect distinguishes upstream RemotePort core from
downstream DFP address: direct DPPHY=0, DPIN0=1, DPIN1=2. Port1's physical
ATC/DPTX selector is1; this is not permission to substitute1 for the actual
root DP-IN index or CM port number. Existing native dp_index derives the
index from the real root switch's DP-IN order. The J293 DT supplies dcpext
routes for both Type-C ports, phy/mux arrays and apple,typec-dptx-phys=<0 1>;
only the owner's port1 is the present target. The prepared crossbar program
and resource planner must consume these distinct values at their actual APIs.

The old DCP README's claim that J293 has only one DCP was corrected: only the
internal processor is currently published by SiliconForest. Physical dcpext
exists; its T8103 IOMFB path is distinct from later-chip dcpext_scanout stubs.
No route owner, external firmware boot, HPD, link training, EDID, connector or
second output is established by this source review. DESK K1–K13 still waits
for the real kernel output. The immediate hardware path remains I210 DHCP,
then the attached dock keyboard, before activating the external pipeline.


### Discarded owner-overlap sample: raw observations only

DISCARDED after the owner-activity notice: this sample overlaps the owner's20:15–20:25Z physical use. Its values below are raw observations, not quiet
or performance qualification; a fresh repeat is required. No input was injected
by CODEX-TB during it. A GUI/bench-guarded61-second sample on CIO29 with actual desk6.next completed
at20:22:20Z without reboot, input, capture, installation or session change.
Kernel thread CPU was2.121% of oneCPU and aggregate host idle98.125%; an unnamed
thread contributed1.494%, SPI HID0.332%, and other individual threads below
0.068%. Earlier stackshots associated the dominant unnamed wait with
PDAppleNVMeRW on both k2 and USB04; this run did not take a fresh stackshot.
NHI counters advanced24 in total at the two explicit query boundaries,
with no observed recurring interrupt storm. The desktop used3.605% of oneCPU:
shared-memory cleanup2.259%, main0.936%, HID reader0.404%, about428 task wakeups
per second. The fixed mix took165.375ms cold and83.838/83.682/83.677ms warm.
These owner-overlap values cannot qualify a quiet comparison or establish
the cause of the reported AppKit slowdown. That finding
remains unresolved. TERM8's c12 framework installation acquired GUI after this
sample released it, so later app timing comparisons need that version boundary.


### Next boot order revised before any hardware action

With the normal-NC Ethernet SDK, allocator fixtures and exact collection/input
checks complete, the pending diagnostic-only CIO38TRACE boot was superseded
by CIO39NC. The sole waiting wrapper was stopped while still blocked on the
shared core lock, before GUI/bench acquisition or target preflight. No CIO38
kernel was installed or booted; its artifacts and receipts remain preserved.
CIO39NC keeps the same bounded trace and changes only PDFBSDem among58
executable modules; the other57 match CIO37NET. This removes an otherwise
extra diagnostic-only reboot. It is scheduled after20:30Z and after TERM8's
entire guarded core pipeline releases, with the same strict preflight,
normal known-good non-AVE CIO29 recovery and actual.next restoration.
It also captures the bound Linux I210 driver's read-only ethtool registers
and live PCIe-C DMA-property/configuration evidence during the normal Linux
visit. There is no additional Linux boot or configuration change. At this
entry, CIO39NC remains uninstalled and its cache hypothesis unconfirmed.


### Owner activity invalidates the20:21 measurement; repeat guarded

The coordinator reported that the owner physically used the M1 during
20:15–20:25Z. The20:21:19–20:22:20 measurement is therefore discarded as
quiet/performance evidence. Raw receipts and original summaries are retained;
current summaries explicitly say DISCARDED_OWNER_ACTIVITY_RAW_ONLY. The
reported CPU percentages and cold/warm values from that run must not support
a quiet-baseline or AppKit-cause claim. CODEX-TB injected no input during it.
Other CODEX-TB M1 trials ended before20:15; contemporaneous local native SDK
and host tests ran on the builder, so this notice does not invalidate them.

The forthcoming DESK7 owner-input marker is absent on the current compositor.
The pending Ethernet trial now waits outside all locks when that marker is
missing or active, rechecks after acquiring GUI, and checks immediately before
the first reboot outside the recovery handler. A refusal releases the slot
without triggering rollback or reboot. Its kernel and input artifacts are
unchanged. No CODEX-TB synthetic pointer/key hold is scheduled.

The repeated CLI CPU/NHI sample is queued after21:08Z and a valid inactive
owner marker. It records actual kernel identity, compositor PID and real
pointer/key counters before and after; any counter/process change, active or
missing marker, or native probe failure discards it. It sends no input and
performs no capture, restart, install or reboot. At this entry it has not run;
the original idle/AppKit cause remains unresolved. The compositor's Shm drop
worker uses a blocking Rust channel in source, so its raw CPU cost must not
be mislabeled an explicit polling loop without further evidence.

### CIO39NC actual trial: link remains up; DHCP still fails

After DESK7's virtual-seat installation and fresh inactive owner checks,
CIO39NC ran21:08:40–21:14:32Z. The normal installer completed21:10:15Z;
UART capture preceded its first boot. Authorization and the actual12-function
native inventory succeeded again, followed by the real mapper/MSI/BME path.
The I210 interface was UP/running with1 Gb/s link and no mutex panic.
Two DHCP DISCOVER writes were accepted after four initial EPERM refusals,
but no OFFER/ACK, leased address or packet completion appeared. Native BSD
packet counters remained zero, with2048 output bytes. The requested-normal-NC
property and2 direct/258 bounce mappings were observed. This does not prove
actual PTE attributes or establish a cache root cause. Stage(b) remains open.

The same Linux visit observed no dma-coherent property throughout the live
PCIe-C ancestry and actual kernel support for CPU/device DMA synchronization.
All three ethtool commands were unavailable (FileNotFoundError); there is no
Linux register-dump comparison from this visit. No Linux configuration changed.

The bounded trace appeared in neither native dmesg nor the retained UART.
Source review found its FreeBSD printf is redirected through freebsd_printf
to dprintf, which returns when fbsdkpi_debug is absent. Thus missing messages
are a diagnostic failure, not proof that descriptor routines were never called.
The next source delta uses the existing unconditional pdos_log seam with the
same finite budgets and adds the IOKit/FreeBSD transmit result. It adds no
timer, polling worker, MMIO write or global debug boot argument. Native build
and checks are still required before another trial.

The failure was preserved and the normal installer restored qualified NON-AVE
CIO29 at21:14:31Z; actual.next/socket/inject checks passed and the owned UART
reader was reaped21:14:32Z before the boot locks released. No USB/display or
hot-unplug success is claimed. The separately guarded CLI idle repeat started
on this restored kernel at21:14:32Z and has no qualified result at this entry.

The corrected unconditional logger and both transmit handoff sites subsequently
native-built from the exact CIO39NC Ethernet tree. The source comparison has
four changed files, two added private headers and3757 unchanged files; no
functional DMA/packet policy or other kernel module is changed. Formatter
truncation and the existing concurrent80,000-call/eight-record budget fixture
pass with ASan/UBSan. All six required repository checks pass. This SDK is
still uninstalled; the next collection must preserve the other57 modules.

### Owner-idle repeat completed on restored CIO29

The replacement measurement ran21:14:33–21:15:33Z. Owner state remained
inactive, real pointer/key counters stayed zero, the actual compositor PID
remained130, and the native process preflight found no yes load. There was no
concurrent core installation or other GUI test in the coordination log.
The sample used the restored CIO29 kernel with DESK7 and the20:55 Foundation
version, so it is not interchangeable with the discarded20:21 measurement.

Visible non-idle kernel threads consumed2.196% of one CPU; machine idle
averaged98.310%. NHI counts rose by12TX+12RX interrupts around the two explicit
snapshot queries, with no observed storm. The largest unnamed kernel thread
used1.573%, and PDM1SPIHID used0.340%. The compositor consumed3.393% of one CPU:
Shm drop1.987%, main0.961%, HID reader0.442%; its interrupt-wakeup rate was
about433/s. The interactive fixed CPU loop took83.676–84.345ms and the syscall
loop14.494ms. These are current measured values, not an AppKit regression test.
The earlier k2/USB04 comparison and shared NVMe hot stack remain scoped
negative evidence for a new Thunderbolt polling loop. The original reported
AppKit slowdown's cause remains unresolved; neither the host tests nor this
replacement sample establish a fix.

### CIO40TX: actual transmit submission, hardware head remains zero

The visible finite logger trial ran21:44–21:52Z with source f8278e228.
It again authorized the TS3 Plus, enumerated the12 native PCI functions and
reached en1 UP/running at1Gb/s. It FAILED DHCP: no OFFER, ACK, native IPv4
address or packet completion. Two342-byte packets reached encapsulation and
both FreeBSD/IOKit handoffs returned zero. Their transmit tails advanced
0→1→2 while TDH stayed0; RX head remained0 and tail0x80. The coherent TX ring
IOVA was0x04000000; packet IOVAs0x04208000 and0x0420c000. Queue and transmit
controls were enabled. This establishes software submission, not device DMA.
No fresh post-doorbell native DART fault or CPU page-attribute snapshot was
captured, so a DART/cache root cause remains unproved.

In the normal Linux installer visit a fixed read-only helper captured20
non-destructive words through the actual bound igb BAR0 resource. Linux had
matching TX head/tail0x82 and RX head/tail0x5a/0x59. Its ring lengths were0x1000,
versus native0x4000; the queue controls also differed. Those differences are
observations, not explanations. No ICR/EICR, clear-on-read statistics, PHY/NVM,
write or reset operation was used. Existing ethtool dump attempts remained
failed because that program was absent; the independent helper succeeded.

Darwin/SSH booted normally but the standard desktop launchd job initially
exited1 before creating its runtime. Script syntax and a native shell command
passed. One explicit start of that existing job inside the same owned boot
slot returned1/connection reset, yet actual job/compositor processes appeared
and the strict process/socket/inject prerequisite then passed. No installed
script was edited and no input was injected. This cold-start cause is unknown;
the restored29 boot did not reproduce it.

Failure captures were retained privately and the normal installer restored
qualified NON-AVE CIO29 at21:52:25Z. Actual.next/socket/inject checks passed;
the owned UART reader was reaped21:52:26Z before unlocking. The collection had
58 executable modules, no AVE/pdave, unchanged boot arguments/default Linux;
only Ethernet changed versus39 and the other57 executable UUIDs matched.
All six repository checks, native SDK/collection checks and the finite logger
ASan/UBSan fixture passed before the trial. No USB or external-display runtime
success follows from these results. Next is bounded DART/mapping/cache-attribute
inspection at submission, then the dock Ethernet, Keychron, display and physical
hotplug milestones in the owner's order.

### Machine trials paused by the owner; offline preparation continues

On2026-11-15 the owner deferred Thunderbolt until he explicitly opens an
exclusive machine window. CIO40TX was already finished, with qualified NON-AVE
CIO29 restored21:52:25Z, actual standard.next/process/socket/inject verified,
owned UART stopped21:52:26Z and all TB locks released. The coordination log
records `CODEX-TB M1 back in SiliconForest; Thunderbolt trials paused by owner`.
There is no pending reboot waiter or slot. No further kernel install, reboot,
Linux visit, GUI/core hold or M1 workload is allowed by this paused assignment;
only a rare read-only check if needed. Local workstation source/build work
continues. AVE remains separately paused, with no build/probe/extraction.

| Piece | Observed on this M1 | Built or host-tested, not observed |
|---|---|---|
| Type-C/ATC/NHI/router/PCIe | Attached TS3 Plus authorized, NHI running, real PCIe tunnel,12 native PCI functions including4 xHCI and I210; repeated40 | Shared ownership/lifecycle extensions do not qualify physical hot-unplug |
| Dock Ethernet | en1 UP/running/1Gb/s; two TX submissions and TDT0→1→2, TDH0; NO DHCP or packet completion | New finite DMA/PAR/RID/PCI/mask diagnostic SDKs and58-module collection; no target execution |
| Dock USB/Keychron | No native USB device or physical typing yet | Exact selected FL1100, native PCI admission/DMA/MSI, generic HID/read-only storage, shared-broker-preserving SDK; host allocator/controller/composition checks pass; USB collection unbuilt |
| DP/dcpext/Samsung | No native HPD/EDID/external connector/output | DP-IN/NHI protocol, PHY/crossbar/resource/mailbox/DART helpers have source/host/SDK evidence; actual native provider/resource/route glue remains |
| DESK K1–K13 | Only internal panel exists in recorded output inventory | Compositor per-output path ready in its own stream; kernel must publish real same-card connector/CRTC/planes/flip IDs/hotplug |
| K14 pinch/rotation | Not installed or physically verified |776-byte version2 producer/recognition, actual-decoder geometry/lifecycle ASan/UBSan tests and native SDK pass; compositor accepts old/new ABI |
| Ten physical dock cycles | Not run | Software retrain or quarantined backing is not removal/replug proof |
| Idle regression | Valid guarded29 repeat21:14:33–15:33, kernel2.196%oneCPU/machineidle98.310%; owner-overlap20:21 run discarded | Original AppKit slowdown cause unresolved; new desktop versions require new measurements |

The offline Ethernet comparison and ranked hypotheses are in
[DIAGNOSTICS.md](../../../integrations/puredarwin/m1-thunderbolt/DIAGNOSTICS.md).
It follows pinned Omacom igb, apple-dart and pcie-apple paths through allocation,
direction/sync, actual descriptor/tail programming, interrupt masks/MSI, MAC/PHY,
BME/bridge windows and the64-SID USB4 DART layout. The STATUS PHYRA delta is set
in **Linux**, not native; both have the GIO master-enable bit. No cause has been
proved by the difference. CPU table coherence in the Linux DART configuration
also prevents treating absent table-cache flushes alone as the root cause.

The one diagnostic kernel gathers a finite snapshot at the existing eight
doorbell trace admissions: DART fault/address, all64 RID/TCR words, SID0 roots,
retained ring/packet backing and CPU walks, CPU PAR attributes and first32 CPU
bytes, guarded endpoint/MSI/upstream-bridge config and non-destructive queue/MAC/
mask words. It adds no polling timer, work thread, fault ACK, flush or functional
DMA-policy change. CPU walks/bytes are not device-fetch proof. Foreign endpoint,
bad ABI, IRQ context and lost power/ownership refuse inspection.

Local actual-method ASan/UBSan tests and both native SDKs pass. The final
collection `/nix/store/nm95yi9gdi9lzz4lys9as5fyy9bxarls-puredarwin-kc-cio41dma-access-guarded`
builds with zero overlapping segments,58 executable modules and semantic
prelink equality after metadata compaction. Only PDAppleCIO and PDFBSDem differ
from40; the other56 executable UUIDs match. Frozen source comparison verifies
only the diagnostic files plus the existing IRQ health fix, with no uninstalled
USB registration imported into the Ethernet baseline. All six required repo
checks pass. It is **uninstalled**, no capsule uploaded and no boot scheduled.

### Superseded dock-only window plan (historical)

First allow12min for one diagnostic trial including normal29 recovery: fresh
coordination/owner guards and core→GUI→bench order; one normal Linux installer
session plus fixed read-only comparison; UART after installer and before first
boot; actual standard desktop checks; TS3 authorization/native12-function
inventory/mapper/MSI/late BME; one bounded DHCP attempt and all paired diagnostic
captures; if a lease exists,16 ICMP replies strictly bound to that interface;
retain every failure; normal29 recovery/standard-state verification/UART reap/
unlock. The detailed sequence and prerequisites are in DIAGNOSTICS.md. No
elapsed time or completed local build authorizes this boot.

After evidence selects an Ethernet fix, qualify it separately before coupling
the prepared USB profile. A USB trial needs roughly10–12min including recovery:
actual selected FL1100 command/event completion and port/device/config/HID
descriptors, then owner physical Keychron typing; request a stick only after
that, mounted read-only. DP needs its missing native provider/graph glue built
before a useful20min display window. Capture real tunnel/HPD/training/EDID,
DESK `--outputs` and independent external scanout before physical hotplug cycles.
No concurrent policy changes or synthetic typing count as these milestones.

Open requests were answered offline: K1–K14 contract acknowledged; SIGSTOP is
an unresolved compositor-specific finding (minimal native reproducers stop),
with future owned EGL-only/HID-only isolation deferred. Firefox's stock Linux
benchmark reference is noted for an authorized Linux window with a separate
uninterrupted allowance of up to20min, installed browser only, matched scale/
viewport/renderer/power/QoS and raw scores; no extra reboot/install or invented
Linux result. Optional AVD changes stay separate; no AVE work.

### Offline K14 and DCP tunnel boundary prepared

K14 now has source and a native PDM1SPIHID SDK, without any target execution.
The existing two-finger decision emits pinch BEGIN/UPDATE/END/CANCEL instead of
discarding it; normalized separation gives Q16 scale, integer CORDIC gives
clockwise centidegree deltas, and centre motion uses libinput units. It keeps
100ms count debounce and suppresses wheel/pointer motion during pinch. Physical
lift ends it; click, another gesture/contact identity, controller reset or basic
protocol fallback cancels it. The exact24-byte event/776-byte version2 ring has
DESK's kind/scale/angle fields, with scale1 for swipes. The installed29 still has
the old producer; no physical gesture or client delivery is claimed.

Actual-decoder ASan/UBSan checks pass3601 independent angle vectors plus pinch
in/out, rotation/wrap, centre motion, brief/long lift, third-finger/click/reset/
basic cancellation, scroll exclusion and ABI offsets. Old pointer/scroll/stroke/
three/four-finger swipe checks still pass. Initial click cancellation tests
failed and led to an END→CANCEL correction; failures remain private. The native
SDK `/nix/store/x6q4xlhpwh1fza7dzmbaqis3nvlin83g-puredarwin-m1-spi-hid-multitouch-0.1`
builds, with constructed-test scratch storage on the heap. The new synthetic
sysctl values2/-2/90 were not run on the M1. Details and qualification limits:
[K14.md](../../../integrations/puredarwin/m1-trackpad-child/multitouch/K14.md).
It requires a later isolated authorized kernel, not inclusion in the Ethernet
diagnostic; allow roughly10min for boot/physical gesture checks/normal recovery.

The DCP tunnel boundary now has a48-byte versioned native request and a
PDDcp-local immutable epoch bridge to the actual pinned
`apple_dcp_tb_dp_tunnel`. It admits a callback before attach returns, keeps full
64-bit generation/owner/callback identity, forwards matching retries, rejects
stale identity and blocks reactivation during detach. Actual driver errors stay
errors; failed retirement keeps a failed epoch. Linux private node pointers
remain local. The native provider must still retain actual connector/owner,
serialize calls and establish power/shared-PHY/route ownership before this seam
can be used. The helper publishes nothing and is currently uncalled.

Actual-source host provider fixtures pass ABI/identity/generation/synchronous
callback/retry/failure/retirement cases. Final native SDK
`/nix/store/c3jsjqjgj65hn8k4ja59kq325pg9cp77-puredarwin-dcp-0.1` builds.
The link map and actual final ARM64 bytes contain a direct BL from the bridge
to the real Linux implementation. A name-only nm check failed because this SDK
strips private local names; the earlier interpretation as removed code was
wrong. Both SDK versions had the actual call. Failed checks and the corrected
byte-level proof remain private. Retaining public entry points is explicit now;
it does not turn the helper into a native provider.

Offline dock USB allocation/controller/admission/composition and DP resource/
mailbox/crossbar/notification fixtures also pass. One USB allocator invocation
initially omitted its required prepared-source argument and failed before
execution; the corrected run passes against the exact SDK source. No machine
work, kernel installation, new AVE work or push accompanies these preparations.

The diagnostic capsule is now packed locally with SHA256
`cb7f057c2aa8746275577c625ff30075626dbb9f8050129c9d361c76515dde2a`.
It is not uploaded or scheduled. Full native2-input non-debug section comparisons
and all57 kext strip proofs pass; unchanged29 boot.bin/configuration,58 modules,
no AVE/pdave and56 other executable UUIDs matching40 are recorded privately.
K14 commit121981a31 and DCP bridge commit14b554684 are separate and excluded
from that diagnostic. Final six required checks pass; nothing was pushed.

Further source review confirms that the private layer's provider registry covers
clock/reset/power/regulator services, not generic PHY/mux/Type-C backends. Pinned
CONFIG-off PHY/mux headers return errors; a compiled dcp-fabric helper does not
satisfy those dependencies. The next work must supply actual retained native
providers under the current TBT lease and publish the aggregate route graph
before DRM component binding. The RTKit shared host is an IRQ/workloop anchor;
mailboxes are separately keyed and validated per consumer, so that shared anchor
alone is not a demonstrated mailbox-routing bug. These are remaining offline
integration requirements, with no new machine trial implied.

### Firefox-first combined kernel prepared offline, 2026-11-15

The owner's latest instruction supersedes diagnostic-first and isolated-K14
installation plans. The next authorized exclusive window starts with Firefox:
ONE combined kernel on qualified NON-AVE29, containing AVD imported GPU capture
buffers/cache/IRQ, K14 pinch and HID readiness. No failing Ethernet, uninstalled
dock diagnostic, DCP or AVE changes are coupled into this image. No machine
action, SSH, target lock, reboot or Linux visit occurred in this preparation.
Last verified installed state remains29 restored21:52:25Z with standard desktop,
UART reap and released locks recorded above.

| Feature | Observed on M1 | Prepared offline only |
|---|---|---|
| Decoder/GPU shared backing | Installed AVD reports importcap0; strict zero-copy client refuses; fallback copies pixels | Exact media import contract, retained original BO/task/UPL, DART rollback/quarantine, IRQ41b6, actual Force CMO handoffs; three-file patch over all52 checked SDK155 files |
| Pin/cache ownership | No native exported-BO import, task-death or old/new-visibility qualification | Frozen XNU/DRM pager and no-copy kernel mapping audit; actual native teardown/decode failure tests; real DC CIVAC API resolved through final ARM64 stub/chained pointer |
| Pinch/rotation | No physical pinch installed or observed | K14 on exact installed Touch Bar variant; only4 multitouch files change, transport/Fn/keyboard preserved; actual decoder3601 angle oracles/lifecycle tests and native SDK |
| HID readiness/timing | DESK7 reports near8ms, no drops/bursts; its reader scheduling lateness fixed in desktop stream | Native-publication rev002 preserved with FIONREAD/select; actual prepared-source4000 races plus pre-publication rejection; no kernel report-cadence change |
| Combined image | Not installed/uploaded/scheduled |57 executable modules, exactly AVD/SPIHID/IOHIDFamily changed,54 other UUIDs including kernel/GPU/DRM/DCP/NVMe/CIO identical; all56 kext non-debug sections preserved; zero overlaps |
| Firefox Linux reference | No Linux score captured | Local installed-Firefox runner reuses stock BrowserBench/Marionette code, fresh profile/no injected input/no browser install; visible viewport/scale/renderer/power captured |

The detailed source/API audit, recipes, actual-source host checks and full window
sequence are in [m1-firefox-kernel/README.md](../../../integrations/puredarwin/m1-firefox-kernel/README.md)
and [SOURCE-AUDIT.md](../../../integrations/puredarwin/m1-firefox-kernel/SOURCE-AUDIT.md).
The immutable AVD snapshot includes the earlier IRQ ownership fix; its upstream
hardware frame40 cause remains unproved. CPU/GPU writes are excluded during
decoder ownership by the cooperative DMA contract. Real GPU-completion leases
must precede reuse; a header usecount alone is insufficient. Native pin/cache
admission and Force-CMO performance remain hardware tests, not source claims.

Local combined collection:
 /nix/store/0lam9mk3qhihx7ziwi6q8wm77wv8y7dv-puredarwin-kc-firefox29-import-pinch-readiness.
Collection SHA25630e04e6209d0147b604b6616cff7cdb20ec97d597949ab486e58ef40676ed1a3.
Private packed capsule SHA2560befc6ffb6473f050b63c81453f52a3e61cfc4bbbb88721fc8cbf8e73a4caa82;
its605-byte boot arguments and boot.bin are byte-identical to29, noAVE/pdave.
Qualified29/a149e487… is the last verified installed image and recovery.

#### Plan for the next owner-opened exclusive window

Allow about50min: fresh coordination/owner checks2min; the normal Linux installer
visit includes up to30min stock Speedometer3.1/MotionMark1.3/JetStream2.2 reference
ONLY if Firefox exists; single combined install/UART/one-time SiliconForest boot
3min; actual standard desktop/CLI admission2min; media/FX import-old/new/held64/
pool-pressure/flush/task-exit/codec-hash/PTS/1080p60/4K30 qualification8min;
physical pinch/rotate and ordinary input regression2min; retain results,
qualified29 recovery if needed, standard-state verification/UART reap/unlock3min.
No browser install or extra Linux reboot for the reference; retain incomplete/
timeout results and mark viewport/scale differences. No synthetic input takes
the owner's pointer/focus. No second speculative candidate is installed if a
prerequisite fails; preserve the CPU-copy fallback and recover.

The local dock41 diagnostic remains ready for a LATER explicitly opened dock
window; the earlier12min sequence is retained as a dock plan, not the next
Firefox window. USB/physical Keychron, DP provider graph/HPD/EDID/Samsung and10
physical dock cable cycles remain unobserved. Original idle regression and
compositor-specific SIGSTOP finding remain unresolved as recorded above.


### GPU freeze recovery and next-kernel priority: devfs clone lifetime

The authorized recovery exception captured the23:20 frozen desktop before any
reset and returned standard SiliconForest at23:37:34Z. Both compositor28999
and GPU helper28583 were in the SAME devfs_dntovn creation wait, before DRM
callbacks, not an observed AGX firmware-recovery lock. AGX/DCP workers were idle
and services attached. All37 tasks/201 threads,23:15+ dmesg/ioreg/UART and
symbolication remain private at runs/sftb/gpu-wedge-2320/.

Source plus actual-function host reproduction shows stat-only DRM lookups leak
clone reservations: no open/close happens, and devfs reclaim never frees them.
Lookup1024 exhausts the1023-slot pool; the allocation-error path leaves DN_CREATE
set, stranding all later lookup/stat/open callers and preventing helper exit.
That threshold is hosted evidence; the original failing caller/count were not
recorded on hardware. No live creator/lock owner was identified in the snapshot.
Older SF-AC dma-buf/syncobj/mapping issues remain separately unproved.

The next SINGLE Firefox kernel now prioritizes the devfs/core + shared DRM
fix alongside the original frozen AVD/K14/HID inputs. It requires exactly five
changed modules and52 baseline UUIDs unchanged, including AGX/DCP/NVMe/CIO.
Native APIs are linked in the final collection; the source/host/native build
handover and55min window plan are in
[m1-devfs-clone/README.md](../../../integrations/puredarwin/m1-devfs-clone/README.md).
The fixed-image5000-stat/512-open probe and owned GPU-helper kill→IPC-closure/
visible-fallback gate run FIRST after standard-state verification, before media/
pinch tests. They are prepared, not observed. No new install is authorized now.

Recovery used one M5 reset, a normal qualified29/a149e487… install, one-time
bless/defaultLinux preserved, actual .next/socket/inject + non-input stats reply,
UART reader stopped and all TB locks released. e2fsck -fn returned4 with bitmap/
count differences; it skipped journal replay and did not repair anything.
Correction: the initial clean request used /sbin/reboot rather than supported
fenced /bin/reboot, so a supported shutdown hang is NOT proved. Raw failures
and the command mistake are retained; no extra recovery boot followed.
CODEX-TB resumed the machine pause after recovery; all later work is offline.


### Devfs-only recovery kernel install in progress, 2026-11-16 00:54Z

The owner's later durable Firefox authorization supersedes the earlier machine
pause for Firefox kernels; idle and hourly waits are lifted. Thunderbolt and AVE
remain paused. The first image deliberately contains only devfs/core + shared
PDDRMDri changes, with55 of57 executable module UUIDs unchanged from CIO29.
Normal installation retained CIO29 as rollback; the supported /bin/reboot reached
Linux without an external reset. Read-only filesystem checking returned4 with
bitmap/count errors and two beyond-EOF extent records in Firefox YouTube SQLite
files. No repair was performed because this exceeds the conditional bitmap/count
repair permission. Private full output, inode/path evidence and filesystem features
are retained in runs/sftb/devfs-install-0041/.

Observed on the new M1 boot: DESK8-next003 hash46dc9bde runs with responsive
non-input inject stats; four processes each completed5000 stat,5000 open-close
and5000 failed O_DIRECTORY opens. Reservations allocated60000/freed60000;
used slots3 before/after, zero allocation failures. GL exact pixel hash and
vulkaninfo each passed50 launches. Firefox20 fresh-profile visible-window
qualification is in progress, so final adoption is not yet claimed.

Identification correction: Mach-O LC_UUID/embedded kernel UUID1FDC2A71… is
not kern.kernelcacheuuid. XNU obtains that sysctl from the prelink plist's
_PrelinkKCID, which is65F85F90-FF31-4541-A51E-8247DD0C4881 in the actual capsule
and live M1. Decompressed capsule bytes equal the built collection. The initial
UUID assertion failure and premature explanation are preserved with the corrected
proof in the run directory; no image change was needed.


### Devfs-only qualification completed01:03:43Z

The first kernel cycle is complete and the image remains the normal SiliconForest
kernel, with CIO29 retained as rollback. All20 consecutive fresh profiles04–23
created mapped, committed and captured windows, used their own app-menu Quit,
and left no process-family member; clone slots returned to3 after every launch.
Median mapping time5.4175s (4.055–6.844s). Visual review of the complete montage
finds blank content/stale NewTab chrome in all20: creation/exit passes, page-paint
is a strict failure handed to FX/DESK. No claim of working Firefox rendering.
UART reader and route are gone; all locks released. The combined decoder/K14
image is now built and separately announced; native import/cache/pinch proof is pending.


### Native Firefox42 import panic and rollback, 2026-11-16

The five-module image0d31b0c0… booted successfully. INFO reported ABI1/caps65543
(import bit16); the gesture ring reported776 bytes/version2. Clone stress on
the actual asahi renderD129 allocated/freed60000 reservations, slots3→3, failures0.
The first harness attempted absent renderD128 (ENOENT, no allocations); that
failure is retained with the corrected-node repeat. The decoder packet's
portable Python placeholder was resolved in a separate native manifest after
all45 files were cold-hash verified; original bytes/receipt remain private.

Strict H264 shared-capture testing then panicked at01:17:15Z in
vm_upl.c:1360, VM_OBJECT_WIRED_PAGE_UPDATE_END's !__overflow assertion. Exact
symbolication gives upl_commit_range+0x1cf4 →upl_commit →
IOGeneralMemoryDescriptor::complete+0x288 →pd_avd_os_dma_free+0x110 →
avd_buf_free/pd_avd_session_destroy/context_destroy. This is teardown accounting,
not a demonstrated Force-CMO fault. The register pair426/−426 is consistent
with a wired-count underflow; MD kind/UPL flags were not captured. No candidate
or after-stats survived; before-stats show jobs0/contexts0. HEVC was not started.
No shared-buffer, cache-visibility, decoder-output or physical-pinch success claim.

The panic automatically returned to Linux; no external reset was needed.
Normal installation restored qualified devfs-only12168f25… /prelink65F85F90….
The consumed next-try mark required one supported sf-session relaunch-next
after boot; DESK8-next003/socket/inject and non-input stats were verified.
UART reader/route stopped, actual interactive lock holder exited01:20:47Z.
Postpanic read-only fsck returned4 (journal skipped plus previous extent/bitmap
errors); no repair, root subsequently mounted. An initial rollback command
found Linux's volatile /tmp installer gone; it made no boot-file write. The
installer/env were reuploaded and the normal rollback then verified its hashes.
All failures and UART are retained privately in runs/sftb/firefox-install-0042/.

The following image43 deliberately retains the ORIGINAL CIO29 AVD, excluding
the failed import/CMO/IRQ input. It retains devfs and K14/HID, adds KERN's
reviewed killable-wait PDAgx/PDDcp and speaker cap25 half-dB steps (control176),
and requires exactly7 changed modules/50 unchanged against CIO29. Source/SDK/
link/host evidence is separate from the forthcoming native wait/speaker gate.
The UPL fix is being prepared independently; no assertion or complete call is removed.


Owner update2026-11-16 01:38Z: extra speaker cap defaults OFF. The current model
passes comparison testing with zero violations; the owner explicitly withdrew
the interim volume reduction. The boot argument pdspeaker_cap_steps remains
available, with default0; model/probe safety limits remain unchanged. The first
cap25 image43 reservation was cancelled before ANY M1 action; its kit is retained
privately. The replacement image43 uses sound SDK c5zjb6pd…, collection gif6ishr…,
capsule11f6f53b2089f2a6…, prelink0C63FD93-8845-4DE8-85E5-C317D51928A0.
It still has7 changed modules/50 unchanged against CIO29 and ORIGINAL AVD.
Native default-cap0 / K14 / killable-wait qualification is pending.


### Private-UPL accounting fix prepared offline, 2026-11-16

The frozen source provides a concrete counterexample for the42 panic: device-pager
objects hold private, noncanonical pages, which vm_page_wire/unwire deliberately
exclude from object/global canonical wired counts. The bulk UPL completion path
subtracts all finally unwired pages and queues them, underflowing the zero object
count for426 private pages. A one-line eligibility guard routes private objects
through the existing per-page completion, including its canonical-only accounting
and noncanonical queue guards. Assertions and real complete/unwire remain intact.
The historical descriptor kind and UPL flags remain unrecorded; source agreement
and a matching counterexample do not replace native verification.

Actual-patch-applied ASan/UBSan tests retain the frozen426-page assertion as an
expected failure and pass patched private/canonical cases, repeated wires,
INACTIVATE/FREE_ABSENT, vector/purgable and empty guards. Media's independent
source review agrees with the per-page accounting/queue behavior. Exact frozen
before/after hashes pass in the full native core build, now in progress. All six
repository checks pass. Source, limits and collection recipe are in
[m1-upl-private/README.md](../../../integrations/puredarwin/m1-upl-private/README.md).

The native SDK helper is built but unrun: it first double-maps a real writeback
GPU BO, unmaps its alias, then imports only DATA after a16KiB header and destroys
the context while checking the original mapping/header. It submits no GPU or
decoder jobs, refuses the wrong kernel/import capability and retains synchronous
phase markers. This is the first proposed hardware gate for a fixed-core/import
image; strict complete-frame/PTS/held64 H264 and HEVC gates follow only if it
passes. No zero-copy, physical-pinch or GPU-fence success is claimed.

Image43 remains the independently ready original-AVD/default-cap0 cohort.
Thunderbolt and AVE trials remain paused; no UPL candidate has been installed.


### Kernel43 qualified and retained normal, 2026-11-16 02:08:31Z

The owner's requested default-off sound input is installed: the live
kern.pd_speaker.cap_steps is0 and the enabling boot argument remains available.
Image43 capsule11f6f53b… /prelink0C63FD93… retains original CIO29 AVD (caps7,
no import), devfs, K14/HID and reviewed killable-wait AGX/DCP. Exactly7 executable
modules differ from CIO29;50 are unchanged. No AVE or dock trial was added.

Observed on the M1:60 infinite binary/timeline syncobj waits across SIGKILL,
SIGTERM and caught SIGUSR1 all returned/reaped, worst2.6ms; four concurrent
children completed60000 stat/open-close/failed-open operations with60000 clone
allocations/frees, slots3→3 and zero failures. Exact GL pixel hashes and
vulkaninfo each passed50 runs. This qualifies the tested interruptible waits,
not every uninterruptible DMA wait or a real Firefox-helper fallback. The latter
remains an FX integration gate. Gesture-ring776B/version2 is live; physical
pinch remains unobserved. Current model comparison still has zero violations.

The supported /bin/reboot reached Linux without external reset. Read-only fsck
returned4 with the same two beyond-EOF extents and bitmap/count errors; no repair
was made and root mounted afterwards. Normal installation kept qualified devfs
as rollback; original CIO29 remains local. DESK8-next004 c4a8c437, socket/inject
and non-input stats were verified. UART reader/route stopped; SystemExit released
actual core/gui/bench at the02:08:31 back-in-SiliconForest marker. Private receipts
are under runs/sftb/firefox-install-0043. No push.

The separate private-UPL core and collection now build successfully. Native
instruction review finds the added object-private bit13 branch to per-page
completion, absent from43. The57-module candidate has exactly8 changed/49
unchanged against CIO29 and only core+AVD differ from43; the real Force-CMO
BL/stub/chained-pointer check passes. It is uninstalled. Its first hardware gate
will be the bounded no-job GPU-BO pin/import/destroy probe, followed by strict
frame/PTS/held64 checks only on success. No zero-copy or decoded-output claim.


#### Next Firefox cycle44: announced02:19:50Z, earliest02:21:50Z

This is a Firefox cycle under the standing authorization; dock/AVE trials stay
paused. Post43 FX selector and media control/browser/rate slots completed before
the notice. The candidate remains uninstalled while an earlier GUI holder runs.
It changes only core and AVD relative to qualified43; the optional speaker cap
stays0. The qualified43 capsule is the immediate rollback.

| Phase | Exact action and gate | Budget |
|---|---|---|
| Baseline/shutdown | core/gui/bench, UART before supported /bin/reboot; capture43 ID, desktop and clone state |1min|
| Linux | verify root identity/unmounted state, read-only fsck; no repair of the recorded extent errors |1min|
| Requested Linux reference | installed Firefox/visible-session check in this same visit; fresh profile, stock Speedometer3.1/MotionMark1.3/JetStream2.2, record browser/geometry/scale/renderer/power; SKIP missing browser/desktop, never install a browser or substitute headless |0–10min|
| Install/boot | normal installer, verify candidate+43 rollback, reroute UART, one-time bless; default Linux unchanged; verify .next004/socket/non-input stats |1min|
| First native gate |3 rounds real writeback BOs, distinct duplicate mappings/alias unmap,16KiB DATA import and destroy; no GPU/decoder jobs, header canaries/original mapping alive, counters unchanged/contexts0 |<1min|
| Strict shared capture | separate fresh cold prefixes per codec; all45files verified before loading; H264600 then HEVC300 pictures, independent full NV12 MD5+PTS, first picture held64 callbacks, copied bytes0, exact kernel-job deltas/no faults/quarantine/contexts |1min|
| Retain or recover | GL hashes/Vulkan10, final caps/desktop; stop UART and release actual locks at return marker. Any failed prerequisite stops later codecs and restores43 through the normal installer |1min plus recovery if needed|

Expected6min, at most16min with the bounded Linux reference. Raw Linux scores
have uninstrumented owner-input interference and are not a matched-comparison
claim; incomplete runs remain incomplete. No pointer/key injection is needed.
A shared-capture PASS still does not prove actual GPU-consumer completion,
browser60fps,4K30 delivery or physical pinch. Those remain the media/FX/DESK
integration gates after the kernel returns. No extra Linux visit for benchmarks.


### Kernel44 shared capture qualified, 2026-11-16 02:37:43Z

The fixed private-UPL core is now installed and retained normal with the
reviewed import driver, exactly8 changed/49 unchanged executable modules versus
CIO29; only core and AVD differ from qualified43. Capsule7b2eba23…,
prelink9ACED54D…. Speaker extra cap remains0 with its enabling boot argument,
and K14/HID plus the qualified killable-wait drivers remain unchanged.

Observed on the M1:3 no-job rounds each imported/destroyed14 real writeback GPU
BOs carrying6965056 image bytes (426 padded16KiB DATA pages plus a header),
with distinct duplicate mappings,
alias unmap, original mapping alive and header canaries preserved. The real
complete/unpin path ran without the prior UPL overflow. DART allocations,
contexts and quarantine returned to zero; no decoder or GPU job was submitted.

Strict H264600 and HEVC300 pictures both pass the independent Linux full-visible
NV12 MD5+PTS oracle, actual GPU backing/two planes/DATA offset, first picture
held64 callbacks and copied-bytes0. Fresh cold prefixes per codec avoid reusing
Mach-O files after XNU's instruction lowering; all45 staged files were verified
before loading. Kernel jobs900/done900, errors/timeouts/stale/DART faults0,
contexts0/DART KiB0/quarantine0. Final GL pixel hashes and Vulkan each pass10
launches; clone slots remain3. This proves the tested shared-capture paths,
not GPU-consumer completion, producer-task death, browser presentation or60fps.
H264's instrumented decoder average4221us and MD5 callback21377us must not be
read as a playback-rate result. Those remaining integration gates stay explicit.

The clean /bin/reboot reached Linux without reset. Read-only fsck returned4:
the previous two EOF extents plus one new three-block EOF extent in an agent's
Firefox startup cache, with bitmap/count differences. Exact ncheck/stat are
private; no repair and no clean-filesystem claim. Three4KiB blocks beyond a
52208-byte EOF complete a16KiB VM page: a pager/truncation rounding hypothesis
was passed for source audit, not established as the cause. Root mounted.

No installed Linux Firefox was found, so the requested reference was skipped:
no browser install, headless substitute, scores or extra boot. An initial nested
helper syntax error ran no browser; its failure is retained alongside the
corrected availability check. The normal installer kept43 as rollback; original
CIO29 and devfs-only kits remain local. DESK9-next001 df5a3290/socket/inject and
non-input stats were verified; UART stopped, actual core/gui/bench released by
SystemExit at the02:37:43 return marker. Private receipts and classifications:
runs/sftb/firefox-install-0044. No dock/AVE trial or push.


### Kernel44 producer-task lifetime gate, 2026-11-16 03:18:01Z

Three native rounds passed producer GPU-VA unmap, SIGKILL of only our child,
and reap in2.53–2.54ms while the parent kept the consumer BOs. Immediate INFO
returned0, contexts1 and caps0x10007: retirement is lazy, on the next CREATE,
as the frozen driver documents. The real CREATE reaper ran complete/unpin;
consumer data marked after producer death survived retirement and new allocation
pressure. Final contexts/DART bytes/quarantine were0, with no GPU or decoder
jobs and no fault-counter change. This tests that lazy retirement contract,
not immediate-on-death cleanup or GPU-consumer completion.

The first probe's immediate-INFO composite predicate remains a failed run; its
stale errno did not establish an ioctl timeout. Revised source prints each field
and tests the documented CREATE reaper instead of changing the kernel to meet
that mistaken expectation. Both runs remain private under
runs/sftb/firefox-task-death-0044{,-v2}. Same qualified44 and DESK9-next001
remained running; non-input stats responded, UART stopped, actual core/gui/bench
released at03:18:01. No install, reboot, injection, dock or AVE trial.


### Kernel45 ext4 EOF allocation qualified, 2026-11-16 03:45:05Z

Capsule5c323fb6/prelink2BFF4349 is installed normal, source733332ef0. Only
com.apple.filesystems.ext4 differs from qualified44:57 executable modules,
9 changed/48 unchanged versus29,55 other kext sources and core retained.
All6 required checks pass; real VM frozen/fixed controls give e2fsck RC4/0,
28 mmap/write sizes+7 resize steps, clean unmount and29 independently dumped
file bytes each. The source clips write BLOCKMAP requests at current/active
extending EOF; journal ordering, flushes, NVMe and halt fences are unchanged.

On the M1,28 own mmap/write cases+7 resizes/fsync/reopen and29 host byte
oracles pass. The first native st_blocks==data-size assertion FAILS and remains
a failed diagnostic: five fragmented files include extent-tree metadata, which
the frozen ext4_ext_account_meta explicitly counts. Read-only F_LOG2PHYS_EXT
(flags0, no allocation, packed20-byte ABI) then verifies247 initialized data
blocks and681 hole/unwritten tail blocks across all29 files through128KiB.
This establishes the tested absence of initialized rounded EOF tails, not absence
of unwritten preallocation, physical-media readback, whole-filesystem cleanliness
or power-loss safety. The original accounting failure was not relabeled PASS.

GL exact hashes5/Vulkan5 pass; clone slots3/fail0. Import caps65543, gesture
ring776B/version2 and extra speaker cap0 remain; no AVE/boot-arg change. No
new decode/fence/browser-FPS or physical-pinch qualification is added.

Clean /bin/reboot reached Linux without reset. Unmounted-root read-only fsck
returns4 with two legacy EOF extents and known deleted-inode/bitmap/count
errors; the earlier startup-cache record is not reported in this pre-install
check. No repair, and no existing-error improvement is credited to45. Root
mounts. Normal installer retains44 rollback; default Linux and one-time bless
are preserved. Same DESK9-next001 df5a3290/socket/inject stats respond; UART
reader/route stopped and actual core/gui/bench released by SystemExit at the
03:45:05 back marker. Receipts: runs/sftb/firefox-install-0045, private.

The30-file4KiB descriptive sample is worse after this cold boot: write p50
0.288→1.459ms, fsync9.790→11.010ms, unlink11.262→20.358ms. Different boot,
warmth and allocation state prevent causal attribution. This is not a speed
improvement; stage timing/steady-state repeats are needed before a performance
change. Source already batches64 metadata blocks/1s; no durability reduction.
A separate source audit notes ignored fsync/flush errors and commit-error buffer
cleanup, without a native I/O-error observation or a proposed45 journal change.


Follow-up offline fragmented control: --fragmented seeds alternating holes in a
fresh regular scratch image using debugfs, requires a clean precheck, then boots
the same frozen/fixed drivers. Both28 IO cases+7 resizes/29 dumped byte oracles
pass;17 files in each contain counted extent-tree metadata. With
e2fsck -fn -E no_optimize_extents the frozen image has real initialized EOF
extent errors (RC4), fixed image is clean (RC0). The optional optimization
exclusion matches M1 checks and does not suppress consistency errors. The first
fragmented classifier expected only i_size wording and remains failed; the
corrected check ties either exact i_size or logical13..15 bounds errors to the
inode of /m-52208. Private receipts: runs/sftb/ext4-eof-qemu-fragmented-{001,002}.

A further native45 CLI sample (no input/install/reboot; other desktop work active)
gives three30-file p50s: fsync16.129/13.264/12.678ms and
unlink18.619/13.081/13.489ms. No causal44/45 performance claim follows.
Read-only NVMe counters show0 errors/timeouts, flush lifetime mean6.519ms;
392 flushes during the samples imply an approximate interval mean6.760ms
(from rounded cumulative averages). Global background I/O is included. The
source's two journal flushes could account for much of the13ms latency; this
is a measurement-supported lead, not per-call attribution. DTrace was absent
from PATH. Before choosing an optimization, obtain syscall/commit/flush-stage
counts and timings; preserve crash consistency and all existing barriers.


### Urgent Firefox launch check on45, 2026-11-16 04:24Z

TERM reported a real SSH-launched family failing child check-in with
BOOTSTRAP_UNKNOWN_SERVICE, and DESK reported a second-launch crash. The initial
health harness also had missing automation permission and an owner-overlapped
capture; these failures were retained, not counted as kernel failures.

The corrected installed-app gate completed3 compositor application requests
and3 SSH wrapper launches. CODEX-TB independently inspected all six retained
no-input renderer images: each has its own heading/text and three coloured
squares. None of those six Firefox logs contains bootstrap_look_up failure or
MachCheckInListener timeout. This refutes a universal45 launch outage; retain45
with exact44 rollback ready. The earlier genuine intermittent timeout remains
open and is not erased by these six successes. FX owns the fuller runtime gate.
Private receipts: runs/firefox/urgent-launch45-v2 and
runs/sftb/firefox-bootstrap-0045.

Comparison of the actual derivations confirms IDENTICAL immutable core input
sp4zw2… for44/45, all55 other kext source inputs,605-byte boot arguments and
boot.bin. Only ext4 changes. The same Mach SEND_INVALID_RIGHT guard signature
is already present in the saved44 state before45's reboot. These facts exclude
an intentional Mach/bootstrap policy or boot-arg change, not an effect of boot
timing or filesystem behavior. A native SSH parent registered a fresh private
service; its exec-spawned child looked it up successfully. The first probe's
cleanup failed with SERVICE_ACTIVE because its receive right was still alive;
that failure remains. Closing that right before unregister gives all-zero
registration/lookup/cleanup in the corrected probe. No existing service changed.

CORRECTION: an initial TB name-filtered process review incorrectly called a
Firefox parent childless; its PPID graph contains12 plugin-container descendants.
The correction was posted immediately after the full graph was checked.

Pre-install filesystem validation was REAL QEMU, completed03:25:43 BEFORE the
03:41:12 installer: exact frozen11f479… and patchedar0bc… SDKs,16KiB VM/4KiB FS,
28 mmap/write cases,7 resize steps, clean unmount and29 independent Linux
debugfs byte oracles per driver; frozen fsck RC4 reproduces the EOF error,
patched RC0. Actual-source ASan/UBSan counterexample/controls and96 identical
SDK exports also passed. The fragmented17-metadata-file VM control came AFTER
installation and is supplemental. Native45 acceptance had filesystem/GPU/desktop
checks but no fresh painted Firefox launch; the corrected gate closes that gap.
Future kernel acceptance must include a fresh installed-app painted launch via
each route before retaining it normal, as well as the change-specific tests.
No rollback, reboot, bootstrap policy rewrite or user runtime selection was
performed for this check; the owner-specified both-paths-fail trigger was not met.

FX final review04:25:04 confirms all3+3 launches map/paint with no bootstrap
errors, but desktop3 has paragraph glyph corruption (s appears as dots), so
this is launch availability, not full visual quality. The other five reviewed
scenes are correct. The launchd1/sshd4/session5 parent relationships also match
the saved44 boot; complete service timing/state equality is not established.


### Decoder context attribution candidate and DESK crash follow-up, 2026-11-16

The urgent launch check retains45; no further reboot occurred. FX subsequently
recovered DESK's exact minidump and matched the loaded binary UUIDs: a null
CFRunLoop yields fault address0x10 in CFRunLoopRemoveSource during AppRestart /
xpcom-shutdown. A network-reachability caller is in that path. This is a distinct
observed failure from MachCheckIn timeout, not evidence of GPU/devfs wedging;
the teardown cause remains open with the framework owners.

Source-only candidate integrations/puredarwin/m1-avd-context-stats adds ABI1
cap17 and a versioned128-byte context-owned read-only ioctl7. Real synchronous
session counters distinguish own jobs/results from concurrent decoding; no
IRQ writer, allocation, hardware programming, timer or teardown is added by a
query. PID/task ownership and a generation token reject foreign or recycled
contexts. The six existing ioctl sizes/opcodes are unchanged on host and the
actual arm64 SDK. Capture backing/IOVA/prepared byte fields describe ONLY the
live capture set, not all decoder allocations or post-destroy retirement.
An absent context is not proof of zero DART mappings or GPU quiescence.

The literal patched getter and decode function pass ASan/UBSan with fake
providers, including rejection atomicity, two-session interleaving and decode
success/HOLD/error/timeout/sync branches. The real kext SDK build and old/new
arm64 ABI assertions pass. These are offline results, not native attribution
or real DMA/interrupt tests. Media/KERN have the source-review contract; no
collection, install or machine test is reserved for it. Private build/test
receipts are under runs/sftb/avd-context-stats/. Kernel45 retains its original
AVD module. Native acceptance will require two real owned contexts, independent
packet/pixel/PTS counts, ownership/generation refusal and live capture queries,
as well as both installed Firefox routes painting before promotion.

DESK9 staged .next002 (8ed7da8a…) and daily002 (39dff5bd…) for the next boot.
The compositor running during this follow-up remains001; no relaunch occurred.


## Selected upstream12377 ports, 2026-11-16 (offline first)

Base stays c713d8fb; eight selected original commits apply cleanly in order to
a temporary fixture. No rebase, shared pin change, Nix reorganisation or NVIDIA
work is imported. Per-commit conflicts, SDK/host evidence, storage/image/tool
and Security details: `integrations/puredarwin/upstream-12377/README.md`.

| Port | Built/tested | What is still unobserved |
| --- | --- | --- |
| a25f0365 PCI scan | arm64 IOPCIFamily and literal fallback controls; emulated root-port xHCI publishes USB/HID and reads8MiB exactly. | No M1 bridge trial. kPCIHotPlugTunnel is excluded; an ordinary downstream bridge could qualify. This is not an I210 no-packets fix. |
| 2617f566 shared PCI/iflib/igc | arm64 em+igc SDKs and actual helper controls pass; full M1 em+igc variant fails the frozen16MiB KC slot (payload0x1250000), retained. ix is x86-only and unbuilt. | igc is I225/I226, not the I210. No change to descriptors/tails/DART sync/MSI; I210 packet cause remains open. Source/bundles stay staged until the dock track solves its image budget. |
| aaf3604d composites/heap tables | Generic and exact frozen deployed-source xHCI SDKs pass; unpatched frozen source reproduces SDK SHA65b7c308… byte for byte. Port uses offsets only/no fuzz. Actual helper/descriptor/IAD controls and dedicated QEMU USB reads/HID publication pass. | No physical dock USB device, keyboard typing, storage mount or cable cycle qualification. Shared PHY/DART/lifetime implementation is preserved. |

Observed versus built: the M1 still has qualified45 at this source checkpoint.
The default next candidate379sk links57 executable modules with the SAME core,
three changed executable UUIDs (PCI/xHCI/AVD diagnostics), one metadata change
(composite match) and54 unchanged executable UUIDs. em/igc are not loaded in
that candidate and no endpoint matching is enabled. Do not move the qualified
core or remove unrelated filesystem/GPU drivers just to force dormant dock
modules into a Firefox boot. The full59-module placement failure is separate
from successful driver compilation.

QEMU004 is a real disposable emulated root-port/xHCI/USB8MiB exact-read test,
with published USB HID nodes,28 filesystem I/O cases/7 resize steps, clean
unmount,29 independent Linux debugfs byte oracles and fsck-fn0. The raw4096-byte
node gives old ext4.util254 versus patched253; the earlier buffered-node control
accepted both and remains a failed full run. This is no M1 DART/MSI/physical HID
or DP evidence. Earlier output-namespace and missing boot-flag failures remain.

Next Firefox cycle notice is posted; no installation is claimed here. It will
keep45 rollback, use locks/UART/normal installer and both painted Firefox routes,
and couple pdm1tpscroll=20 to DESK10 (remove the argument with a desk9 rollback).
Thunderbolt hardware trials still require the owner's exclusive window; AVE
remains paused. The source survey does not close DHCP, dock keyboard, Samsung
DP or ten-cycle hotplug milestones.

### 2026-11-16: selected upstream fixes now qualified in Firefox kernel 46

The source-port checkpoint above was followed by an actual M1 installation.
The 57-module image changes IOPCIFamily, RavynXHCIPort and AVD context
statistics; the core and ext4 are byte-identical to qualified kernel 45.
Native GL hash/Vulkan checks passed five times each, and three desktop plus
three SSH Firefox launches painted the test document and its three colours;
all six captures were visually inspected. No synthetic input was used.
The new context API passed owned-context isolation and error-count tests with
zero real decode jobs. This does not qualify decode/import/fence retirement.
DESK10 reports the requested scroll conversion (0.206 mm steps).

The first package was rejected before XNU because its 621-byte command line
exceeded firmware revision 2's 608-byte capacity. The same kernel booted with
599 bytes after removing only `serial_video_mirror=0`: the exact ARM platform
source defaults that option to zero and takes the same console branch.
The rejected package and UART evidence remain private. Host packing now
checks revision-specific capacity before creating or truncating output.

Thunderbolt/AVE hardware trials remain paused: no new dock enumeration,
I210 traffic, dock USB typing, HPD/DP or hotplug evidence was obtained. The
full network-module variant remains blocked by the fixed collection slot;
em/igc are SDK-built and are not loaded in this image. Kernel 46 is normal,
qualified 45 is rollback, and all install locks/UART were released. Wi-Fi
power-save latency is the next authorized Firefox kernel priority, using the
same kernel 46 cohort and command line; see `m1-wifi-pm/README.md`.

### 2026-11-16: kernel 47 Wi-Fi priority installed, dock trials still paused

The next authorized Firefox cycle changes only PDBwfm from qualified 46;
the core and 56 other module UUIDs are identical, with the same 599-byte
command line and `pdm1tpscroll=20`. FXNET's power-save port uses the exact
deployed SDK source, with a byte-identical baseline reproduction and narrow
request-validation/firmware-error reporting corrections. Full SDK, actual
function sanitizer controls, required host checks and the disposable QEMU
PCI/USB/storage rehearsal passed before installation. QEMU links the new
Wi-Fi driver but has no matching Broadcom firmware.

| Path | Observed on the M1 | Built or pending |
| --- | --- | --- |
| Wi-Fi PM | Firmware GET mode 2, timeout 2000; runtime 200/2000 works | Real page-load and idle-power comparison belongs to FXNET |
| Radio latency | Same boot, one-second ping: median 179 to 5.85 ms; max 289 to 6.66 ms; 18 replies per setting, discard first three, no loss | Short comparison; ambient traffic uncontrolled, no Linux-parity claim |
| Desktop/GPU | Standard DESK10 `.next`, scroll conversion 0.206 mm; GL/Vulkan five each; clone slots three used, zero failures after tests | Initial desktop job exited 1; supported job start recovered it; cause remains open |
| Firefox | Three desktop and three SSH local pages mapped/painted/readable, all captures inspected, no input injected | Not arbitrary website, video, frame-rate or shutdown qualification |
| PCI/USB upstream ports | Source and SDK/QEMU coverage; retained in 46/47 | No additional physical dock enumeration or USB-device evidence |
| Dock Ethernet | No new packet evidence | em/igc bundles SDK-only; full collection variant exceeds fixed slot; no I210/DART fix claimed |
| DP/HPD/hotplug | No new evidence | Hardware work remains paused until the owner opens a dock window |

Qualified 46 is the immediate verified rollback. Clean shutdown reached
Linux on the second probe without external reset. The read-only filesystem
check retained its known error classes; no repair or clean-root claim.
UART and install locks were released at the logged return to SiliconForest.
AVE remains paused. See `integrations/puredarwin/m1-wifi-pm/README.md` for
reproduction, exact test scopes and the unresolved startup observation.


### 2026-11-16: Firefox kernel 48; dock hardware remains paused

Kernel 48 is qualified normal with 47 rollback. This authorized Firefox cycle
changes only ext4's journal-image read shortcut; core and 56 other modules,
Wi-Fi PM 2000, 599 boot-argument bytes and `pdm1tpscroll=20` are unchanged.
Source, SDK, sanitizer controls and real QEMU storage/USB oracles precede the
normal install. Native runtime cache controls/data, GL/Vulkan five each, six
Firefox paint/natural-quit checks and standard DESK10 `.next` pass. No synthetic
input. Known root fsck errors remain; no repair or clean-root claim. UART and
all install locks were released.

| Path | Observed on the M1 in this cycle | Built or pending |
| --- | --- | --- |
| GPU shared-buffer fences | Actual pending GPU producer, reservation snapshot and cross-process CPU consumer poll/data pass; UAPI already existed on 47 | Mesa/CGL/WindowServer activation and actual GPU-consumer/image-lifetime gates belong to client agents |
| Small storage operations | Metadata journal hits bypass disk reads; fsync/unlink stacks show journal flush waits and reclaim-triggered fsync | No demonstrated append latency gain; remaining queue/flush and batching attribution |
| Wi-Fi and trackpad | Firmware timeout 2000 ms; compositor scroll conversion 0.206 mm retained | No new physical pinch or idle-power result |
| Dock router/PCIe/I210/USB/DP/hotplug | No new hardware evidence | All Thunderbolt/dock hardware trials still paused; offline source/SDK evidence from earlier sections unchanged |

No dock or AVE hardware trial was scheduled. Current status, client handoffs
and private install receipts are in the coordinator files; the next dock
window still follows the existing ranked measurement plan, without claiming
kernel 48 advances physical dock milestones.


### Wind-down hand-over, 2026-11-16: dock paused; exact upstream dispositions

Kernel 48 remains normal, 47 is rollback, and no install/reboot is pending.
Its full hashes, exact restore path and standard desktop selectors are in
SF-R's wind-down section and `runs/m1-coord/status-CODEX-TB.md`. The driver
cohort still contains the earlier native dock enumeration work. The owner's
exclusive Thunderbolt window has not opened; winding down is not permission
to resume it. AVE remains separately paused. Firefox kernel authorization did
not authorize dock hardware trials.

The last physical dock evidence is the CalDigit TS3 Plus: Type-C negotiation,
ATC/NHI, authorized router and PCIe tunnel, with twelve PCI functions including
four xHCI endpoints and I210. I210 en1 reached 1 Gb/s link, but two submitted TX
descriptors advanced tail 0 to 1 to 2 while head stayed zero; no completed TX,
DHCP lease or usable Ethernet was established. There is no native dock USB
keyboard typing or stick mount, external HPD/EDID/scanout, or ten physical
unplug/replug cycles. Built helpers and QEMU USB do not change those facts.

| Requested upstream commit | Applied/conflicts | Build/test and installed state |
| --- | --- | --- |
| a25f0365 IOPCI | Source hunk applies cleanly to c713; no rebase or Nix reorganization. | SDK and actual fallback host controls pass; installed in kernel 46 and retained through 48. QEMU root-port enumeration passes. Ordinary bridges only; tunnel-class exclusion remains. No new physical dock qualification. |
| 2617f566 shared PCI/iflib/igc | Shared PCI helpers, iflib and selected driver source hunks apply cleanly; upstream flake wiring ported separately. | em and igc SDKs plus helper controls pass; ix is x86-only and unbuilt. Full M1 network collection fails the frozen 16 MiB slot, payload 0x1250000. Not installed. igc handles I225/I226, not the dock's I210; no no-packets fix. |
| aaf3604d USB | Clean generic apply; deployed frozen Apple/DART xHCI accepts offset hunks 120/124 without fuzz. | Exact baseline reproduction and patched SDK/host/real QEMU USB pass. Installed 46, retained 47/48: heap transfer arrays, IAD composites and mixed storage. No physical dock HID/storage/hotplug proof. |
| ca1fd5ed ext4.util | Exact source hunk applies cleanly. | SDK and actual reader sanitizer controls pass; raw 4 KiB QEMU control rejects with old utility and succeeds with new. Userspace tool not installed on M1. This is not an ext4 kernel commit/allocator fix. |
| aa82116c image | Exact converter source copied as a patch; old image Nix hunks apply in fixture. Shared image/installer recipes intentionally unchanged. | Synthetic and built 1.34 GB image conversions pass GPT/table CRCs and unchanged partition payload hashes. Not applied to owner disk. Existing installer already uses/checks 4 KiB GPT; do not run converter on it. |

The five requested source ports have all been started and preserved; none is
silently missing. What remains unstarted is physical qualification for paused
dock hardware, runtime utility adoption and an installable network collection.
Reproduction, source hashes, notices, clean-apply controls and retained failures
are in `integrations/puredarwin/upstream-12377/README.md`. No shared pin was
rebased. `c95aefa58` preserves the selected ports; later kernel 46 qualification
is recorded above. The older README's initial uninstalled-candidate paragraphs
are historical; this disposition table states the installed outcome.

The next five dock steps, only after a new exclusive window is authorized,
are: port the finite Ethernet diagnostic delta from DIAGNOSTICS.md onto the
then-current qualified Firefox kernel, preserving its fixes; capture paired
Linux/Darwin DART stream/TTBR/ring/tail/interrupt/bus-master/bridge measurements
and kill or confirm the ranked no-TX hypotheses; qualify the selected Ethernet
fix by actual completed packets, DHCP and bound-interface ping; admit the
selected dock PCI xHCI and test the owner's Keychron and read-only stick; then
complete native DP-provider/graph glue, HPD/EDID/modeset and independent second
scanout before ten physical dock cycles. Do not install the old CIO41
collection over 48: it predates the Firefox fixes and needs a current rebase
of its narrow diagnostic source, not a repository rebase.

Existing build/test entrypoints are the recipes in `m1-thunderbolt/` and the
commands in its `DIAGNOSTICS.md`, plus `upstream-12377/test-source.py` and its
`build-kexts.nix`, `build-qualified-xhci.nix` and `run-qemu.py`. Preserve the
no-packets failures and source/SDK-only USB/DP boundaries. The Linux Firefox
Speedometer/MotionMark/JetStream reference and physical pinch qualification
are also still pending a suitable later window; no benchmark score is invented.
The already-live dma-buf UAPI and pending-fence probe are documented in SF-AC
and `m1-dmabuf-fences/README.md`; no further kernel install is required merely
to introduce those ioctls.
