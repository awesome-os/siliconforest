# One Firefox kernel on qualified CIO29

Current2026-11-16 01:29Z: devfs-only12168f25… is the qualified normal kernel.
The AVD5238 import/cache image42 booted and advertised capability, but the first
strict shared capture PANICKED during IOMemoryDescriptor teardown. It was rolled
back. No zero-copy or physical-pinch claim. Full evidence and recovery are below.

build-final-kc.nix produces the next safe image43: devfs+K14/HID, reviewed
killable GPU waits (KERN0f446bdcd) and optional speaker-cap boot argument (default0 per owner); exactly7
changed modules/50 unchanged versus CIO29. The ORIGINAL CIO29 AVD is retained,
with its exact module UUID verified by verify-baseline-avd.py. The failed
AVD import/CMO/IRQ module is excluded, so the import-only cache-link gate is
explicitly inapplicable; no stubbed cache success or capability is substituted.
The old five-module recipe remains preserved for audit and must not be installed
again until its UPL failure is fixed. The following scope/contract sections
record that failed import candidate's source intent, not native qualification.

Owner authorization permits installs as soon as ready with2-minute notice,
core/gui/bench locks, UART, rollback and standard-state recovery. Idle and hourly
waits are lifted; Thunderbolt and AVE stay paused. build-final-kc.nix pins all
three reviewed replacement SDKs. Its normal installer and actual native gates
remain the kernel owner's responsibility; this directory does not install.

## Exact scope

| Module | Prepared change | Preserved baseline |
|---|---|---|
| SFAppleAVD | Whole capture-set GPU BO DATA import, retained task/pins, alias validation, DART rollback/quarantine, IRQ ownership fix, explicit cache ownership | ABI1, codec implementations, private-allocation fallback, root-only node; no encoder |
| SFM1SPIHID | K14 pinch/rotation/centre/end/cancel, version2 gesture ring | Exact installed Touch Bar source, keyboard/Fn/transport and existing typing hooks |
| IOHIDFamily | FIONREAD/select on existing keyboard/mouse queues, wake after publication | Exact installed native-publication rev002, atomic readiness and early-input drops |

No SPI report cadence, input polling timer, kernel thread, new buffer-export API,
GPU driver, DCP provider, dock driver or kernel core implementation is added.
DESK7 measured physical reports near8ms with no sequence loss/bursts; its reader
scheduling caused the measured lateness and was corrected in its own stream.
HID readiness removes the need for the compositor's idle4ms polling when adopted.

`build-avd-import.nix` uses immutable SDK155 source
`/nix/store/y6pr54piv1wc2w2nkqcris680jy8yq02-puredarwin-apple-avd-tree`.
`prepare-avd.py` verifies **all52 files before and after** the narrow patch;
only platform/core/OS API files change. The patch includes the independently
reviewed IRQ41b6f2d34 change; SDK155 itself predates that fix. Codec/Linux files
remain byte-identical to pinned Omacom6a08a08e. The patch keeps their GPL licences;
the build/verification tools are MIT. No firmware or captures are included.

`build-k14-touchbar.nix` starts with the installed immutable Touch Bar source
fiv3mhsa74djlc6pfw6cv80160jqp755 and changes only four multitouch files.
The earlier standalone K14 SDK x6q4… is **not** the installed Touch Bar variant
and must not replace this input. `build-hid-readiness.nix` first applies the
original rev002 hash-checked publication patch, then readiness, and preserves
actual post-patched source in its output for host verification.

## Import, cache and lifetime contract

The media request chooses **import of caller-owned GPU BO pixels**, not a new
AVD dma-buf export. Pass the BO data address **+16384**, excluding its shared
atomic header. Import the entire exact capture count before any MAP/HOLD/decode;
use driver stride/UV offset/full sizeimage including private tail, rounded16KiB.
Keep shared header atomics WB. Virtual/physical aliases and existing AVD-private
page overlap refuse transactionally; no whole-BO uncached alias is created.

The frozen source audit is in [SOURCE-AUDIT.md](SOURCE-AUDIT.md). Ordinary
`flush_dcache`, `flush_dcache64`, `performOperation` and DART/IO barriers are not
real cache maintenance in this exact coherent-I/O kernel. Existing
`CleanPoC_DcacheRegion_Force(vm_offset_t,size_t)` enables Apple DC MVA operations
and executes DC CIVAC. The patch invokes it on the retained DATA mapping in
16KiB chunks, bounding each preemption-disabled interval to one page:

1. After initial zeroing, before publishing any capture translation.
2. Before a destination is handed to the decoder, before job publication.
3. After confirmed non-HOLD completion, **before** any CPU grey-chroma fill.
4. After a grey-chroma CPU fill, before returning output to a GPU consumer.

