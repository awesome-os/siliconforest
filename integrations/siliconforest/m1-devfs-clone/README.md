# DRM metadata lookups can strand devfs creation

The 2026-11-15 23:20Z desktop freeze was captured and recovered at23:37:34Z.
Standing owner authorization now permits immediate Firefox/stability installs:
2-minute notice, core/gui/bench locks, UART, rollback and standard-state recovery.
Owner-idle and hourly waits were lifted. The first cycle uses build-devfs-kc.nix
(CIO29 plus only core/shared DRM), then the Firefox additions in the next cycle.
The recipes here build locally; the controlled installer owns all machine actions.

## Captured evidence and cause

Raw evidence is private at runs/sftb/gpu-wedge-2320/: all-task stackshot and
decoded JSON, symbolicated threads against the exact installed29 and GPU/DCP
link maps, ps/kernel identity, dmesg since23:15, ioreg and UART. The stackshot
contains37 tasks/201 threads. Both compositor28999/main73279 and Firefox GPU
helper28583/Renderer72355 wait in devfs_dntovn+0x94 on the same wait channel.
The compositor is doing open/namei; the helper is doing fstatat/namei. Neither
has reached a DRM callback. AGX/DCP taskqueue workers are in idle semaphore
waits; services are attached/busy0 and the retained ring has no GPU fault/
timeout/recovery around the incident.

The wait is the DN_CREATE condition, **not a mutex held by the dying helper**.
msleep drops devfs_mutex while waiting. No live creator was found in the
captured stacks; generic msleep does not report an owning thread, so the
original creator cannot be named from this evidence.

Source audit of the actual installed shared SFDRMDri finds1023 allocatable
clone reservations. It already releases them on successful close/failed open.
A metadata-only lookup allocates a cloned vnode and never calls open or close.
devfs_inactive recycles it, but devfs_reclaim never calls its clone callback.
Thus metadata lookups leak reservations. In devfs_dntovn, clone allocation
failure returns ENOMEM through out, bypassing DN_CREATE clearing/wakeup.
Every later lookup then waits indefinitely, including a killed helper.

The actual frozen functions reproduce this mechanism on the host: stat-only
lookup1024 returns ENOMEM with1023 slots used and DN_CREATE still set. This
matches the native wait. The historical allocation count and the precise
first failing caller were not captured on the M1, so the threshold is hosted
evidence, not a retroactive hardware measurement.

This does not establish the cause of SF-AC's older dma-buf hang, syncobj
termination issue or G13 mappings dropped under in-flight jobs. No corresponding
older kernel backtrace is available. The taskqueue0041 fix remains unchanged.

## Fix and lifetime

| Change | Contract |
|---|---|
| Clear/wake on clone allocation failure | Every owned creation path exits through cleanup; future lookups return errors rather than inherit an orphan flag |
| Free the actual allocated clone on vnode-create failure | Use vnfs_rdev, including allocated minor1 (which happens to equal the ALLOC action constant) |
| Interruptible creation wait | PCATCH selects THREAD_ABORTSAFE in frozen kern_synch.c; propagate EINTR/ERESTART with the devfs reference released, without clearing another creator's flag |
| Opt-in clone API | devfs_make_node_clone_with_flags(...DEVFS_CLONE_FREE_ON_RECLAIM...) registers vnode-owned reservations; existing API/consumers retain their close semantics |
| Reclaim hook | Release the reservation on final cloned-vnode reclaim, covering stat-only lookups and failed opens |
| Shared DRM ownership | Keep the slot reserved after close/error until that vnode is reclaimed; otherwise old-vnode reclaim can release a newer file's reused slot |
| Live-cookie refusal | Do not release a slot whose DRM cookie is still live |

