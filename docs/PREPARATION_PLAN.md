# Work before the Linux machine is ready

Historical preparation plan: on November 7, 2026 the user confirmed the Linux
machine had been rebuilt and `/home/chrisk/src` was healthy. Recovery-based
deferrals below no longer block source acquisition or local build preparation
in this workspace. Follow `docs/FIRST_LINUX_SESSION.md` for the next build gates.

## Highest-value work now

**Make the project portable before making it boot.** Keep this work in the provided bundle or a healthy workspace. Leave the recovering filesystem out of the workflow.

### 1. Turn the audit into a reproducibility plan

The earlier audit records candidate revisions and admits that no builds or boot tests were performed. Preserve it as historical evidence. Verify repository identities, commit objects, recursive submodules, source licenses, and the Nix lock once source acquisition becomes appropriate. Do not silently move to branch tips.

Deliverable now: a candidate registry with explicit unverified flags and a promotion checklist. Deliverable later: a tested build lock plus hashes of artifacts and toolchain inputs.

### 2. Write a small, ordered backlog

Use concrete acceptance criteria: a repeatable unmodified VM baseline, independent XNU entry on a selected target, stable RAM-root userspace, correct GPU-buffer lifetimes, verified off-screen execution, then presentation and input. Do not launch all kernel, graphics, compatibility, and desktop ports at once.

Deliverable now: `planning/tasks.json`. None of its operating-system tasks is passed.

### 3. Build the offline parts of the laboratory

Metadata validation, artifact hashing, expected event formats, conservative log parsing, and report structure do not need a live Mac. This bundle implements those small pieces and tests them against synthetic inputs. Timeouts, recovery, actual transport ownership, device discovery and kernel loading remain future integration work.

A hardware runner must distinguish transport loss, guest panic, missing console, and an unresponsive target. A timeout is not proof of a kernel fault. Record the last observation and stop for recovery rather than automatically reflashing.

### 4. Specify interfaces before porting

Define the boot contract and the buffer lifetime/ABI contract. Inventory Linux-specific references in the exact driver revision. Select a few portable protocol tests only after retrieving their complete source and provenance. Do not invent hardware register values from prose.

### 5. Design the experience in parallel

The project should have an interaction model beyond changing the kernel: coherent Chonkstep gestures, legible scaling, predictable app launch, transparent system state, and a safe developer workflow. Specify these now without making desktop work depend on premature GPU claims.

## Work to defer

Large source checkouts and Nix builds on the recovering machine; live boot experiments; internal-storage/DMA device bring-up; power-state changes; kernel signing or boot policy; firmware extraction or redistribution; unverified third-party binaries; unattended recovery loops; and claims that a macOS application works.

## Optional independent work on the M5

Record the existing known-good procedure, tool revisions, cable/port arrangement, and available old logs in a separate healthy workspace. This is inventory, not an instruction to reconfigure or boot the target now. Do not place that information on the recovering Linux disk. Share sanitized logs only when useful for implementing the parser or transport adapter.

## First deliverable when Linux is ready

Reproduce an unmodified, pinned PureDarwin ARM64 VM baseline, preserving the exact command and log. The documentation distinguishes an ARM64 runner from its default x86_64 VM. A generic QEMU virt boot demonstrates userspace and toolchain progress, not Apple AIC, DART, DCP, GPU, or storage support.

References: `docs/SOURCES.md`, prior audit sections F02, F03, F04 and Acceptance gates.