CPU/GPU **writes must be excluded during decoder ownership**. A CPU read may
populate only a clean line; clean+invalidate after device writes must never
write back stale dirty CPU pixels. A diagnostic/hash reader must wait for the
completion boundary too. GPU reads of held frames/references require existing
media leases and real GPU completion fences; an active GPU reader prevents
capture reuse. Consumer acquisition precedes producer release. Header counts
and `IsInUse` alone are not fences. The kernel cannot revoke arbitrary writes
through another process's shared mapping; this is a cooperative DMA API contract.

The retained kernel mapping maps the original VM object without copying, keeping
its device-pager GEM/file/VMA hold alive after caller unmap/task death. The UPL
wires real individual private-page PPNs; it does not assume contiguity. Normal
teardown unmaps DART successfully before completing/releasing pins and the kernel
alias. Any imported decode failure is conservatively treated as possibly active
DMA: poison admission and retain all backing/maps/IOVAs until reboot, including
a reset failure. INFO withdraws import capability after poison. This trades a
reboot after even a malformed imported job for avoiding uncertain reuse. GPU
consumers separately retain the original BO through their existing fileports/
GEM mappings and completion leases.

These are source and host conclusions. They do not establish that native BO
UPL admission succeeds or that AVD→AGX visibility and frame reuse work on hardware.
The existing CPU-copy→GPU path is not proof of the new device-write path.

## Reproduce locally

```sh
nix-build --extra-experimental-features 'nix-command flakes' \
  integrations/siliconforest/m1-firefox-kernel/build-avd-import.nix --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' \
  integrations/siliconforest/m1-firefox-kernel/build-k14-touchbar.nix --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' \
  integrations/siliconforest/m1-firefox-kernel/build-hid-readiness.nix --no-out-link
```

For combined linking, supply the private recorded qualified29 `buildCommand` as
`--arg frozenPhase ./runs/m1-usbc/firefox29-frozen-build.sh` to `build-kc.nix`.
Its SHA256 must equal f441db4633e65c3fe18016de21e8a883fec2dde9979f42c029fc8b46d452022a;
changed/unknown phases refuse. This is the exact installed baseline recipe,
with only three kext arguments replaced. Local stripping verifies every
non-debug section address/size/content in all56 input kexts, original overlap
validation still runs, and plist compaction preserves semantic values.

`verify-collection.py` requires57 executable fileset entries, exactly three
changed UUIDs and54 unchanged including the kernel/AGX/DRM/DCP/NVMe/CIO.
`verify-cache-link.py` follows the final ARM64 BL, import stub and actual
64_KERNEL_CACHE chained pointer to the existing kernel Force routine. A symbol
name or a successful relocatable SDK alone is not treated as a resolved call.

Host checks against a prepared source tree:

```sh
python3 integrations/siliconforest/m1-firefox-kernel/test-cache-order.py PREPARED_TREE
python3 integrations/siliconforest/m1-firefox-kernel/test-native-ownership.py PREPARED_TREE
python3 integrations/siliconforest/m1-firefox-kernel/test-replay.py PREPARED_TREE \
  runs/avd/rec/bbb-1080p-h264-10s.pdaj \
  runs/avd/rec/bbb-1080p-hevc-10s.pdaj \
  runs/avd/rec/bbb-1080p-vp9-10s.pdaj
python3 integrations/siliconforest/m1-hid-select/host-test.py HID_SDK/nix-support/source --prepared
sh integrations/siliconforest/m1-trackpad-child/multitouch/run-host-tests.sh
```

Actual extracted decoder/grey bodies pass12 ordering/failure cases under
ASan/UBSan; missing pre/post hooks are rejected counterexamples. Extracted native
sync/free/decode bodies test DATA bounds/header exclusion, page chunks, legacy
fallback and retained pins on DART/decode failures. Frozen IRQ actual-source
10 cases pass and old-placement2 races fail as expected. Three codec recordings
replay900 decodes twice with identical instruction streams and no leaked host
allocations; all12 Linux identity files match. This proves neither output pixels
nor physical DMA. HID actual prepared source passes4000 publication/select races
plus pre-publication rejection; K14 actual decoder passes3601 angle oracles and
lifecycle/legacy regressions.

## Local artifacts, not installed

