# Development increment 002 — November 7, 2026

## Starting point

Continues the local repository at `65953cd`, preserving the previous history.
Work ran only in the assistant's isolated workspace. No user's machine, storage,
USB connection, target boot policy or GitHub remote was changed.

## Implemented

1. `core/backing_store.{h,c}`: a portable coordinator using the existing lifetime
   policy plus explicit allocation/release provider callbacks. It publishes
   handles only after successful allocation, tracks rounded resident bytes,
   retains backing on release failures, rejects callback reentrancy, and moves
   destruction to explicit collection rather than fence/firmware callbacks.
2. `tools/cross_abi.py`: compiler-evaluated target layout probes using LLVM IR
   constants, with no foreign execution. Compiles both C components for ARM64
   Darwin and AArch64 Linux and checks object header identity.
3. `tools/check_mutations.py`: six deliberately broken copies of the coordinator,
   each required to compile and then fail the native tests. An unmodified control
   must pass. Compile failures/timeouts do not count as detections.
4. Unified native/cross checks, new regression tests, provider documentation,
   source evidence and expanded CI configuration. CI has not run remotely.

## Actual execution results

| Check | Result |
|---|---|
| Python tests | 114 passed, zero failures/errors/skips |
| Native C test groups | 29 passed: 16 policy + 13 backing-store |
| Existing policy state exploration | 100,000 deterministic transitions |
| New backing-store stress loop | 10,000 real host allocation/free cycles, including simulated timeouts |
| Clang AddressSanitizer + UndefinedBehaviorSanitizer | No reported failures in native suites |
| GCC with warnings as errors | Native suites and freestanding compile passed after fixing enum signedness |
| Clang static analyzer, new coordinator | Zero diagnostics |
| Deliberate regressions | All 6 compiled and were detected by runtime test failures |
| ARM64 objects | Both C components emitted ARM64 Mach-O and AArch64 ELF relocatable objects |
| Compile-only layout comparison | 10 types, 42 offsets, sizes/alignments and 3 constants agreed across selected profiles |
| Real upstream header host cross-check | IR-extracted values matched a separately compiled/executed native host probe |

The GCC first run rejected enum/int ternaries under `-Werror=sign-compare`.
The code now uses explicit status branches; the original failed report is kept
as `checks-gcc-before-fix.json`, alongside the successful rerun.

## Source capture

Full `omacom/linux-aurora` `cleanup.rs` and PureDarwin `pd_dri_cdev.h` were
retrieved with GitHub and matched to their reported Git blob hashes. The cleanup
inventory is a full-file lexical inventory, not macro/type/transitive analysis.
The source cache is excluded from the distribution. Direct network downloads
from this container failed DNS resolution; the connected GitHub reads worked.

## Limits and work still outstanding

- No complete Asahi kernel/Mesa UAPI pair or real Darwin SDK layout comparison.
- No PureDarwin build, XNU link, kernel extension, boot image or physical boot.
- No GPU/firmware commands, wired memory, IOMMU mappings, cache policy or actual
  fences. Device events are simulated; the host allocator is not a DMA allocator.
- No thread-safe kernel wrapper, worker queue, pager/last-mapping callback bridge,
  unload coordination, shared-buffer capability model or complete GEM semantics.
- The plain ARM64 object files are not arm64e validation and retain unresolved
  runtime/policy symbols. Compilation is not linking or execution.
- No GitHub creation, push, Actions run, background job or hardware action.

## Next implementation boundary

The coordinator is a testable seam for a future XNU backing provider. Before
connecting it, inspect the real mapping/pager implementation and define wired
page ownership, callback token lifetime, legal destruction context, cache modes
and serialization. Expand the UAPI capture against real SDKs rather than invent
replacement headers. The first OS milestone remains an unmodified PureDarwin
ARM64 VM baseline on the restored Linux builder.

## Reproduce

```sh
python3 tools/check.py --sanitize --cross
python3 tools/check.py --cc gcc
python3 tools/check_mutations.py
python3 tools/lab.py validate
python3 tools/lab.py plan
```

The hardware plan remains blocked. A six-mutant check deliberately executes
assertion-failing host test binaries; those failures are the expected evidence.
