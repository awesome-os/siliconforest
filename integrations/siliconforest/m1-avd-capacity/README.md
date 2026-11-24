# Offline AVD capacity candidate — NOT QUALIFIED OR INSTALLED

Paused at the owner's wind-down on 2026-11-16. Kernel 48 remains normal,
with qualified 47 kept as rollback. This directory is preserved work for a
later session, not a completed 4K fix. No kernel 49 capsule was packed,
no install notice was issued and no M1 boot/probe used this driver.

The media agent's actual kernel-48 browser pair could not create a 3840x2160
AVD context. The driver logged no contiguous address space for 1597 16 KiB
pages while 1985472 KiB was mapped in its 2 GiB window. Nominal free space
was 111680 KiB, greater than that 25552 KiB request, which also establishes
fragmentation. The agent observed about 5.22 GiB physical free memory. These
are its captured hardware results, not a new test by this directory.

The candidate applies only after the exact qualified context-stats/import
source. It expands the window from 2 to 3 GiB, ending at 0xC4000000 below
2^32, retains first-fit allocation and all map/unmap/TLB poison/quarantine
paths, and adds compile-time 32-bit/L1-table bounds. A read-only
`kern.sf_avd.contexts` reports owner PID and whether the original task still
matches, codec/dimensions/buffers, import/submission state, capture/prepared
bytes, serialized allocation deltas, total free IOVA and largest free run.
It never reaps a live task or changes transactional IMPORT: all replacement
buffers still map before the old set is swapped and released. Diagnostics
and accounting have not been tested on hardware or in host fault controls.
The tracked mapped-byte delta needs review across failed imports, decode
reallocation and quarantined teardown before it is trusted as a public API.

A full arm64 SDK and collection build completed before wind-down. Its
`nix-support/collection-proof.json` verifies 57 executable modules, only
SFAppleAVD changing from kernel 48, with core and 56 other UUIDs identical.
The final plist compaction count of 69 includes metadata entries; it is not
an added set of 12 executable drivers. This is build/provenance evidence only.
There are **no new host tests, QEMU rehearsal, native 4K data/retirement gates,
or crash/lifetime results for this candidate**. The attempted preparation
first failed a source-pattern assertion before producing a patch; its corrected
version matches the exact baseline and the SDK applied every hunk without fuzz.

The build commands, on the healthy workstation, are:

```sh
nix-build --extra-experimental-features 'nix-command flakes' \
  integrations/siliconforest/m1-avd-capacity/build-kext.nix --no-out-link
nix-build --extra-experimental-features 'nix-command flakes' \
  integrations/siliconforest/m1-avd-capacity/build-kc.nix --no-out-link
```

The built collection is
`/nix/store/n265l9gjny1vdakjfssdh1jydjmdprxx-puredarwin-kc-firefox49-avd-capacity`.
Private baseline/candidate source copies, preparation failure and build log
are in `runs/sftb/avd-capacity/`. `apply.py` asserts all before/after SHA256s
and applies `capacity.patch` to a copy; it is not a runtime installer.

To resume, first audit every firmware IOVA cast/shift, L1/L2 index and
allocator boundary against the pinned Omacom source. Add actual-function
host tests for high addresses, fragmented free runs, wrapping hint searches,
allocation/rollback/invalidate errors and diagnostic accounting. Budget
coexisting contexts plus CREATE's full capture/work pool and IMPORT's
simultaneously mapped old/new pools. A 3 GiB span alone cannot guarantee a
large contiguous hole. Reuse the existing per-context isolation/ABI controls,
then link the candidate into a disposable VM and rehearse the unchanged
storage/GL/Vulkan/Firefox cohort. QEMU cannot exercise real AVD/DART hardware.

Only after those gates pass in a later authorized session should the owner
of kernel installs copy/adapt `runs/sftb/firefox-install-0048/` into a new,
unique install run. Keep exact kernel-48 files as rollback, pack the collection
with the unchanged qualified 599-byte arguments, announce two minutes ahead,
take GUI/core/bench, start UART, cleanly reboot, save the read-only filesystem
check, and invoke the normal SFBOOT installer with the new complete pair.
Never install the raw collection as kernel.bin. One-time boot only, then
qualify concurrent H264/4K decode plus transactional import peak, exact pixels,
owner isolation, natural retirement, fragmentation diagnostics and the normal
GL/Vulkan/Firefox/desktop gates before promotion. There is no install or enable
command to run now; the owner explicitly stopped this cycle.