- AVD SDK: `/nix/store/5238qcrsg1syilcp8h613qa6sdqn2d0d-puredarwin-firefox-avd-import-cache-0.1`.
- Touch Bar + K14 SDK: `/nix/store/42j1wmvkbyfmj0hvq5m9d9s0v5xazg39-puredarwin-firefox-k14-touchbar-0.1`.
- Publication + readiness SDK: `/nix/store/zc02mxv57xr0snv68pzdl5pynkidmd6i-puredarwin-firefox-native-hid-readiness-0.1`.
- Combined collection: `/nix/store/0lam9mk3qhihx7ziwi6q8wm77wv8y7dv-puredarwin-kc-firefox29-import-pinch-readiness`.
- Collection SHA256: `30e04e6209d0147b604b6616cff7cdb20ec97d597949ab486e58ef40676ed1a3`.
- Packed capsule SHA256: `0befc6ffb6473f050b63c81453f52a3e61cfc4bbbb88721fc8cbf8e73a4caa82`
  (private `runs/m1-usbc/kit-firefox29`; not uploaded or installed).

The collection's `nix-support` contains UUID, cache-link, section-stripping and
prelink proofs. Private raw logs and capsule are under `runs/m1-usbc/`; no upload,
reservation or target process is created. Qualified29 capsule a149e487… remains
the recovery and last verified installed image.

## Next exclusive window: Firefox first, about50 minutes

This plan supersedes the old diagnostic-first/isolated-K14 install order.
No elapsed time or completed build opens the window.

| Step | Budget | Evidence / stop condition |
|---|---:|---|
| Fresh owner/coordinator announcement, log/guards, core→GUI→bench |2min | Explicit window, no competing install; do not infer permission from time |
| Normal Linux installer visit; installed Firefox reference if present |up to30min | Stock Speedometer3.1/MotionMark1.3/JetStream2.2, fresh profile, visible desktop, raw scores/renderer/power/kernel/version/actual viewport and scale; skip if absent, no install or extra reboot |
| Install the ONE combined capsule using normal installer; UART before first boot |3min | Preserve605-byte args/boot.bin/defaultLinux, no AVE; one-time bless, serial retained |
| Standard desktop and CLI admission checks |2min | Actual `.next`/process/socket/inject state, module identities, no faults/storm; FIONREAD feature detection consumes no events |
| Imported output qualification with media/FX clients |8min | Actual GPU FD, all format/import/alias guards; old/new patterns, held64/pool-pressure/flush/producer exit; independent codec hashes/PTS,1080p60/4K30,0 copied pixels and matching completions/no stale/timeouts/DART |
| Owner physical pinch/rotate + normal pointer/scroll/keyboard regression |2min | Real ring2 and compositor `WMGESTURE pinch` delivery; no synthetic pointer/focus takeover, no cadence claim without measurement |
| Retain results, restore qualified29 on failure, verify standard state/UART reap/unlock |3min | All failures preserved; no speculative second candidate, all locks released |

`linux-reference.py` defaults to printing this reference plan. Its `--execute`
mode runs locally on arm64 Linux with a new profile and the existing benchmark/
Marionette implementation, sends no injected input, never touches owner profiles,
and never installs a browser. Stage its two existing Firefox Python dependencies
beside it before the window. Match SiliconForest viewport/scale when available;
record and label differences rather than claim comparable scores. MotionMark
requires a visible fullscreen viewport. BrowserBench deadlines remain failures.

If any import/cache/fence prerequisite fails, retain the CPU-copy fallback and
recover rather than call zero-copy qualified. Force CMO cost and native GPU-cache
visibility must be measured; neither60fps nor zero-copy follows from this build.
Dock Ethernet diagnostic, USB/keyboard and DP-provider qualification remain
separate subsequent work after Firefox and an explicit owner window.


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
IOGeneralMemoryDescriptor::complete+0x288 →sf_avd_os_dma_free+0x110 →
avd_buf_free/sf_avd_session_destroy/context_destroy. This is teardown accounting,
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
reviewed killable-wait SFAgx/SFDcp and speaker cap25 half-dB steps (control176),
and requires exactly7 changed modules/50 unchanged against CIO29. Source/SDK/
link/host evidence is separate from the forthcoming native wait/speaker gate.
The UPL fix is being prepared independently; no assertion or complete call is removed.


Owner update2026-11-16 01:38Z: extra speaker cap defaults OFF. The current model
passes comparison testing with zero violations; the owner explicitly withdrew
the interim volume reduction. The boot argument sfspeaker_cap_steps remains
available, with default0; model/probe safety limits remain unchanged. The first
cap25 image43 reservation was cancelled before ANY M1 action; its kit is retained
privately. The replacement image43 uses sound SDK c5zjb6pd…, collection gif6ishr…,
capsule11f6f53b2089f2a6…, prelink0C63FD93-8845-4DE8-85E5-C317D51928A0.
It still has7 changed modules/50 unchanged against CIO29 and ORIGINAL AVD.
Native default-cap0 / K14 / killable-wait qualification is pending.
