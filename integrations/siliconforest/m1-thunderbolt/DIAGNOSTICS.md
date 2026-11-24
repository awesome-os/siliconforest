# Deferred exclusive dock window

Machine trials are paused by the owner on 2026-11-15. This document authorizes
no install, reboot, Linux visit or target workload. CIO40TX finished and qualified
NON-AVE CIO29 was restored; diagnostic artifacts below are local builds only.

The owner subsequently made Firefox the first priority. This is the retained
LATER dock plan, superseded for the next window by the single combined
AVD/K14/HID image and Linux reference in
[../m1-firefox-kernel/README.md](../m1-firefox-kernel/README.md).
No diagnostic-first or separate K14 install is currently planned.

## Captured result and source comparison

CIO40TX authorized the TS3 Plus and enumerated 12 native PCI functions. I210
reached UP/running and 1 Gb/s, but DHCP failed. Two 342-byte packets reached
`igb_isc_txd_encap`; the FreeBSD and IOKit handoffs returned zero. Actual TDT
advanced 0 → 1 → 2; TDH stayed 0. RX head stayed 0, tail 0x80. No packet
completion, OFFER, ACK or IPv4 address was observed. Link is not data transfer.

Compare against Omacom `linux-aurora` **6a08a08e57558083f552733a5276d71b1a4d8ff6**,
not an unpinned Linux tree. The saved Linux and Darwin captures stay private.

| Path | Linux source and operation | Frozen native source and observed difference |
|---|---|---|
| Ring allocation | `drivers/net/ethernet/intel/igb/igb_main.c:igb_setup_tx_resources`, `igb_setup_rx_resources`: `dma_alloc_coherent` | `fbsdkpi/xnu/sf_fbsd_os.cpp:sfos_malloc_contig`, `sf_owned_contiguous`, `sf_owned_map`: retain the exact allocator object for coherent rings, actual native mapper/command; 2 direct and 258 bounce maps observed on39. Requested WC/normal-NC is not an actual CPU attribute measurement. |
| Packet allocation/direction | `igb_tx_map`: `dma_map_single(...DMA_TO_DEVICE)` and fragment mapping | `haiku/freebsd_network/bus_dma.cpp:bus_dmamap_sync_etc`: CPU→Haiku bounce before `sfos_dma_sync(...,1)`; native source→owned bounce then `IODMACommand::synchronize(kIODirectionOut)`. POSTREAD reverses native→Haiku→original. Every actual prepare/sync/retire error must remain a failure, with backing retained. |
| Descriptors | `igb_tx_map`: little-endian buffer IOVA, type/length, final RS/EOP; `dma_wmb()` before tail | `SFFBSDem/dev/e1000/igb_txrx.c:igb_isc_txd_encap`: little-endian address/command/olinfo; captured342-byte advanced descriptor, second has RS. Iflib sync and normal-NC `dsb sy` precede posted BAR doorbell. A barrier does not clean a WB cache line. |
| Queue programming | `igb_configure_tx_ring`: length/count, base, TDH/TDT0, thresholds and queue enable | Captured native TDBA0x04000000, length0x4000, TXDCTL0x0201011f, TCTL0xa50400fa. Linux length0x1000, TXDCTL0x02100108, TCTL0xa503f0fa. Both queues enabled; differing thresholds/length are not by themselves an invalid ring. First32 retained CPU bytes in the diagnostic will check the actual ring. |
| RX | `igb_configure_rx_ring`, `igb_alloc_rx_buffers`: coherent ring, streaming RX ownership, barrier before tail | Captured native RDBA0x04004000, length0x4000, RXDCTL0x02040808, RCTL0x04448002, RDH0/RDT0x80. Linux RXDCTL matches; Linux RCTL0x04448022 and progressing head/tail. RX filtering can explain missing receives, not stationary TX head. |
| Interrupts | `igb_request_irq`, `igb_configure_msix`, `igb_irq_enable`: vector/mask/auto-clear setup and handler | `SFCIOPCIMSIController` owns AIC818..849; one real MSI permitted for the actual I210, doorbell0xfffff000. Actual controller counters are captured independently. ICR/EICR reads clear causes: never add a second diagnostic consumer. Sample IMS/EIMS/EIAC/EIAM/GPIE/IVAR and native MSI config instead. |
| MAC/PHY | `igb_setup_tctl`, `e1000_82575.c` link/reset paths | Native STATUS0x00280383 vs Linux0x00280783 differs in bit0x400, defined PHYRA (PHY Reset Asserted); **native lacks this bit**, Linux has it. Both have GIO_MASTER_ENABLE0x80000. Do not label the delta a native asserted reset or disabled bus master. Capture CTRL/CTRL_EXT/STATUS/TIPG and existing link state; do not read PHY/NVM through a new MDIC transaction. |
| Native PCI bridge | `drivers/pci/controller/pcie-apple.c:apple_pcie_enable_device`, RID2SID reservation and root window setup | Native inventory uses different bus numbers from Linux. Compare actual BDF, command BME/MEM, BAR and every upstream bridge command/bus/memory window; do not match Linux BDF literals or confuse CPU BAR routing with device-to-host DMA routing. Successful MMIO proves only the former. |
| Stream choice | Same `pcie-apple.c`, `PORT_RID2SID_VALID | sid<<16 | rid`, DT iommu-map-mask | Native reserves RID→SID with `rid & 0x3f`. Native I21003:00.0 and Linux06:00.0 both map to SID0. Check live64 RID slots, not a cached initialization property. |
| DART layout | `drivers/iommu/apple-dart.c:apple_dart_hw_t8103_usb4`: T8020, APPLE_DART,64 SID,36-bit output, TCR0x100, USB4 TTBR0x400,4 roots/SID, no bypass | `sf_pciec_dart_sid0.h`: blocked64-SID bank, SID0 TCR0x80, TTBR valid/root>>12, explicit lower/upper flush stream words. This is not the NHI SID1/0x200 layout and not DART2. Live64 TCR/RID words, SID0 roots and error/fault address will test the actual state. |
| Table publication | `apple-dart.c:apple_dart_finalize_domain`, `io-pgtable-dart.c`: coherent table walks | `SFCIOPCIDartMapper` retains16KiB table pages, T8020 PTE format, publishes with `dsb sy`, checks/flushes before success. Linux uses `coherent_walk=1`; absence of a CPU table-cache flush alone is not a demonstrated bug. CPU table walks do not prove the device fetched those PTEs. |

