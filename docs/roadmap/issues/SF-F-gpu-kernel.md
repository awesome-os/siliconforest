## Goal
The M1's GPU (AGX G13) driven by Omacom's kernel driver, running in our kernel, presenting the same interface to userspace as on Linux.

## Decision
**Reuse Omacom's Rust driver; do not rewrite it in C.** Build the Rust-for-Linux `kernel` crate on PureDarwin's Linux compatibility layer (`src/Kernel/fbsdkpi`: FreeBSD LinuxKPI plus drm-kmod) and compile the driver against it. Reasons:
- The driver's value is its firmware-version-specific structure definitions; a rewrite forks from Omacom permanently.
- PureDarwin's DRM core already includes the scheduler, GPU VM, exec and sync objects, the exact pieces this driver selects.
- Rust now runs in our kernel (proven on the M1, `reports/sprint-010/rust-kext-spike-001.md`).

Fallback if the crate approach stalls: a C port in the style of `PDPowerVR` (13,552 lines), using the Rust driver as the specification. Decided at F3: continue with the crate (see Results).

## Measured inputs (`docs/GPU_PLAN_NUMBERS.md`)
- Driver at the lab pin (Linux 7.2.11): 111,468 lines of Rust; the M1 needs the common code plus G13, **29,653 lines**. The rest is later GPU generations.
- It references about 27,600 lines of `kernel`-crate abstractions directly, calls 122 C symbols itself, and sits on about 9,000 lines of C (Apple RTKit and mailbox, DRM scheduler, GPU VM).
- It reads about 180 device-tree property names; on Linux, m1n1 synthesises many GPU ones (`src/kboot_gpu.c`, 3,400 lines). Darwin has only Apple's device tree.
- Matched pair, proven daily on this M1: `linux-aurora 7.2.11.aurora6` with `mesa-aurora 26.3.0_devel.20261025.147a62f6190` (omacom/mesa branch `integration/aurora`).
- Gaps: no Rust in PureDarwin's kernel build before our spike; the DRM core there is at about the Linux 6.14 level while the Rust wrappers target 7.2.

## What exists
- `integrations/puredarwin/m1-rust-kext/`: toolchain (rustc 1.95 from the pinned package set, custom target, own-built `core`/`alloc`), `PDRustHello` proven on hardware. The crate work is in `lkpi/` (bindings generation, helpers, patches, `PDRustLKPI`).
- RTKit, power domains, DART from the Wi-Fi work (to be shared through SF-A).
- Earlier, never-run GPU groundwork: `integrations/puredarwin/m1-gpu-*` (3,139 lines of C: UAT page tables, RTKit control, channel codec).

## Results so far (2026-11-09; `docs/roadmap/research/rust-kernel-crate-on-lkpi.md`)
**Proven on the M1 (boot `r02`):** the Rust-for-Linux `kernel` crate's core tier runs in our kernel on the compatibility layer. Test kext `PDRustLKPI` printed all 15 predicted lines and `ALL OK`: `pr_info!`, `KBox`/`KVec`/`KVVec`, `Mutex`, `SpinLock`, `Arc`, atomics, errno conversion, monotonic time, a work item with a completion, `CondVar`. Wi-Fi in the same boot was unaffected.

**Cost of the core tier:** 136 mechanical one-line gates plus +41/-12 hand-written lines against the pristine crate, and 641 lines of C helpers (most Linux functions are macros in LinuxKPI, so each one Rust calls needs a helper).

**Compile-only status of what the driver needs** (nothing below has run):
- Bindings: 58 of 94 headers the Linux helper includes are present and parse; 35 are absent (among them GEM shmem helper, Apple RTKit/mailbox/PMP, io-pgtable).
- Only `device_id` compiles in the subsystem tier. Everything else is blocked, with each blocker listed in the research file.
- Of the driver's 122 direct C symbols, 44 exist.
- DRM wrappers have few errors of their own; all are version skew between drm-kmod's 6.14-level C and the 7.2 wrappers (`drm_sched_init` arguments, `drm_gpuvm_sm_map` request struct, missing immediate-mode GPUVM calls).

**Decision at F3: go, with three adjustments.**
1. **Keep the unmodified crate for the core tier and the DRM wrappers**, and bring the DRM scheduler and GPU VM C code to 7.2 rather than back-porting the wrappers (the driver relies on the newer behaviour).
2. **Write thin replacement modules for `device`, `driver`, `platform`, `devres`** with the same Rust surface, over PureDarwin's platform glue. Those wrap Linux driver-core internals newer than LinuxKPI and unrelated to how a device arrives here (an IOKit nub and Apple's device tree).
3. **Implement the roughly 15 `apple_rtkit_*`/`apple_mbox_*` C functions over our existing RTKit client** (SF-A A3) and keep the Rust wrappers unmodified, instead of porting Linux's RTKit drivers.

