# Working architecture and decisions

Status: proposed design for a bounded prototype. Not a verified combined operating system.

## Roles

- Linux builder: source workspaces, isolated/pinned builds, cross-toolchains, software tests, VM experiments, artifact production.
- M5 macOS controller: known-good USB/m1n1 environment, explicit target-test execution, raw console/proxy capture and recovery coordination.
- Separate target: the board on which experimental XNU executes. Board/SoC/firmware are not yet selected.

The builder produces an immutable test bundle. A future controller verifies it against an approved target profile, executes one test, stores observations, and returns a result. Neither role automatically installs an operating system.

## Working decisions

**D01: Linux is the build host, not the intended runtime kernel.** The first runtime experiment targets XNU. Keep enough portable tooling that a failed XNU assumption does not invalidate all the work.

**D02: Omacom remains the primary hardware reference.** Record each of the m1n1, Linux Aurora and Mesa integration revisions separately. Aurora Silicon and Gravity remain comparison sources. A newer commit is a candidate, not an automatic update.

**D03: PureDarwin is a candidate platform/runtime.** Its availability and documented features are not an implementation guarantee for our exact revision or target. Reproduce an unmodified baseline before patching it.

**D04: Separate kernel portability from userspace compatibility.** A native hardware substrate and a macOS-framework compatibility effort are different workstreams. Select Darling components only after a concrete dependency requires them.

**D05: Preserve useful interfaces, not accidental Linux internals.** Evaluate an Asahi userspace ABI-compatible backend. Resolve architecture-specific ioctl encoding, mapping, sharing and synchronization semantics explicitly. Do not assume a similarly named method has identical ownership rules.

**D06: Keep Chonkstep's interaction model.** Audit the compositor's platform boundary for input, outputs, presentation, device access, process launch and session lifetime. Software/nested rendering is a separate intermediate target, not proof that physical scanout works.

**D07: No automatic hardware actions in phase zero.** The current tool deliberately cannot boot, reset, flash, execute commands, or connect to a machine. A JSON flag is not a security boundary. A future privileged controller must enforce authorization outside agent-writable source/configuration.

## Choices deliberately left open

The first target model; kernel page-granule/platform profile; Rust driver-adaptation approach; exact ARM64 build outputs; application ABI policy; project/product name; update format; packaging policy; project license; and host controller deployment method.

## Independence vocabulary

Track three kinds of dependency separately: custom OS code; macOS host/control tooling; and Apple platform firmware/security boot requirements. Avoid describing a RAM-root XNU experiment as independent of all Apple firmware merely because it is independent of macOS userspace.