## Ranked hypotheses and one discriminating capture each

These are ordered investigation priorities, not established causes. A negative
measurement narrows a hypothesis; it is not a blanket proof that all DMA works.

| Rank | Hypothesis | Single measurement in the diagnostic boot | Decision |
|---|---|---|---|
| 1 | Actual CPU backing remained WB despite requested normal-NC, so the device cannot see current descriptors | At the first nonzero TDT, retain the mapper's ring object and capture `AT S1E1R` PAR attributes, physical segment and first32 CPU bytes | PAR fault or physical mismatch invalidates the probe. ATTR0x44 supports actual normal-NC at that VA; a WB result establishes a policy/attribute mismatch, but causal proof still needs a later isolated fix and hardware progress. Never flush a device-written coherent ring indiscriminately. |
| 2 | Wrong requester/stream/root/PTE or a late DART fault prevents descriptor fetch | Same bounded submission snapshot:64 live RID slots/TCRs, SID0 TTBRs/error/fault address, expected backing PA and CPU walk for first2/last2 live mappings | A valid fault with ring/packet IOVA and inconsistent live mapping localizes failure. Match current BDF, not Linux bus number. Matching CPU tables and no valid error do not prove a hardware walk or exclude a fabric issue. |
| 3 | Software submitted a valid-looking descriptor to the wrong ring or malformed actual bytes | Pair captured TDBA/TDLEN/TDH/TDT with first32 bytes of the retained mapping whose IOVA equals TDBA | Wrong descriptor IOVA/endianness/length/EOP/type or ring bounds confirms a construction/backing mismatch. Correct CPU bytes narrow it; they do not prove visibility to the device. |
| 4 | BME, upstream command/window or tunnel forwarding is not actually enabled when TX starts | Finite same-path native endpoint/ancestor PCI config snapshot after the guarded doorbell | Missing BME/MEM or an out-of-window BAR is an actionable configuration error. Valid windows and functioning BAR accesses do not prove inbound DMA completion. Existing authorized tunnel/RUN/link checks remain required. |
| 5 | Queue/MAC transmit remains disabled, misprogrammed or PHY state blocks data despite link | Compare the same non-destructive CTRL/STATUS/TCTL/TXDCTL/length/head/tail/TIPG/switch-control words with the saved Linux snapshot | An enable/reset/ring-bound inconsistency is testable. Threshold differences alone are not proof. TDH0 makes an interrupt-only explanation lower priority. |
| 6 | MSI/mask/handler delivery prevents reclaim or RX service | Compare IMS/EIMS/EIAC/EIAM/GPIE/IVAR, native endpoint MSI capability and independently captured AIC fired/rearm counters around the failed DHCP attempt | Hardware head/DD advancing with no handler narrows this to interrupt service. TDH0 and no DD cannot be repaired merely by a completion interrupt. Existing real handler owns cause reads/ACK. |
| 7 | RX filter or bounce ownership prevents replies, after TX actually works | One bounded RX ring/control snapshot plus actual POSTREAD/packet counters and strict DHCP transcript | Only promote after TX hardware progress. A link and output byte count cannot substitute for emitted/received packets or a DHCP ACK. |

