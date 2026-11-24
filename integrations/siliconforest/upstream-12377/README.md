# Selected SiliconForest ports, without rebasing

Base remains `c713d8fb9b048a38e2242d67e29b9957ced8c495`; surveyed upstream tip is
`41a24e40`. No checkout HEAD, shared flake pin, NVIDIA code or upstream Nix
reorganisation is adopted. `manifest.json` records exact selected source diffs,
their hashes, and before/after file hashes. The host test also applies all eight
original commits, including their old Nix hunks, in chronological order to a
temporary c713 fixture. They apply cleanly there. This does not update our tree
to the upstream tip or claim all its intervening changes are compatible.

| Commit | Port/conflicts | Build and host evidence | What it changes for SiliconForest |
| --- | --- | --- | --- |
| a25f0365 | Exact IOPCIConfigurator source hunk, clean on c713. | Real arm64 IOPCIFamily build; literal fallback block tests valid/absent vendor IDs, no-link guard and tunnel exclusion. | A replying function can override a clear link-active bit on an ordinary bridge. `kPCIHotPlugTunnel` is explicitly excluded; an ordinary downstream dock bridge might benefit, but tunnel classification must be observed. No new hotplug manager or packet/DMA fix. |
| 2617f566 | Shared fbsdkpi PCI helpers, iflib shims and igc/ix source adjustments apply cleanly. Nix's igc KC list addition is handled separately. | Actual arm64 em and igc build with implicit driver calls made errors; literal PCI helper tests pass. ix is configured only on x86 and has not been SDK-built here. | igc serves I225/I226; the dock's I210 remains em/igb. Existing PCI helper bodies move from LinuxKPI to common glue; this does not change I210 descriptors, tail writes, DMA synchronization, MSI or PHY handling. Save/restore and PME limitations remain explicit. |
| aaf3604d | Clean on c713; deployed frozen Apple/DART/lifetime xHCI accepts all hunks with offsets 120/124, no fuzz. | Generic and deployed-source arm64 builds; unpatched deployed-source SDK reproduces exact binary SHA65b7c308…; actual heap helper/descriptor walk plus IAD plist host checks. | Heap allocation replaces two nested transfer tables (160 entries each), with checked failure/RAII cleanup; configuration limit grows512→4096; EF/02/01 composites match the splitter; mixed storage/other interfaces use generic USB publication. No physical HID, mass-storage or unplug qualification. |
| ca1fd5ed | Exact ext4.util source hunk, clean. | Real arm64 utility build; actual reader with ASan/UBSan covers512..65536-byte fake devices, short reads, allocation failure and block-size fallback. | Raw-device probes read whole blocks from offset0 and take the superblock at1024. Buffered block-device reads can already accept the old request; do not confuse that with the raw-device failure. This changes a userspace probe, not ext4 allocation, fsync or repair. |
| aa82116c | Converter copied as an exact source patch; old image Nix hunks apply, but no shared image recipe is changed. | Python converter runs on synthetic regular images and a built1.34GB virt rehearsal image; both GPT/table CRCs and partition byte spans are checked. No physical disk operation. | Converts a newly constructed512-byte GPT image to4096-byte GPT units while preserving partition bytes. Upstream's T8103 image uses that format; our existing M1 installer already checks its4096-byte GPT and installs boot files without rewriting it. The converter is not our installer or a general repair tool. |
| 0163b2db | Source and small package recipe ported; explicit arm64 toolchain/SDK/library arguments replace the upstream flake wiring. | Real arm64 sfres build; whole CLI with fake CF/CG providers covers list, highest/explicit refresh, IDs, refusal and no modes. | A shell frontend to main-display CoreGraphics mode APIs. It does not provide EDID, train DP, implement per-output modesetting or prove our compositor's real mode-switch contract. Not installed or run on the M1. |
| a08eb9b6 | Only the real XNU content_protection forwarding header is retained. | Actual Security, trustd and sftlscheck build succeeds. Dependent full arm64 SystemConfiguration/configd/bootplib/ipconfiguration/ipconfig build also passes (isolated header-patched source scope; no shared pin change). | Removes the missing trustd header blocker without a success stub. No callback implementation or runtime FX-003 fix is claimed from this header. The Firefox stream owns its independent network backend. |
| 63a61e89 | Its two-line reversal cancels a08's git CoreFoundation/SystemConfiguration arguments exactly. | Full source fixture verifies nix/arm64.nix returns byte-for-byte to c713 for those changes. | There is no CoreFoundation code fix in these two commits. Keep the erroneous git arguments out; keep the independent trustd header. |

