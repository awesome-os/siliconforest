# Apple Silicon Darwin OS: source-level feasibility audit

**Snapshot: November 7, 2026. Prepared for Chris Kearney.**

## Decision

Proceed with a bounded native-XNU platform prototype using Omacom's m1n1 as the development baseline, PureDarwin as the candidate Darwin runtime and DRM infrastructure, and Omacom's Linux Aurora/Mesa integration branches as the primary hardware and graphics references. Do not treat the repositories as an already compatible operating system.

The strongest newly verified architectural precedent is PureDarwin's ARM64 `PDDRMmsm` driver: a native/clean-room GPU implementation exposing a Linux graphics userspace interface through an XNU-facing platform-device bridge. An analogous Asahi implementation is a defensible design. It does not exist as a demonstrated working combination in this audit.

The biggest work items are a target-specific XNU platform, safe GPU memory/firmware lifecycle integration, a compatible render/display interface, and Chonkstep's Linux-specific backend. Darling is a selective userspace source donor, not the glue that makes Linux drivers work on XNU.

## Scope and evidence rules

This is a static audit of selected source and build interfaces, with branch references and source blobs recorded. It is not a claim that every line of twelve repositories was reviewed. No repositories were cloned locally, no kernels or packages were compiled, and no target was booted. Project documentation describing successful runtime behavior is kept distinct from code inspection. No complete binary ABI, security, or legal compatibility certification is implied.

The M5 Mac is the host/debug machine. This audit does not assign it the target role. The initial physical target must be locked by board ID, SoC ID, firmware profile, RAM configuration, and recovery path before testing.

## Source snapshots

| Repository | Branch | Commit | Role |
|---|---|---|---|
| `omacom/m1n1` | `integration/aurora` | `121fcbf35d4501814b58ca5347926a4173b6a512` | Primary boot/debug baseline |
| `omacom/linux-aurora` | `integration/aurora` | `6a08a08e57558083f552733a5276d71b1a4d8ff6` | Primary Apple hardware driver reference |
| `omacom/mesa` | `integration/aurora` | `a19691114759e2c0507f3502cf6166ef2c6e2e33` | Primary graphics userspace reference |
| `PureDarwin/PureDarwin` | `upstream-xnu-12377` | `e7aa00a60f0b56ef35aa24252e81467787bf71ea` | Darwin kernel, runtime, and DRM integration candidate |
| `darlinghq/darling` | `master` | `5b7e4870d743d40ea65410d719c6ab59e9497111` | Selective framework source candidate; not native-XNU runtime |
| `chriskearney/chonkstep` | `main` | `e0fe3a000aec71b1db46b940e2a75670be4e0f31` | Desktop source; Linux-only Wayland backend needs port |
| `aurora-silicon/aura` | `main` | `78bac92d3e2725609605d2272956d5ca637b9461` | Additional bootloader research, mixed-license provenance |
| `aurora-silicon/linux` | `aurora-wip` | `05eaa1f71fa0352522bdf8c79d99c6e602ab7777` | Additional experimental hardware backend reference |
| `aurora-silicon/mesa` | `aurora-wip` | `23f97d30e0ea3b37e8105346ae35932dee41d017` | Additional graphics/UAPI comparison |
| `GravityLinux/bootloader` | `main` | `c22cd92fe5383a029a379466632899bea6c73e69` | Additional locked-register/SPTM hypervisor research |
| `GravityLinux/linux` | `gravity-m4` | `4fc326f6c25ccceb7515e2635c22e01fde5d3bac` | Separate M4 GPU backend reference |
| `GravityLinux/mesa` | `main` | `87ac51d411f0e58a9433567ec2666f457be89e1d` | Additional graphics/UAPI comparison |

These are observed development snapshots, not an endorsed compatible release. The accompanying JSON is an audit manifest, not a complete build lockfile.

## F01. Use Omacom Mesa's integration branch, not its default branch

The default Mesa `main` revision was also inspected at `7cda7850edd103ace21aac37d416d2fdf7a282e1`. Its Asahi ioctl enum ends at `SUBMIT`. The integration branch adds `GEM_MADVISE`, and its `agx_device.c` contains the corresponding call. Omacom Linux Aurora also registers this operation.

