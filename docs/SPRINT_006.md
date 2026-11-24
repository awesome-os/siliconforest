# Development increment 006 — November 7, 2026

This increment takes the RAM-only probe from host-tested candidate code to the
real pinned PureDarwin SDK and an experimental ARM64 virt kernel collection.
The clean baseline checkout and its twice-booted image remain separate.

## Build and inclusion

`stage_puredarwin.py --arm64-vm` adds the two ARM64 bundles to the build/install
selection and VIRT collections, stages the userspace recipe and classifier, and
adds `dlmemtest=1` only to the experimental minimal images. It verifies the exact
commit and edited upstream file blobs before changing a clean checkout. The tool
does not fetch, build, boot, activate a module, or commit anything.

The first real SDK bundle build failed: the kernel SDK does not supply the C11
atomic convenience API used by the host, and the adapter lacked the generic VM
memory-types include. These failures are preserved in `kext-build-001.json`.
The mapping ticket now uses compiler-provided lock-free atomic bool operations
with the same acquire/release semantics. Host threaded publication tests and the
cross-ABI check cover the change. The adapter includes the pinned generic header
rather than inventing a cache constant. The second bundle build succeeded.

The built PDDRMDri and DLMemoryProbe executables are ARM64 `MH_BUNDLE` files.
Their stored `__realmain` pointers match `pd_dri_start` and `dlmp_start`. The
experimental KC is ARM64 `MH_FILESET` with 52 entries, including both exact
bundle IDs. Reports contain artifact hashes and source snapshot identities.

KC construction is permissive: other bundled drivers have unresolved imports
redirected to panic trampolines. For each probe target the builder reported only
unresolved `dyld_stub_binder`, a baseline pattern. Claude identified an incomplete
KPI dependency declaration for `kauth_cred_issuser`; this KC build reported no
unresolved instance of that import. These observations are not a proof that
every import or execution path is safe. Activation and device behavior need
their own runtime record.

## Host verification

Clang ASan/UBSan and GCC each passed 142 Python tests and 77 C groups. All 27
mutations were detected. The production unoptimized frame-size checks and
updated mapping ABI checks passed. Local validation, planning, acquisition plan,
and publication dry run completed without downloading or publishing anything.

## Runtime status

The experimental minimal image built successfully. Read-only extraction from
its GPT/ESP/ext4 bytes confirmed the exact KC hash, `dlmemtest=1` boot arguments,
and the smoke executable hash. The image's smoke executable matches Claude's
standalone SDK build byte for byte. Inspection used no mount.

One fresh experimental cold boot reached a usable Darwin 25.5.0 ARM64 virt
shell. Explicit `pdkext load` commands for PDDRMDri and DLMemoryProbe each
returned 0. The kernel logged `RAM probe publication 0`, and the device node
`/dev/dri/dlmemtest` was observed. Through that node the hash-verified exerciser
reported `PASSED_KERNEL_UNMAPPED` (11 checks, exit 0), followed in the same boot
by `BLOCKED_MAPPED_RECLAMATION` (34 checks, exit 77). The mapped classifier
requires both live-mapping checkpoints, the child exit, complete ticket drains,
and the exact quarantine receipt. This is not a mapped-reclamation PASS.

`reports/sprint-006/vm-device-runtime.json` records the actual QEMU arguments,
isolated network namespace, artifacts, guest commands, JSON results, exit
statuses and full serial transcript hash. Guest JSON retains
`evidence=guest_self_report` and `host_verified=false`. The guest remained
responsive after both tests. QEMU was stopped through its monitor, exit 0;
guest shutdown was not tested. Both the experimental and baseline image hashes
were unchanged after the snapshot run.

Remaining limits include the three-second teardown polling window, missing JSON
on the default alarm path, an incomplete Unsupported KPI dependency declaration,
and lack of independent pager-finalization proof. The observed run does not
resolve those limitations or broaden the tested environment.

Mapped backing remains permanently quarantined until reboot. The capability
`DLMP_CAP_MAPPED_RECLAIM` is absent. A correctly observed mapped lifecycle may
report `BLOCKED_MAPPED_RECLAMATION` (exit 77), never PASS. An earlier failure must
remain FAILED. No Apple hardware, M5, GPU, firmware, or boot policy is involved.