The patches retain upstream source notices. IOPCIFamily keeps Apple's APSL2
header, RavynXHCIPort its ravynOS MIT notice, and the new GPT/sfres/trustd files
their SiliconForest MPL2/BSD2 notice. Common glue's MIT notice and the full upstream
shim rename/license diff remain in the exported patch; this is not a new lab
relicensing decision. Existing FreeBSD/Linux file notices stay untouched.

## Candidate and capacity limit

`build-candidate-kc.nix` produces an **uninstalled**57-module M1 candidate over
qualified45. Its immutable core is identical. Only IOPCIFamily, RavynXHCIPort
and SFAppleAVD executable UUIDs change; the composite driver's metadata also
changes. The other54 executable UUIDs and52 kext sources match45. AVD contains
the independently reviewed per-context diagnostics from
`../m1-avd-context-stats/`, not a decode policy change. Existing devfs, killable
waits, private-UPL, EOF fix, HID/pinch and speaker-cap-default-off remain.
Enumeration metadata preserves bridge/Wi-Fi admission and does not enable a
dock endpoint. No AVE module is introduced.

The full variant (`--arg withNetwork true`) also tries to link the newly built
em/igc modules, with all their PCI matching removed. It **fails real prelink
placement**: payload0x1250000 exceeds the frozen core's16MiB KC slot. Both carry
about884736 bytes of data segment each in addition to code/link metadata. The
SDK builds passed; that does not make this full collection installable. It is
not selected for the next Firefox boot. Keep the network source/bundles for the
paused dock track; solve its image budget in that track rather than moving the
qualified core or dropping unrelated filesystem/GPU drivers during this port.

The next actual kernel cycle must qualify the PCI/USB candidate in QEMU first,
then follow the existing notice/locks/UART/normal-installer/rollback rules and
both painted installed Firefox launch routes before promotion. DESK10 requests
`sfm1tpscroll=20`; append it only with verified desk10-or-newer running/next
binaries, and remove it when rolling the compositor back to desk9. It is not
an ABI change or proof of physical pinch/scroll quality. No arguments or kernel
are installed by these build recipes.

QEMU_REHEARSAL: run004 PASS: PCIe-root-port xHCI, synthetic8MiB USB exact reads, published HID nodes,28 I/O+7 resize steps, clean unmount,29 independent debugfs byte oracles and e2fsck-fn0. The actual4096-byte raw-node utility control returns old254/unrecognized versus patched253/IO-success. Buffered-node003 accepted both and remains a failed full run. No physical keys were injected. Earlier failed/incomplete attempts are retained.
The dedicated guest uses a PCIe-root-port→xHCI→synthetic8MiB USB disk and USB
HID, plus a separate4096-byte scratch disk for the existing28 I/O/7 resize/
29 independent disk-byte oracles. The old/new utility control must probe the
raw scratch node. A positive buffered-node result is not the negative control.
No QEMU result establishes M1 dock enumeration, DART/MSI, I210 packets, real
keyboard input, DP, HPD or hot-unplug safety. Thunderbolt and AVE hardware
trials remain paused. There has been no M1 use for this source-port phase.

## Reproduce offline

```sh
python3 integrations/siliconforest/upstream-12377/test-source.py
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/upstream-12377/build-kexts.nix --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/upstream-12377/build-qualified-xhci.nix --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/upstream-12377/build-security.nix --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/upstream-12377/build-systemconfiguration.nix --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' integrations/siliconforest/upstream-12377/build-candidate-kc.nix --no-out-link
python3 integrations/siliconforest/upstream-12377/run-qemu.py --output runs/sftb/upstream-12377/new-qemu-run
```

Private receipts are in `runs/sftb/upstream-12377/`. Preserved preparation
failures include the incomplete rename fixture, wrong CMake target/platform
filter, Nix string-context override retaining old broken Security, and the
56-kext guard refusing58. QEMU001/002 failed before boot on the staging tool's
output-namespace guard. QEMU003 booted and verified USB8MiB but failed the full
gate: missing scratch boot flag and a buffered-node utility control where the
old reader also succeeded. The first4KiB image verifier had a local builtin
name shadowing error; the corrected verifier uses separate byte-count names.
These failures are not silently relabelled as passes or upstream regressions.