That is a concrete interface difference, not just a branch naming preference. Preserve the integration branch as the initial graphics reference. The kernel and Mesa headers nevertheless have different blob hashes: matching operation names is not proof that every layout, flag, size, or firmware payload agrees.

**Required contract tests:** compare ioctl command encodings, `sizeof`, `alignof`, `offsetof`, feature bits, extension lengths, zero-padding behavior, and error semantics. Generate each side's definitions independently. Do not hard-code Linux ioctl numbers into Darwin without verifying the userspace and kernel encodings agree.

[omacom/mesa: `src/asahi/lib/agx_device.c`](https://github.com/omacom/mesa/blob/a19691114759e2c0507f3502cf6166ef2c6e2e33/src/asahi/lib/agx_device.c); [omacom/mesa: `include/drm-uapi/asahi_drm.h`](https://github.com/omacom/mesa/blob/a19691114759e2c0507f3502cf6166ef2c6e2e33/include/drm-uapi/asahi_drm.h); [omacom/linux-aurora: `include/uapi/drm/asahi_drm.h`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/include/uapi/drm/asahi_drm.h)

## F02. The existing m1n1 runner is a useful automation foundation

Omacom's runner accepts Mach-O payloads, symbol files, scripted hooks, command hooks, log capture, CPU selection, device-tree node removal, raw payloads, and SPRR-emulation selection. It invokes the hypervisor loader, can retain state after a proxy error, and has an interactive recovery path. These are useful primitives for a deterministic host-side test runner.

This does not establish that PureDarwin's kernel collection can be loaded unchanged, nor that an independently built XNU image has the platform services supplied by a normal macOS kernel collection. Asahi's hypervisor documentation describes stock or KDK kernel execution with macOS components and warns that the proxy and target m1n1 ABI must match.

**Boot contract still to validate:** kernel entry and format, boot-arguments version, Apple device-tree contents, physical/virtual memory maps, reserved firmware memory, kernel-collection dependencies, page granule, console transport, CPU startup, timers, and interrupt routing. A working macOS guest is the control case, not the independent-OS success criterion.

Keep the known-good Omacom proxy and target build together. Do not substitute an arbitrary newest host script against an older resident m1n1.

[omacom/m1n1: `proxyclient/tools/run_guest.py`](https://github.com/omacom/m1n1/blob/121fcbf35d4501814b58ca5347926a4173b6a512/proxyclient/tools/run_guest.py); [Asahi hypervisor documentation](https://asahilinux.org/docs/sw/m1n1-hypervisor/)

## F03. PureDarwin has substantial real DRM integration

The inspected build includes DRM GEM, GPUVM, PRIME, sync objects, scheduling, fences, reservations, and project implementations of dma-buf and sync-file glue. `PDDRMDri` exposes device operations for open/close/read/ioctl/poll/mapping, and supports platform-device metadata rather than only PCI enumeration. The Linux compatibility configuration identifies itself as `LINUXKPI_VERSION=61400`.

ARM64 is not merely hypothetical in this layer: the extensions build includes `PDDRMDri` and `PDDRMmsm` on ARM64. The latter builds an Adreno implementation around the MSM UAPI and uses `PDDRMPlatformDevice.cpp`; attachment is opt-in through `pdmsm=1`.

This is the best structural starting point found for a proposed `PDDRMAsahi` component. That proposed name denotes new work, not an existing driver.

**Important limits:** build inclusion is not runtime validation. The inspected DRM device header labels its WIMG cache constants as x86 values. Its mapping contract and underlying XNU implementation need an ARM64 page-size/cache-coherence audit. `pd_sync_file.c` implements fence descriptors and ioctl operations; `pd_lfile_lkpi.c` describes readiness polling. Blocking wakeups, event-loop integration, cancellation, and timeout behavior require tests, not assumptions. The polling observation is a test requirement, not a confirmed bug report.

[PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/drm/drm.cmake`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/drm/drm.cmake)

[PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/pd_dri_cdev.h`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/pd_dri_cdev.h)

[PureDarwin/PureDarwin: `src/Kernel/Extensions/PDDRMmsm/CMakeLists.txt`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/PDDRMmsm/CMakeLists.txt)

[PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/drm/src/pd_sync_file.c`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/drm/src/pd_sync_file.c)

[PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/drm/src/pd_lfile_lkpi.c`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/drm/src/pd_lfile_lkpi.c)

## F04. The GPU port is substantially more than ioctl translation

Omacom's `driver.rs` depends on Rust-for-Linux platform devices, device references, DRM traits, synchronization primitives, GEM shmem objects, and generated C bindings. Its Kconfig requires 16 KiB pages and selects DRM scheduling, GPUVM, RTKit, and other Apple support.

The strongest concrete coupling is in `gem.rs`: the code operates on a Linux shmem file's `f_mapping`, changes Linux address-space mapping flags to control migration/eviction, and maintains device-held backing and process memory charges. An XNU port must preserve the lifetime invariant, not transplant Linux structure field offsets. Firmware and the GPU must never access backing that the host VM has recycled.

PureDarwin's inspected compatibility build is C/C++ infrastructure. It does not demonstrate an implementation of the Rust `kernel` crate required by the Asahi driver. Two viable implementation directions remain:

1. Preserve the MIT-selectable hardware/protocol logic while replacing its OS bindings with a small, explicit XNU substrate.
2. Implement the required subset of Rust-for-Linux interfaces on top of PureDarwin's DRM bridge, only where the actual dependency list makes that smaller than a direct port.

Recommendation: keep the existing UAPI and usable protocol logic, but do not commit to emulating all of Linux or rewriting the GPU driver wholesale. Prototype memory ownership and one real submission before choosing a broad compatibility architecture.

[omacom/linux-aurora: `drivers/gpu/drm/asahi/driver.rs`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/driver.rs); [omacom/linux-aurora: `drivers/gpu/drm/asahi/gem.rs`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/gem.rs); [omacom/linux-aurora: `drivers/gpu/drm/asahi/Kconfig`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/Kconfig)

## F05. The graphics interface has identifiable scope

The inspected Omacom integration headers and driver expose twelve Asahi-specific operations:

| Group | Operations |
|---|---|
| Device query | GET_PARAMS, GET_TIME |
| GPU virtual memory | VM_CREATE, VM_DESTROY, VM_BIND |
| Buffer management | GEM_CREATE, GEM_MMAP_OFFSET, GEM_BIND_OBJECT, GEM_MADVISE |
| Execution | QUEUE_CREATE, QUEUE_DESTROY, SUBMIT |

A usable implementation also needs generic GEM close, memory mapping, synchronization objects and timelines, buffer sharing, descriptor ownership, and device discovery. Userland libraries must translate or implement their platform interactions correctly. A passed GET_PARAMS test is not evidence of rendering.

Recommended acceptance sequence: query; allocate/map/release; bind/unbind with guard tests; a real fence that signals; deterministic off-screen GPU work with readback; cross-process buffer sharing; then display presentation. Add negative tests for stale handles, malformed sizes, client death, queue timeouts, invalid mappings, and memory pressure.

## F06. GPU rendering and display are separate workstreams

Omacom's display Kconfig is a distinct driver with RTKit, KMS helpers, DMA/shmem objects, backlight, Type-C routing, and optional audio dependencies. It also has hardware-independent KUnit suites for firmware parsing, DMA ownership, reply correlation, display mode selection, and lifecycle policy.

Those tests are valuable assets. Porting their cases and fixtures can preserve important knowledge without booting a real DCP for every regression. Their Linux test harness does not itself run under XNU; extract testable protocol logic or supply a new runner.

Initial visible graphics can target a verified firmware framebuffer. That is not equivalent to accelerated rendering, KMS mode setting, external-display support, or safe sleep/wake. A driver that renders into memory but cannot safely present is not yet a desktop graphics stack.

[omacom/linux-aurora: `drivers/gpu/drm/apple/Kconfig`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/apple/Kconfig)

## F07. Apple platform work exists, but generation coverage must be explicit

PureDarwin's ARM platform expert includes `PDAppleAIC`. The inspected implementation reads Apple's `/arm-io` device tree and uses fixed legacy AIC offsets for event, target CPU, and masks, initially routing interrupts to CPU zero.

Omacom's interrupt driver contains separate AICv1, AICv2, and AICv3 layouts, including different register placement and per-die structure. Thus an existing Apple AIC class does not establish support for every M-series target. It is a useful implementation to extend or replace with target-appropriate code, not a complete modern Apple platform.

Target selection must validate AIC version, timer/FIQ delivery, per-CPU interrupts, secondary-core startup, memory granules, DART/IOMMU behavior, mailbox/RTKit services, power domains, and reboot. None of these physical behaviors was tested during this audit.

Start with one CPU, a RAM-backed root, a controlled console, and no internal-storage writes. Add secondary CPUs and DMA-capable devices only after their lifetime and isolation contracts pass. A hypervisor is a debugging facility; it is not by itself proof that experimental device access cannot damage host state or storage.

[PureDarwin/PureDarwin: `src/Kernel/Extensions/PDArmPlatformExpert/PDAppleAIC.cpp`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/PDArmPlatformExpert/PDAppleAIC.cpp); [omacom/linux-aurora: `drivers/irqchip/irq-apple-aic.c`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/irqchip/irq-apple-aic.c)

## F08. Newer Aurora and Gravity work is relevant, but is not a single superseding stack

Aurora's current `aura` project retains m1n1 ancestry and host/proxy facilities. Its README distinguishes its raw payload support from Mach-O boot-object work still subject to separate review. It also distinguishes inherited MIT code from newer GPL-2.0-only contributions.

Aurora's inspected kernel WIP configuration adds experimental GPU backends with precise board/firmware gates. Some paths retain firmware-visible allocations until reboot, block power transitions while DMA is owned, and qualify startup on provider readiness. Those are substantive constraints, not generic generation-wide support claims. Its Mesa UAPI includes GEM_MADVISE, but the entire header is not byte-identical to Omacom's.

Gravity's kernel has a distinct `DRM_ASAHI_M4` path for T8132. Its runner contains locked-system-register/SPTM boot-argument handling and removes unsupported exclave device-tree routes in one path. Its inspected Mesa UAPI enum lacks the extra MADVISE operation present in Omacom's integration tree. Gravity's public site describes its M4 Mac mini offering as an early alpha rather than a daily driver.

Use Omacom as the requested baseline; track these as additional upstreams. Select patches by hardware need, compatibility, provenance and tests. The audit did not compute a complete commit-by-commit ancestry comparison or validate a mixed-vendor release tuple.

[aurora-silicon/aura: `README.md`](https://github.com/aurora-silicon/aura/blob/78bac92d3e2725609605d2272956d5ca637b9461/README.md)

[aurora-silicon/linux: `drivers/gpu/drm/asahi/Kconfig`](https://github.com/aurora-silicon/linux/blob/05eaa1f71fa0352522bdf8c79d99c6e602ab7777/drivers/gpu/drm/asahi/Kconfig)

[aurora-silicon/mesa: `include/drm-uapi/asahi_drm.h`](https://github.com/aurora-silicon/mesa/blob/23f97d30e0ea3b37e8105346ae35932dee41d017/include/drm-uapi/asahi_drm.h)

[GravityLinux/bootloader: `proxyclient/tools/run_guest.py`](https://github.com/GravityLinux/bootloader/blob/c22cd92fe5383a029a379466632899bea6c73e69/proxyclient/tools/run_guest.py)

[GravityLinux/linux: `drivers/gpu/drm/asahi/Kconfig`](https://github.com/GravityLinux/linux/blob/4fc326f6c25ccceb7515e2635c22e01fde5d3bac/drivers/gpu/drm/asahi/Kconfig)

[GravityLinux/mesa: `include/drm-uapi/asahi_drm.h`](https://github.com/GravityLinux/mesa/blob/87ac51d411f0e58a9433567ec2666f457be89e1d/include/drm-uapi/asahi_drm.h)

## F09. Darling is useful selectively, and ARM64 build paths now exist

Darling's inspected top-level build explicitly selects Linux as its system and enables ARM64 slices on ARM64 hosts. Its architecture is still a Linux-hosted Mach-O/runtime environment, including a userspace kernel server. Native XNU already supplies Mach/BSD kernel services; importing that Linux emulation layer wholesale would create the wrong dependency direction.

The useful candidates are individual framework implementations, portability fixes, and tests. Its submodule manifest separates Foundation, CoreFoundation, libdispatch, Security, XPC, and other components. This audit did not recursively inspect every framework submodule or certify any proprietary application.

PureDarwin already has framework/runtime implementations, so compare at component level and retain a single owner for each ABI. Do not stack two incompatible versions of libSystem, dyld, or Mach IPC glue and assume duplicate coverage adds up to compatibility.

[darlinghq/darling: `CMakeLists.txt`](https://github.com/darlinghq/darling/blob/5b7e4870d743d40ea65410d719c6ab59e9497111/CMakeLists.txt); [darlinghq/darling: `.gitmodules`](https://github.com/darlinghq/darling/blob/5b7e4870d743d40ea65410d719c6ab59e9497111/.gitmodules); [darlinghq/darling: `README.md`](https://github.com/darlinghq/darling/blob/5b7e4870d743d40ea65410d719c6ab59e9497111/README.md)

## F10. Chonkstep needs a platform port, not a redesign of its interaction model

The canonical repository returned by GitHub is `chriskearney/chonkstep`. Its workspace pins a vendored Smithay 0.7.0. Both the native Wayland binary and compositor backend explicitly gate their functional dependencies to Linux; on other hosts the workspace can compile stubs/empty libraries for development tests.

The enabled backend requires DRM/GBM/EGL, libinput, udev, libseat and XWayland integration. The manifest says geometry/stacking live in `wm-core` rather than Smithay's `Space`, which is an encouraging separation. It does not prove the whole core or shell is already portable.

Keep the interaction model, theme and geometry code where dependencies permit. Introduce a Darwin-specific platform boundary for output, input, seat/device access, presentation, event delivery, and process/session integration. First prove nested or software-rendered operation on a Darwin environment, then a real display backend. Simply removing a `cfg(target_os = "linux")` gate would expose missing dependencies rather than implement them.

[chriskearney/chonkstep: `Cargo.toml`](https://github.com/chriskearney/chonkstep/blob/e0fe3a000aec71b1db46b940e2a75670be4e0f31/Cargo.toml); [chriskearney/chonkstep: `crates/chonkstep-wayland/Cargo.toml`](https://github.com/chriskearney/chonkstep/blob/e0fe3a000aec71b1db46b940e2a75670be4e0f31/crates/chonkstep-wayland/Cargo.toml); [chriskearney/chonkstep: `crates/wm-wayland/Cargo.toml`](https://github.com/chriskearney/chonkstep/blob/e0fe3a000aec71b1db46b940e2a75670be4e0f31/crates/wm-wayland/Cargo.toml)

## F11. Licensing and security are per-component gates

The inspected Omacom Asahi `driver.rs` and `gem.rs` explicitly offer GPL-2.0-only OR MIT. That is materially better for reuse than the earlier broad statement that the Apple GPU implementation is simply GPL-only. It does not change the licenses of dependencies. The inspected Linux interrupt controller file is GPL-2.0-or-later. Aura and Gravity also include GPL-licensed work.

Maintain a per-file provenance ledger and preserve authorship/notices. Keep host tooling, separately distributed bootloader code, userspace libraries, and linked kernel code distinct when assessing redistribution. This audit identifies license declarations; it does not resolve all legal questions about a combined distribution.

PureDarwin's extension build explicitly includes an allow-all AMFI stand-in. Its licensing documentation also identifies experimental cryptography and an older Darling-derived GPL component still used by Security.framework. Consequently, an XNU kernel here must not be marketed as inheriting macOS security properties. Authentication, certificate trust, sandboxing, signing policy, and crypto need their own review before this becomes a workstation carrying valuable credentials.

[PureDarwin/PureDarwin: `src/Kernel/Extensions/CMakeLists.txt`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/CMakeLists.txt); [PureDarwin licensing](https://puredarwin.org/handbook/licensing.html)

## Recommended architecture

```text
M5 macOS host: editor/agent, USB proxy, log collection, recovery control
        |
Linux builder (VM or remote): pinned PureDarwin images/KCs/toolchain
        |
Target Apple Silicon Mac: existing firmware -> pinned Omacom m1n1
        |
XNU + target platform services (interrupts, timers, MMU/DMA, mailbox/power)
        |
PureDarwin DRM/device infrastructure + new Asahi GPU/display adaptation
        |
Omacom Mesa integration branch, ported to the Darwin runtime
        |
Chonkstep core + Darwin-compatible compositor platform backend
        |
Native Unix tools; selected compatibility frameworks where proven useful
```

The Linux builder is not the target OS kernel. PureDarwin's current status says macOS hosts cannot produce its images/kernel collections/runners, so the host should orchestrate a supported Linux build environment rather than assume an all-macOS image build works. Targeting ARM64 does not make every x86_64 toolchain/application status claim apply to ARM64.

A Linux driver-domain design could retain Linux hardware drivers wholesale, but would require a separate virtualization/IOMMU/IPC architecture and would keep Linux in the runtime. It is an alternative research direction, not evidence that the native-XNU port is already solved.

## Acceptance gates

All gates below are proposed tests. None is marked passed by this audit.

| Gate | Required evidence | Rejection condition |
|---|---|---|
| A: reproducible baseline | Exact source/toolchain/board/firmware manifest; PureDarwin ARM64 VM shell; known-good m1n1 control boot | Unpinned dependencies or unreproducible control case |
| B: independent XNU entry | Locally built XNU reaches a distinctive marker with symbols and repeatable logs | Merely booting Apple's stock kernel again |
| C: platform/userspace | Timers, interrupts, stable RAM root, process lifecycle, basic userland | Hang on interrupt, memory alias fault, missing required proprietary platform driver |
| D: DRM substrate | Real buffer ownership, map/unmap, cross-process sharing, sync wakeups and timeout tests | Stubbed successful ioctls, stale backing, missing event delivery |
| E: GPU | Firmware startup; known off-screen computation/render; readback; client-death/timeout handling | A renderer string without verified hardware output; unsafe memory lifetime |
| F: display/desktop | Safe presentation and input; Chonkstep backend; no accidental Linux-only dependency | One screenshot without stable presentation or recovery |
| G: usable workstation | Networking/TLS, storage durability, shutdown/recovery, power management, security review, app tests | Assuming macOS binary compatibility or security from the kernel name |

Record every test with source hashes, firmware hashes, host/proxy version, target identity, boot profile, log, panic/register state, observed milestone, and recovery outcome. Firmware-owned pages must stay retained until a verified shutdown/reset boundary. Do not make blind reboot loops the default after a device failure. Keep partitioning, boot-policy changes, and internal-storage writes outside unattended early tests.

## Bottom line

The structural head start is real, especially the boot/debug environment, GPU knowledge and protocols, Mesa userspace, and PureDarwin's native DRM platform bridge. The missing work is also concrete. The best-supported next implementation is a bounded XNU platform/DRM prototype that preserves the Asahi userspace contract and ports selected hardware logic. It is not an indiscriminate merge of Darling, Linux drivers, and PureDarwin.

A successful source audit answers where the interfaces and risks are. Only build and hardware tests can establish that this particular combination boots, renders, survives failures, and runs the intended applications.

## Inspected source ledger

Ranges describe the material actually retrieved. A full blob hash identifies the source version; it does not imply the whole file was read when a range is listed.

1. [omacom/m1n1: `proxyclient/tools/run_guest.py`](https://github.com/omacom/m1n1/blob/121fcbf35d4501814b58ca5347926a4173b6a512/proxyclient/tools/run_guest.py). Read scope: full file; pinned identity rechecked at lines 1-70. Blob: `51743c7c9f83a353ecf49016f62e3baa65f25a2a`.

2. [omacom/linux-aurora: `drivers/gpu/drm/asahi/Kconfig`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/Kconfig). Read scope: full file. Blob: `0e989189206bade9d0ce2ec18aa7ee9f82619fb8`.

3. [omacom/linux-aurora: `drivers/gpu/drm/asahi/driver.rs`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/driver.rs). Read scope: 1-230. Blob: `80582ece5d23915951e256fdb0b5be6b0d072580`.

4. [omacom/linux-aurora: `drivers/gpu/drm/asahi/gem.rs`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/gem.rs). Read scope: 1-160. Blob: `72238159d04752e42cd3184ab5c36e975dd354ee`.

5. [omacom/linux-aurora: `include/uapi/drm/asahi_drm.h`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/include/uapi/drm/asahi_drm.h). Read scope: 50-130. Blob: `dc34315ec8c8229fef8de9b8885640df2c7206c6`.

6. [omacom/linux-aurora: `drivers/irqchip/irq-apple-aic.c`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/irqchip/irq-apple-aic.c). Read scope: 1-190. Blob: `1125008130ef4570dedf651a76284a86bfc48e05`.

7. [omacom/linux-aurora: `drivers/gpu/drm/apple/Kconfig`](https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/apple/Kconfig). Read scope: full file. Blob: `0675e5ac1b2e3c9d0508d13576435ffd62eca761`.

8. [omacom/mesa: `src/asahi/lib/agx_device.c`](https://github.com/omacom/mesa/blob/a19691114759e2c0507f3502cf6166ef2c6e2e33/src/asahi/lib/agx_device.c). Read scope: 1-110. Blob: `b24942a8ebae877c8121b98f4040e0a11e8e6ae8`.

9. [omacom/mesa: `include/drm-uapi/asahi_drm.h`](https://github.com/omacom/mesa/blob/a19691114759e2c0507f3502cf6166ef2c6e2e33/include/drm-uapi/asahi_drm.h). Read scope: 50-110. Blob: `a22a2ca73c9ac1a7465534d909189e8d3dd4b4b0`.

10. [PureDarwin/PureDarwin: `.gitmodules`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/.gitmodules). Read scope: full file. Blob: `a3b7a376e21b06e9174eb73c38d5aec6fda3f98a`.

11. [PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/pd_dri_cdev.h`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/pd_dri_cdev.h). Read scope: full file. Blob: `395083ab8450653ede86772e27e1d3eff4e4b759`.

12. [PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/fbsdkpi.cmake`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/fbsdkpi.cmake). Read scope: 1-280 requested; response truncated near end of range. Blob: `30ada3c64d0ef6a99142dc906045af599cf37332`.

13. [PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/drm/drm.cmake`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/drm/drm.cmake). Read scope: full file. Blob: `c804e8083562cfa91625e38c4154c3a47122baaa`.

14. [PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/drm/src/pd_sync_file.c`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/drm/src/pd_sync_file.c). Read scope: full file. Blob: `a8ceabd6f702ee00b50016fd4eb2236e34762af1`.

15. [PureDarwin/PureDarwin: `src/Kernel/fbsdkpi/drm/src/pd_lfile_lkpi.c`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/drm/src/pd_lfile_lkpi.c). Read scope: full file. Blob: `885677532fe01295fd5436c949b8c4bcb1706a77`.

16. [PureDarwin/PureDarwin: `src/Kernel/Extensions/CMakeLists.txt`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/CMakeLists.txt). Read scope: 1-240. Blob: `5715a0992837e3d823bf65b05edf24a5a838d1bf`.

17. [PureDarwin/PureDarwin: `src/Kernel/Extensions/PDDRMmsm/CMakeLists.txt`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/PDDRMmsm/CMakeLists.txt). Read scope: full file. Blob: `03f91fe7b7aa7ddaffbd9f983bef37d3e1ba40e8`.

18. [PureDarwin/PureDarwin: `src/Kernel/Extensions/PDArmPlatformExpert/PDArmPlatformExpert.cpp`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/PDArmPlatformExpert/PDArmPlatformExpert.cpp). Read scope: 1-220. Blob: `4c823527ae73a8ed62013b974d3e8b2be0766248`.

19. [PureDarwin/PureDarwin: `src/Kernel/Extensions/PDArmPlatformExpert/PDAppleAIC.cpp`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/Extensions/PDArmPlatformExpert/PDAppleAIC.cpp). Read scope: 1-220. Blob: `890ff07d4a98a76af9ae7dc284efd28c7b8c5673`.

20. [PureDarwin/PureDarwin: `nix/arm64.nix`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/nix/arm64.nix). Read scope: 1-180. Blob: `2f219188a26bd26794ff1c9e33637b9d24be5329`.

21. [PureDarwin/PureDarwin: `nix/lib/target-info.nix`](https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/nix/lib/target-info.nix). Read scope: full file. Blob: `d845143bf0619b664e191def38a19ee4bb87ce57`.

22. [darlinghq/darling: `CMakeLists.txt`](https://github.com/darlinghq/darling/blob/5b7e4870d743d40ea65410d719c6ab59e9497111/CMakeLists.txt). Read scope: 1-240. Blob: `3c520eaff2cbb750be7fce31e4224913d8fc7bfe`.

23. [darlinghq/darling: `.gitmodules`](https://github.com/darlinghq/darling/blob/5b7e4870d743d40ea65410d719c6ab59e9497111/.gitmodules). Read scope: 1-150. Blob: `0334fa35b3c380955a7e0403c023a1e902342591`.

24. [darlinghq/darling: `README.md`](https://github.com/darlinghq/darling/blob/5b7e4870d743d40ea65410d719c6ab59e9497111/README.md). Read scope: 1-170. Blob: `bb0f7c713e4c24b97bc256ebd1eb85456042cd7f`.

25. [chriskearney/chonkstep: `Cargo.toml`](https://github.com/chriskearney/chonkstep/blob/e0fe3a000aec71b1db46b940e2a75670be4e0f31/Cargo.toml). Read scope: full file; blob matched pinned tree. Blob: `94e46b13b7525b63b8737fb061c29d8c071c589a`.

26. [chriskearney/chonkstep: `crates/chonkstep-wayland/Cargo.toml`](https://github.com/chriskearney/chonkstep/blob/e0fe3a000aec71b1db46b940e2a75670be4e0f31/crates/chonkstep-wayland/Cargo.toml). Read scope: full file. Blob: `11390811fef20f13358963f8a01af2e98bb8d112`.

27. [chriskearney/chonkstep: `crates/wm-wayland/Cargo.toml`](https://github.com/chriskearney/chonkstep/blob/e0fe3a000aec71b1db46b940e2a75670be4e0f31/crates/wm-wayland/Cargo.toml). Read scope: full file. Blob: `081829a491066b3697431a65c88886000f16b964`.

28. [aurora-silicon/aura: `README.md`](https://github.com/aurora-silicon/aura/blob/78bac92d3e2725609605d2272956d5ca637b9461/README.md). Read scope: full file. Blob: `a40bd61d3f98435e1716668d55804a0da2f68460`.

29. [aurora-silicon/linux: `drivers/gpu/drm/asahi/Kconfig`](https://github.com/aurora-silicon/linux/blob/05eaa1f71fa0352522bdf8c79d99c6e602ab7777/drivers/gpu/drm/asahi/Kconfig). Read scope: 1-140. Blob: `6c5c31de0528a02347d00338157e7d1d4e52305b`.

30. [aurora-silicon/mesa: `include/drm-uapi/asahi_drm.h`](https://github.com/aurora-silicon/mesa/blob/23f97d30e0ea3b37e8105346ae35932dee41d017/include/drm-uapi/asahi_drm.h). Read scope: 50-115. Blob: `d8a332dd44401d3413092ee2c2372ab3bb04bfa4`.

31. [GravityLinux/bootloader: `proxyclient/tools/run_guest.py`](https://github.com/GravityLinux/bootloader/blob/c22cd92fe5383a029a379466632899bea6c73e69/proxyclient/tools/run_guest.py). Read scope: 1-170. Blob: `ef25e38dad1b1e3a10f1981d4137e05be6ddaf3d`.

32. [GravityLinux/linux: `drivers/gpu/drm/asahi/Kconfig`](https://github.com/GravityLinux/linux/blob/4fc326f6c25ccceb7515e2635c22e01fde5d3bac/drivers/gpu/drm/asahi/Kconfig). Read scope: full file. Blob: `daf29ca2864663b30278caf064825a474a39098b`.

33. [GravityLinux/mesa: `include/drm-uapi/asahi_drm.h`](https://github.com/GravityLinux/mesa/blob/87ac51d411f0e58a9433567ec2666f457be89e1d/include/drm-uapi/asahi_drm.h). Read scope: 50-115. Blob: `b1d6d35fea77bd365e64737a9817ff4e9e64a3bb`.

