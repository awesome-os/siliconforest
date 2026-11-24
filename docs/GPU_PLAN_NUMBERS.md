# GPU plan: measured inputs (2026-11-09)

Sources measured, fetched at the lab's pins into `sources/gpu-plan/` (untracked):
`omacom/linux-aurora` 6a08a08e (Linux 7.2.11) and `omacom/mesa` a1969111 (26.3.0-devel).
Nothing here has been built or run; these are counts and file-presence checks.

## Kernel driver (`drivers/gpu/drm/asahi`, Rust)

| What | Lines |
|---|---|
| Whole driver, 265 files | 111,468 |
| `g17/`, `g15/`, `m3/` (later GPU generations, not the M1) | 81,815 |
| Remainder: common code plus the M1's G13 | 29,653 |

- It uses 12 DRM ioctls of its own (`DRM_IOCTL_ASAHI_*`), DRM sync objects and PRIME.
- Kconfig selects DRM scheduler, GPUVM, GEM shmem helper and Apple RTKit, and requires 16 KiB pages.
- It reads about 180 distinct device-tree property/compatible strings. On Linux many GPU
  properties are synthesised by m1n1 (`kboot_gpu.c`, 3,400 lines) from the Apple device tree;
  Darwin has only the Apple tree, so an equivalent has to exist on our side.

## What the driver needs underneath it

Rust-for-Linux `kernel` crate in that tree: 63,177 lines, plus 3,047 lines of C helpers.
Modules the driver references directly, with their size:

| Module | Lines | Module | Lines |
|---|---|---|---|
| drm (gem, sched, gpuvm, syncobj, file, ioctl) | 5,802 | sync | 4,825 |
| alloc | 3,364 | io | 1,919 |
| time | 1,681 | dma | 1,251 |
| debugfs | 1,028 | xarray | 778 |
| uaccess | 664 | error | 663 |
| maple_tree | 656 | device | 633 |
| of | 588 | platform | 581 |
| soc/apple (rtkit, mailbox) | 502 | dma_fence | 479 |
| page | 455 | driver | 441 |
| task | 435 | others (str, types, fmt, ...) | ~900 |

About 27,600 lines of Rust abstraction referenced directly. The driver also calls 122
distinct C symbols through `bindings::` itself (dma_fence, memremap, workqueues, cpufreq and
freq_qos, shrinker, notifier, sha256, a few `of_*`).

C code the Rust side wraps and that must exist: Apple RTKit and mailbox (2,948 lines),
`drm_gpuvm.c` (3,423), DRM scheduler (2,733), and the DART page-table code for the GPU's UAT.

## What PureDarwin already has

- `src/Kernel/fbsdkpi`: FreeBSD LinuxKPI (416 headers, 33,170 lines of C) plus 56,254 lines of
  PureDarwin glue, at `LINUXKPI_VERSION=61400`.
- Its DRM core source list already includes `drm_gpuvm.c`, `drm_exec.c`, `drm_syncobj.c`,
  `drm_gem.c`, `drm_prime.c` and `scheduler/sched_*.c`. It hosts amdgpu, i915, radeon, nouveau.
- Two ARM GPU precedents: `PDDRMmsm` with Mesa freedreno through `/dev/dri` (`PDDRMDri`,
  cross-built libdrm), and `PDPowerVR` (13,552 lines) with Mesa 26.0.3's PowerVR Vulkan driver.
- No Rust in the kernel build: no rustc/bindgen in the nix kernel recipes.
- From the Wi-Fi work: a working RTKit client (SMC), PMGR power-domain code, DART code.
- Earlier lab GPU groundwork never run on hardware: `integrations/puredarwin/m1-gpu-*` (3,139 lines of C).

## Mesa side

- Gallium driver `src/gallium/drivers/asahi`: 24,225 lines; winsys 77 lines; shared compiler and
  libraries under `src/asahi` (the raw count, 783,977 lines, is dominated by generated and
  test data and includes the Vulkan driver).
- Matched pair (corrected 2026-11-09): the M1 in this lab runs `mesa-aurora
  26.3.0_devel.20261025.147a62f6190` (omacom/mesa, branch `integration/aurora`, not `main`) on
  `linux-aurora 7.2.11.aurora6`, and that combination works daily. Use Mesa commit `147a62f6190`
  with the 7.2.11 kernel driver. Their copies of `asahi_drm.h` still differ textually (9 lines only
  in Mesa, 25 only in the kernel: newer feature bits on the kernel side, extra command-struct
  variants on the Mesa side); the interface is negotiated through feature bits and sizes, so a
  textual difference is not a mismatch. My earlier "the pins do not match" reading was wrong for
  the shipped pair. The lab's Mesa pin `a1969111` is newer than what ships and should be replaced.

## Gaps this makes visible

1. Rust in the kernel: toolchain, bindgen against LinuxKPI headers, and the `kernel` crate built
   on LinuxKPI instead of Linux. This is new for PureDarwin.
2. API level: the Rust abstractions target Linux 7.2's DRM scheduler and GPUVM; PureDarwin's DRM
   core is at the 6.14 level.
3. LinuxKPI holes for the 122 direct C symbols and whatever the used `kernel` modules call
   (cpufreq/freq_qos, shrinker, maple tree, `of_*` over the Apple device tree, RTKit).
4. The device-tree properties m1n1 normally synthesises for the GPU.
5. Mesa `147a62f6190` (the shipped pair) built for PureDarwin's `/dev/dri` path.
