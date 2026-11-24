# Development increment 005 — November 7, 2026

The user confirmed the Linux machine was rebuilt and the workspace is healthy.
The old recovery deferral is historical. This increment starts from lab commit
`bf62c0673ce64c48503739fadf0140db39796e8d`.

## Memory probe hardening

Independent reviews found that the mapped exerciser could report PASS after
unauthorized reclamation, or report BLOCKED after an early pager notification
had been hidden by quarantine. The engine now records exported allocation
generations and enforces quarantine independently of provider notifications.
The provider retains its separate gate and never advertises mapped reclamation.

A pure host-tested classifier validates baseline copyout, historical counter
deltas, two pending mappings at both live checkpoints, complete post-exit drain
accounting, and exact quarantine/error/byte counts. Reclamation without the
capability and unrelated provider errors fail. The POSIX control still uses real
host mmap/fork/exit and reports no XNU claim. Byte loops now count one check each;
the actual control passes 17 checks. Lifecycle mismatches and collection timeouts
print the last observations to stderr. Device JSON explicitly identifies itself
as guest self-report with `host_verified=false`. An outer runner must treat
missing output as failure.

The unshipped protocol appends quarantine counters: STATS is 88 bytes and COLLECT
48 bytes. Old binaries must be rebuilt together. Large initialization temporaries
were replaced with initialization in place. Native and cross-object compilation
now reject component stack frames above 1024 bytes, including unoptimized native
builds. This does not establish a kernel stack budget or compile the C++ adapter.

The pinned source trace excludes the suspected ordinary file-close race through
XNU's fileproc reference/drain rules. Pager finalization and exceptional shutdown
remain separate. It also confirmed the provider's unsupported smart-pointer
assumption. The candidate now transfers one explicit raw factory reference and
releases it on failure. It has still not been compiled against the actual SDK.

## Completed host checks

- 133 Python tests, no failures, errors, or skips, under each compiler run.
- 76 native C groups under Clang ASan/UBSan and GCC warnings-as-errors.
- Real POSIX control passed in both full runs.
- 27 deliberately broken variants compiled and failed at runtime: 6 backing,
  6 mapping, 9 engine, and 6 result-classification mutations.
- Updated protocol layouts agreed across compile-only Darwin ARM64 and Linux
  AArch64 checks. Target code was not executed.

Reports are in `reports/sprint-005`. No GitHub CI run or publication occurred.

## Source and VM preparation

The three primary Omacom candidate commits and their root trees resolve remotely;
the registry remains historical and unpromoted. Twelve selected PureDarwin files
were independently byte-checked. An exact clean PureDarwin checkout plus its
recorded corecrypto submodule was acquired and checked with Git.

Branch comparison confirms `upstream-xnu-12377` at `e7aa00a` contains `main` and is
333 commits ahead. Its XNU base is 12377.121.6; the older branches use 7195.121.3.
The unique `20-5-oss` squash mostly adds 32-bit board work and does not replace
this ARM64 baseline. No branch tip or cherry-pick was silently adopted.

Nix 2.34.9 was installed from its official binary archive after matching the
published SHA-256. The native package attempt failed on mirror 404s without
changing installed packages. No channels or shell-profile edits were added.
The exact source flake evaluated without a lock-file update, confirming the
ARM64 virt runner, minimal image, kernel, and open-source SDK outputs.

The unmodified minimal-image build completed successfully. One attempt was deliberately
interrupted to relocate large compiler logs from tmpfs to disk; completed Nix
derivations remained cached. The successful subsequent attempt used the same source and
build arguments. Its image hashes and result are recorded separately in
`reports/sprint-005/vm-baseline-build.json`, with logs under ignored `runs/`.
The built image subsequently reached an interactive Darwin 25.5.0 shell over
serial under pinned QEMU 10.2.4. `uname -a` identified XNU 12377.121.6,
`DEBUG_ARM64_VIRT`, and `arm64`. `id`, `pwd`, and a child shell returning status
7 were observed. The VM used a disposable snapshot, fresh firmware-variable
copy, and isolated networking; it was stopped through the QEMU monitor after
checks. This is upstream virt boot evidence, not Apple hardware, adapter, or GPU
support. A second cold boot with fresh firmware variables reproduced the
interactive shell and kernel identity. The base image hash was unchanged after
both snapshot runs. See `vm-baseline-boot.json` and
`vm-baseline-boot-repeat.json` for exact commands, identities and transcript hashes.

Next: stage the candidate in a separate
checkout for the real SDK/KC/startup and RAM-only device tests. No physical
target, M5, GPU, firmware extraction, or boot policy was accessed or changed.
