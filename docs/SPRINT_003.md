# Development increment 003 — November 7, 2026

Continues `ff3bef1`. Work ran in the assistant's isolated Linux workspace, not on
the user's recovering filesystem or the M5. No remote or hardware was changed.

## Delivered

- Source-traced pager-release integration notes, distinguishing unpublished
  rollback from ownership transferred to the PureDarwin device pager.
- `core/mapping_ticket.{h,c}`: stable single-use mapping holds, an asynchronous
  atomic release notification, serialized draining, restricted mapping geometry
  and permissions, and checked physical-page-number conversion.
- Fourteen new native test groups, including eight notification threads racing
  one serialized drain. Backing allocations are real host allocations; pager,
  GPU and reset events remain simulated.
- Six new mapping mutations in the reproducible mutation tool and a compile-only
  mapping-layout specification. Existing history and tests remain intact.

## Actual execution evidence

| Check | Result |
|---|---|
| Python regression tests | 114 passed, no failures/errors/skips |
| Native C groups | 43 passed: 16 lifetime + 13 backing + 14 mapping |
| Concurrent handoff test | 8 threads, 400,000 notifications, one successful drain |
| Clang ASan/UBSan native suites | Passed without reported diagnostics |
| GCC native suites | Passed |
| GCC ThreadSanitizer mapping suite | Passed, no reported diagnostics |
| Deliberate regressions | 6 backing + 6 mapping mutations compiled and failed at runtime |
| New component static analysis | No diagnostics |
| ARM64 cross-compilation | All three C components emitted Darwin ARM64 Mach-O and Linux AArch64 ELF objects |
| Mapping layout comparison | 3 types, 12 offsets, sizes/alignments and 4 constants agreed for the selected compiler profiles |

Reports and exact commands live in `reports/sprint-003`. The existing 100,000
policy-transition and 10,000 host allocation/free stress loops were also rerun.
Compile-only objects were not linked into a kernel or executed on ARM64.

## Source confidence

New observations inspect selected pinned ranges of `PDDRMDri.cpp`,
`kern_mman.c`, `device_vm.c` and `IOBufferMemoryDescriptor.h`. They are not new
full-source byte-verification results. Network acquisition from this container
still failed DNS resolution; the GitHub connector supplied those source reads.
No upstream implementation is copied into the new C code.

## Important limits

There is still no real page provider, XNU pager, kernel worker, DMA backend,
security enforcement, boot image or kernel integration. The ticket callback can
run concurrently, but the rest of the store API still requires external
serialization. Tickets cannot be freed or reused merely because drain succeeded;
the real callback producer must be detached/joined. No kernel-unload barrier is
implemented. Source review does not establish that the pager release point is a
complete VM/TLB teardown barrier.

## Next meaningful OS milestone

On the restored Linux builder: reproduce the unmodified, pinned PureDarwin ARM64
VM. Then link a RAM-only XNU test component with a real wired backing provider and
exercise map/close/unmap/failure/exit lifetimes in that VM. Keep Apple GPU DMA,
internal storage and physical target boot out of this first integration test.
The M5 remains the later USB/debug controller, never an assumed target.

## Reproduce

```sh
python3 tools/check.py --sanitize --cross
python3 tools/check.py --cc gcc
python3 tools/cross_abi.py --spec specs/mapping-layout-probe.json \
  --build-dir .build/mapping-abi --report .build/mapping-abi.json
python3 tools/check_mutations.py
python3 tools/check_mutations.py --suite mapping-ticket \
  --build-dir .build/mapping-mutations --report .build/mapping-mutations.json
```

For the separate ThreadSanitizer run, the complete GCC compile and execution
commands are recorded in `reports/sprint-003/tsan.json`.