**Two properties of the compatibility layer block the driver outright and come first:**
- LinuxKPI's `PAGE_SIZE` is 4 KiB on our 16 KiB kernel; the driver requires 16 KiB.
- Every `kmalloc` is a blocking allocation with 8-byte alignment; atomic context and stricter alignment are not honoured.

**Packaging fact:** LinuxKPI is not a shared kext. Each kext statically links its own hidden copy (plus the DRM core for DRM kexts). The GPU kext will carry its own.

## Work items
| ID | Deliverable | Acceptance | Size (estimate) |
|---|---|---|---|
| F1 | Bindings from the compatibility layer's headers, with status tables | **Done** (compile evidence) | done |
| F2 | `kernel` crate core tier on LinuxKPI | **Done, proven on the M1** | done |
| F3 | Subsystem status matrix and go/no-go | **Done: go, as above** | done |
| F4 | LinuxKPI page size 16 KiB (one define plus an audit of its page code) | Existing DRM kexts still build; a page-size check in `PDRustLKPI` passes on the M1 | ~520 lines audited |
| F5 | Allocation backend: aligned and non-blocking `kmalloc` | `KBox<u128>` succeeds; an atomic-context allocation test passes on the M1 | ~150 lines of C |
| F6 | Lock lifetime: Rust locks release their XNU lock on drop | Leak counter stays flat over 10,000 lock create/drop cycles | ~30 lines of Rust |
| F7 | Helpers for io, irq, uaccess; bindgen fixes for the `file` and `free` macro renames | `io`, `irq`, `uaccess`, `dma`, `dma_fence` modules compile | ~250 lines |
| F8 | Replacement `device`/`driver`/`platform`/`devres` modules over an IOKit nub and Apple's device tree; `of` on top | A test kext binds to a device-tree node and reads properties through `kernel::of` on the M1 | ~1,500 lines of Rust |
| F9 | DRM C core to 7.2: scheduler (~2,700 lines), GPU VM (~3,400), small `drm_gem`/`drm_syncobj`/dma-fence changes; port the GEM shmem helper (~1,000 lines) | `drm::{sched,gpuvm,gem,gem::shmem,syncobj}` compile; existing DRM kexts still build | port from Linux 7.2 |
| F10 | `apple_rtkit_*` and `apple_mbox_*` C functions over SF-A's RTKit; PMP stub or port | `soc::apple::{rtkit,mailbox}` compile; a coprocessor reaches "ready" from Rust | ~600 lines of C |
| F11 | Decide per use: xarray (extend, ~400 lines), maple tree (port ~7,000 or replace), cpufreq/freq_qos, shrinker, hrtimer, module parameters | Each has a recorded decision and the module compiles or the driver use is removed | varies |
| F12 | Device-tree synthesis for the GPU: the T8103 part of m1n1 `kboot_gpu.c` | Property dump matches what Linux sees on this M1 | ~600 lines |
| F13 | Build the G13 subset of the driver and boot the GPU firmware | Kernel log: firmware version detected, coprocessor ready, no crash | 29.6k lines compiled |
| F14 | `/dev/dri` render node speaking `asahi_drm.h` | `drmGetVersion` returns `asahi`; `GET_PARAMS` matches Linux on this M1 | small |
| F15 | First render | Mesa's off-screen test (SF-G G5) passes by pixel hash | integration |

F4-F7 are independent of each other. F8, F9 and F10 can run in parallel once F4-F5 land. F12 is independent of everything.

## Dependencies
SF-A (RTKit A3, PMGR A2). SF-G depends on F14. Independent of SF-H until zero-copy scanout.

## Interfaces this ticket owns
`integrations/puredarwin/m1-rust-kext/` and the future GPU kext directory; the Rust toolchain definition (`rust/toolchain.nix`). Any other Rust-in-kernel user builds on that toolchain rather than adding one.

## Risks
- The kernel collection has one global symbol namespace; Rust code shared between kexts must live in one kext with explicit C exports.
- Allocation semantics (blocking vs atomic) differ between LinuxKPI-on-XNU and Linux.
- API drift 6.14 to 7.2 in scheduler and GPU VM.
- GPU firmware state when the hypervisor hands over is unverified.