The callback releases an index reservation only. Existing DRM close and
select cleanup still run in their normal order; no GPU pages, GEM objects,
in-flight mappings or decoder backing are freed to make process exit pass.
The new callback takes the same devfs→slot-lock order as allocation. Legacy
PTY/audit/dtrace cloning devices do not opt in and receive no new reclaim call.
No devnode structure layout, polling timer, GPU firmware or recovery policy
changes. Native kill/IPC teardown qualification remains pending.

The new shared kext requires the new kernel API: install them only in the same
collection. It is not compatible with qualified29's core by itself.

## Source and build boundaries

kernel.patch changes only five frozen core files: devfs tree/vnops/header/flags
and BSDKernel.exports. build-kernel.nix asserts the base derivation is exactly
qualified29's r0519s8… before applying the patch. manifest.json checks before/
after bytes; kernel_build accounts for the exact original lab export patches.

dri.patch changes only the installed SFDRMDri.cpp in immutable source
gj14r67462ndvmf7f2al6ah8isjhk4wc. api.patch supplies the matching devfs header.
The kext recipe uses prebuilt cached XNU headers, so overlay-header.py replaces
only the four generated devfs.h copies with that verified API and records all
before/after hashes. The initial native failure on a stale header is preserved.
Original XNU and PureDarwin licences remain; the tools/host fixture are MIT.

build-firefox-kc.nix keeps the original reviewed three SDKs fixed:
AVD5238qcr…, Touch Bar/K14 42j1wm…, publication/HID readinesszc02mx….
It adds only the new core and shared SFDRMDri to the qualified29 recipe.
AGX, DCP, NVMe, dock and every other module remain the baseline inputs.
Final verification requires57 executable modules, exactly five changed UUIDs
and52 unchanged. All56 input kexts retain their non-debug bytes through stripping;
original overlap validation still runs, and plist compaction preserves values.
Actual ARM64 branches/stubs/chained pointers must resolve both the new clone
API and AVD's existing Force-CMO routine.

## Host evidence and native qualification

Run python3 integrations/siliconforest/m1-devfs-clone/test-source.py.
It extracts actual devfs_dntovn/inactive/reclaim and shared DRM clone/open/close
bodies from frozen and patched sources, then compiles them with ASan/UBSan.
Only C's zero-to-enum initialization gets a type-preserving cast for C++ hosting.

The original1024th-stat counterexample must fail. Fixed checks cover5000 stat,
5000 open/close and5000 failed-open cycles, full-pool refusal/reuse, creation
failure/wakeup/actual-minor retirement, minor1, interrupted wait, delayed reclaim
after close, legacy consumers and refusal to recycle a live cookie. This tests
source lifecycle logic, not real VFS scheduling, GPU DMA or IPC destruction.

Native build recipes:
- build-kernel.nix: frozen installed core + patch.
- build-dri.nix: shared SFDRMDri only.
- build-firefox-kc.nix: one future image, with required frozenPhase argument.
- build-probe.nix: native clone-probe client, built but never run on M1.

The probe requires the reviewed new kernel UUID and a render node, refuses a
different actual kernel before touching that node, and performs5000 stat plus
512 open/fstat/close cycles. Use only the exact private window-plan argv after
the new fixed image boots. Do not use the old kernel UUID or run it on29.

Native SDK/collection builds are local evidence only. The original artifacts,
later boundary-case rebuilds, raw failures and final capsule manifest remain
private under the incident directory. Final artifact paths/hashes are recorded
below after the native checks finish.

## Recovery record

The single authorized cycle used one external reset, a Linux visit with
read-only e2fsck -fn, the normal installer for exact29/a149e487…, then one-time
SiliconForest boot. Standard .next/process/socket/inject and a non-input stats
roundtrip succeeded. UART reader and route process stopped; all CODEX-TB locks
released before returning to the pause.

e2fsck -fn returned4: journal replay skipped, block/inode bitmap differences,
free-block counts off by3 and many extent-narrowing suggestions. It did not
repair anything; this is not a post-replay filesystem qualification.

