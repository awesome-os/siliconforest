# Evidence and reference register

Prepared November 7, 2026.

## Prior evidence, preserved without promotion

`archive/darwin-apple-silicon-audit-2026-11-07.md` and `archive/source-manifest-2026-11-07.json` came from the preceding conversation. They describe selected static inspection and explicitly report no builds or hardware boots. This preparation pass preserved their bytes and imported their source records; it did not re-fetch those repository objects or certify their findings. All registry verification flags remain false until independent evidence is recorded.

## Official documentation checked in this preparation pass

- PureDarwin Getting Started: https://puredarwin.org/handbook/getting-started.html
  Describes Linux/Nix requirements, partial macOS host support, and the distinction between building individual components and images.
- PureDarwin Running in QEMU: https://puredarwin.org/handbook/images/running.html
  Distinguishes the x86_64 default VM from the ARM64 virt runner and discusses image overlays.
- Asahi m1n1 Hypervisor: https://asahilinux.org/docs/sw/m1n1-hypervisor/
  Documents host/target ABI synchronization and macOS guest experimentation. It does not demonstrate that our independent XNU combination works.
- Asahi m1n1 User Guide: https://asahilinux.org/docs/sw/m1n1-user-guide/
  Describes proxy and virtual-UART interfaces. Exact interface selection remains a property of the selected fork/build and physical setup.

Online documentation is mutable and is not part of a cryptographically pinned source lock. Reconcile it with the checked-out revision before executing commands. No unrelated third-party articles were used as implementation evidence.

## Evidence classes

Proposed design; inherited static-audit finding; source object independently verified; build executed; synthetic test passed; VM boot observed; hardware behavior observed; compatibility test passed. Never substitute one class for another.

## Provenance policy

Track path, upstream URL/revision, authorship, declared license, modifications, destination linkage boundary, and review notes for each reused file. A source file's dual license does not license every dependency the same way. This is a provenance workflow, not a legal clearance for a combined distribution.
