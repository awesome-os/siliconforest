# Development increment 004 — November 7, 2026

## Checkpoint and scope

Started from the restored Sprint 003 archive at `59795e8`. Its complete Git
history was present. Work ran in the assistant's isolated Linux workspace. No
user filesystem, M5, target Mac, GitHub repository or remote CI was modified.
This increment prepares the first real-kernel memory experiment, but **does not
claim a kernel build or a working XNU driver**.

## Delivered code

1. A serialized retained-backing visitor that rejects unheld, stale and reentrant
   access. It lends a cookie synchronously, not a new ownership reference.
2. A bounded RAM-probe C engine with typed, fixed-width protocol messages,
   independent file sessions, one allocation per session, mapping preparation,
   single-use release tickets, and explicit drain/collection. It does not expose
   pointers or physical addresses in user messages. Invalid size/version/flags,
   out-of-range mappings, exhausted budgets and provider failures are observable.
3. A userspace exerciser using actual mmap, file-close, fork, cross-process shared
   writes, parent unmap and child exit. Its host control uses an unlinked temporary
   file. Device mode requires explicit opt-in, protocol identity and a clean VM
   probe baseline; no device is guessed or opened by default.
4. Source-only C++ IOKit allocation provider and PDDRMDri bridge, plus CMake/plist
   integration candidate. Requests wired kernel/user-shareable backing, zeros it,
   and supplies host physical page numbers to the existing pager bridge. Root-only
   opens, explicit boot flag, bounded allocations, no GPU/DMA, and no unload.
5. A source staging tool which verifies exact clean PureDarwin HEAD plus the DRM
   header blob, produces/checks a patch, and applies it only with explicit opt-in.
   Tested by actual git-apply in disposable fixture repositories. No real
   PureDarwin checkout was staged or built in this environment.
6. Extended native tests, mutation checks, compile-only ARM64 protocol layout
   probes, source-review records and a Linux-session handoff guide.

## Safety gate kept visible

The previously inspected pager callback occurs before memory_object_destroy.
This is NOT a demonstrated upstream bug, but its sufficiency as a final
backing-reuse barrier remains unverified. The IOKit candidate therefore retains
all exported allocations until reboot. There is no runtime override.

Unmapped allocations can eventually exercise true allocate/close/collect in XNU.
A mapped device run must stop with BLOCKED_MAPPED_RECLAMATION (exit 77) if the
mapping checks succeed and backing is retained. No kernel device run happened
here. Host tests can simulate a completed teardown and test reclamation; that
simulation does not enable the kernel-provider capability.

## Executed checks

| Check | Observed result |
|---|---|
| Python tests | 133 passed, no failures/errors/skips (19 added) |
| Native C test groups | 59 passed: 16 lifetime + 13 backing + 14 mapping + 16 probe |
| Host POSIX exerciser | Passed real mmap/close/fork/exit control; xnu_claim=false |
| Clang ASan/UBSan | No reported errors in the native suites or POSIX control |
| GCC warnings-as-errors | Native suites and POSIX control passed |
| Clang static analyzer | No diagnostics for the C probe, backing coordinator and exerciser |
| Mutation checks | 18/18 deliberately broken variants compiled and failed at runtime |
| ARM64 objects | Four C components emitted ARM64 Mach-O and AArch64 ELF relocatable objects |
| New protocol measurements | 4 types, 27 offsets, sizes/alignments and 6 constants agreed across targets |
| Staging tests | Dry-run, exact-patch fixture application and refusal paths passed |

The existing policy stress loop (100,000 transitions), backing-store allocation
loop (10,000 cycles), and eight-thread mapping notification test were rerun as
part of the full native suite. No new ThreadSanitizer result is claimed here.
Exact compiler invocations, source hashes, stdout and limitations are preserved
in `reports/sprint-004`. Equal protocol layouts do not prove OS ioctl encodings,
SDK compatibility or runtime behavior. The C++ IOKit sources were NOT compiled,
linked, analyzed with an actual SDK, loaded or run. No fake SDK validation is
substituted for that missing evidence.

## First Linux build gates

1. Verify this checkpoint, source pins and healthy workspace. Reproduce the
   unmodified pinned PureDarwin ARM64 VM before staging new code.
2. Stage the candidate into a separate exact checkout and review the diff. Track
   the new files explicitly for the Nix Git source snapshot. The stage tool does
   not add, commit, fetch, install or build anything.
3. Compile against the actual pinned SDK, resolve genuine diagnostics/imports,
   and verify kernel-collection inclusion and start-routine activation. A bundle
   installed into a filesystem is not proof it was loaded into the kernel.
4. Build the userspace binary with that same target SDK. Run the RAM-only
   unmapped test in the disposable VM. Record actual logs and artifact hashes.
5. Observe the mapped lifetime test without bypassing quarantine. Review/prove
   the final pager/VM/backing teardown before enabling mapped reclamation.

The M5 remains the later USB/debug controller. Physical Apple Silicon boot,
GPU support, Darwin Chonkstep, app compatibility and GitHub publication remain
outside this increment. See `docs/XNU_RAM_PROBE.md` for the complete boundaries.