The snapshot executes only at the existing **eight** doorbell trace admissions.
It creates no timer, worker or user-controlled address probe. It refuses IRQ
context, foreign endpoints, invalid ABI size/version and lost power/ownership.
Mapper then controller locks serialize retained backing. Native PCI config reads
run outside those locks. Fixed DART reads do not ACK faults, flush or alter tables.
The PAR helper preserves DAIF and PAR_EL1 and temporarily masks asynchronous
exceptions only around its short translation sequence. PAR reports translation
attributes; its field definition is documented in the
[Arm architecture manual](https://documentation-service.arm.com/static/649ae5b238511951cb799288).
The first32 bytes are CPU observations, not a device-memory snoop. Private UART
and run captures may include packet data; none belongs in Git.

Host fixture: `python3 host-test/dma-inspect.py` executes the actual snapshot
method with fake registers/retained memory under ASan/UBSan. It does not execute
native AT or hardware MMIO. Native SDK builds compile those separately. Frozen
baseline preparation uses `tools/prepare_pciec_dma_inspect.py`, which copies only
the diagnostic method/dispatch/whitelist/prototype, preserving the earlier
Ethernet mapper policy rather than importing uninstalled USB changes.

## Exact first-window sequence and time

Reserve **12 minutes exclusive**, only after the owner explicitly opens it.
No current reservation or automatic waiter exists. All builds, comparison proofs,
uploads and recovery inputs must be ready before taking hardware locks.

1. Read fresh coordination log; acquire core shared, GUI exclusive, then bench.
   Require fresh owner-inactive counters and five minutes quiet/startup, actual
   standard desktop and the expected installed kernel. A refusal releases all
   locks without reboot or recovery. Announce the concrete boot at least2min
   ahead inside the newly authorized window.
2. Normal reboot to Linux; in **one** installer session save the fixed read-only
   Linux comparison, install only the diagnostic collection through
   `sf-bootbin-install.sh`, preserve the qualified29 recovery copy, unchanged
   boot arguments/boot.bin and Linux default. Budget2–3min. No AVE or optional
   media/HID/USB/DP functional changes in this diagnostic kernel.
3. Start/reap-owned UART after successful installer checkpoint and before first
   SiliconForest boot. Boot one-time, verify actual.next/process/socket/inject.
   If the existing standard launchd job is absent, allow one explicit start of
   that existing job; record its return separately and require actual readiness.
   Never replace desktop scripts or claim launchctl rc alone is readiness.
4. Authorize TS3; require all12 real PCI functions, real mapper/MSI and late BME.
   Name the actual I210 interface, bring it up and run the existing bounded DHCP
   attempt. Capture raw handoff/descriptor/doorbell/DART/PAR/RID/PCI/mask records,
   independent MSI counters, link and interface packet/error counters. Budget1min.
   No pointer/key injection, owner browser benchmarks or unrelated GPU load.
5. If DHCP succeeds, require native IPv4 and16 bound-interface gateway ICMP
   replies using the prepared helper (no en0/default-route fallback). Otherwise
   retain the failure without extending a retry loop. Do not modify this kernel
   live or switch several DMA policies in one boot.
6. Restore qualified29 through the normal installer after this diagnostic,
   verify SiliconForest and actual standard.next/socket/inject, stop/reap the
   owned UART, release all locks and announce. Budget3–4min, plus contingency.

Success does not automatically authorize more boots. Use the remaining window
for capture analysis on the workstation; schedule an isolated corrective kernel
only if the owner granted enough exclusive time and the evidence selects it.

## USB and display follow-on gates

Dock USB sources and native SDK already exist (exact FL1100 matching, selected
upstream route, shared mapper, late BME, real MSI with one-second backstops,
retained/quarantined DMA and read-only storage policy). Actual allocator,
controller and composition fixtures pass. No Darwin USB device has enumerated.
Keep that SDK separate from the Ethernet-only diagnostic. After actual Ethernet
qualification, build the coupled collection preserving the shared PHY broker.
One USB trial needs roughly10–12min including recovery. Capture actual selected
endpoint BAR/command/MSI, xHCI halted/reset/run status, command/event ring
completion, PORTSC connection/speed and device/config/HID descriptor results.
Do not read event rings through a second consumer. Generic descriptor-based
`SFUSBHIDDevice` matches above legacy boot keyboard/mouse and sets report
protocol; preserve that source path for the attached Keychron. Do not open its
queue from a probe and steal real input. Ask for physical typing only after actual
enumeration, then a stick in the **same qualified dock port**, mounted read-only.

DP needs more native glue before a useful hardware boot: actual dcpext
DT/mailbox/clock/carveout/DART resources and route graph, a real DCP tunnel
provider, native ordered DP-IN callbacks, and a separate validated ATC crossbar
mapping under CIO's existing TBT lease. Never acquire a conflicting direct-DP
lease. Keep physical Type-C1/DPTX1, root DP-IN order, RemotePort and DFP identity
distinct. Never poll DPIN+0x18 (clear-on-read). Activate only in the order of the
pinned driver; the early-wake hang is documented there.

DP source/host/SDK preparation is not external-display support. Before requesting
a20min display slot, require actual provider and resource code to build. The boot
must capture real DP tunnel/HPD/training/EDID then `--outputs` on the **same** DRM
card, K1–K13 independent CRTCs/flip ids/hotplug/vanished-link behavior and overlay
fallback. K14 is a separate trackpad gesture ABI, not a prerequisite for lighting
the Samsung. True dock cable removal/replug10 times remains a later physical
qualification; software retrain and pinned-until-reboot teardown do not count.

Firefox's Linux stock benchmark reference is noted for an authorized Linux
window with a separate uninterrupted allowance (up to20min), installed browser
only and matched viewport/scale/power/renderer. It cannot fit the diagnostic's
short installer session and must not cause an extra reboot. No Linux score exists.

## Ready local artifacts and remaining provider work

Diagnostic source is committed as `cd9674da7`; the local collection is
`/nix/store/nm95yi9gdi9lzz4lys9as5fyy9bxarls-puredarwin-kc-cio41dma-access-guarded`.
The packed kernel capsule SHA256 is
`cb7f057c2aa8746275577c625ff30075626dbb9f8050129c9d361c76515dde2a`.
It keeps qualified29's605-byte configuration and byte-identical boot.bin, no
AVE/sfave and58 executable modules. Only CIO/Ethernet differ from40; the other56
UUIDs match. Both complete native SDK inputs retain identical non-debug sections
through collection preparation, and all57 kext strip proofs pass. This artifact
is **not uploaded, scheduled or authorized to run** while the machine is paused.

Separate commits `121981a31` and `14b554684` prepare K14 and the native DCP
tunnel ABI/epoch seam. Neither is included in the diagnostic collection. K14
has actual-decoder host geometry/lifecycle checks and a native SDK. DCP has
actual-source host identity/callback/retirement checks and a native SDK whose
final ARM64 bridge directly calls pinned `apple_dcp_tb_dp_tunnel`. The provider
class, graph publication and PHY delegation remain; an uncalled bridge cannot
create a display route.

The next provider implementation must account for these source boundaries:

- `linux/sf_providers.h` in the current private layer registers clocks, reset,
  power and regulators; it supplies no generic PHY, display mux or Type-C mux
  backend. Existing fixed-clock support cannot stand in for those services.
- Pinned `linux/phy/phy.h`, `linux/mux/consumer.h` and
  `linux/usb/typec_mux.h` have CONFIG-off inline getters/registration returning
  errors (`ENOSYS`/`EOPNOTSUPP`). Compiling dcp-fabric is not provider support.
  Enabling defines alone must not replace those failures with fabricated success;
  real retained native provider handles and operations are required.
- The actual Linux route probe resolves PHY and mux before connector wiring and
  Type-C mux registration. Keep route@1's index1 in property arrays even when
  it is the only available route. Publish all aggregate DRM resources before
  the component master's binding; adding a second standalone DCP card later
  does not meet the requested same-card topology.
- RTKit initialization's first retained native host is a shared IRQ/workloop
  anchor. Mailbox resolution is keyed by each consumer's actual path and
  resource/IRQ proof; do not equate that shared anchor with a mailbox alias
  or overwrite it merely to add dcpext. Per-client lifecycle remains required.