Correction: the initial clean request used /sbin/reboot rather than the
installed fenced /bin/reboot wrapper. Its background stderr was redirected;
the capture does not prove supported clean shutdown itself hung. That wrong
path was an agent error. Future recovery must use /bin/reboot and retain its
status/stderr. No extra boot was used to retest after recovery.

## Next owner-opened window: about55min including Linux benchmarks

1. Fresh coordination/log and core→GUI→bench guards; preserve current desktop
   library inputs and prepare qualified29 recovery. No machine access beforehand.
2. During the normal Linux installer visit only, run the prepared stock Firefox
   Speedometer/MotionMark/JetStream reference if installed and time permits
   (up to30min). No browser install or extra Linux reboot.
3. Install ONE combined devfs/DRM + AVD/K14/HID capsule via the normal installer;
   route/start UART after installation and before the one-time SF boot.
4. Verify actual kernel identity and standard responsive .next/socket/inject.
   FIRST run the fixed-image clone metadata/open probe (about1min).
5. FX's owned GPU-helper startup-failure/kill → IPC closure → visible software
   fallback and subsequent launch (about2min); no owner process signals.
   Source PCATCH tests do not substitute for this native exit/IPC result.
6. Media import/cache/held-frame/producer-exit/hash/PTS/zero-copy gates and owner
   physical pinch/ordinary input regressions (about10min), preserving fallback.
7. Retain every failure, restore qualified29 if required, verify standard state,
   reap UART and release all locks (about5min). No speculative second candidate.

Without the optional Linux benchmark allowance, reserve roughly25min. Dock
diagnostics and older GPU/syncobj issues remain subsequent work. No new owner
window, upload or reboot is scheduled by these local builds.


The first install also exposes read-only kern.sf_dri.clone_slots with independent
used/scan counts, allocation/free/failure totals and peak. dri-stats.patch is a
separate checked layer so the already-frozen kernel input does not change.
The native probe now forks4 children; each does5000 stat,5000 open/fstat/close
and5000 deliberately failed O_DIRECTORY opens of the render node. It requires
stable slot counts, counter/scan agreement and balanced allocations/frees.
No frame, DMA or firmware work is submitted by this stress. GL/vulkaninfo50 and
20 isolated visible Firefox launches follow in the installation qualification.


## Native devfs-only installation, 2026-11-16

The first install uses the devfs-only collection on the exact CIO29 baseline,
with only XNU and shared SFDRMDri changed (55 unchanged module UUIDs).
Observed: DESK8-next003 and non-input inject stats respond; four children each
complete5000 stat,5000 open/close and5000 failed-open cycles. All60000 clone
allocations are freed, used3 before/after, failures0, peak7. GL exact pixel-hash
and vulkaninfo each pass50 launches. Firefox20 fresh-profile mapped-window
qualification is still running. Its first capture has a blank content area;
page-paint correctness is a separate unresolved Firefox/compositor issue.

For native identity gates, use the collection prelink plist's _PrelinkKCID,
not the top-level or embedded kernel's LC_UUID. XNU bootstrap.cpp copies that
prelink ID into kern.kernelcacheuuid. The installed candidate's prelink ID is
65F85F90-FF31-4541-A51E-8247DD0C4881; Mach-O LC_UUID is1FDC2A71… . The first
gate used the wrong field and refused before stress; private receipts retain
that failure and a corrected byte-equality/prelink proof.


Devfs-only qualification completed2026-11-16 01:03:43Z: the image remains
installed as the normal SiliconForest kernel with CIO29 rollback. Twenty
consecutive fresh profiles04–23 map/commit/capture a window and cleanly Quit/reap
their whole family; slots3 after every launch, zero allocation failures. Median
map5.4175s, range4.055–6.844s. LOOKED at the20-capture montage: ALL content areas
are blank/stale NewTab chrome. Thus window creation/close passes while Firefox
page-paint is STRICTFAIL, handed to FX/DESK; no presentation-correctness claim.
DESK8-next003/inject responds, owned UART reader/route are gone, locks released.
