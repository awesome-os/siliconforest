# m1-rust-kext: Rust in XNU arm64 kexts, and the Asahi GPU driver built on it

Everything here is built with nix only:

```
export PATH=$HOME/.nix-profile/bin:$PATH
F="--extra-experimental-features nix-command --extra-experimental-features flakes"
cd integrations/siliconforest/m1-rust-kext
nix-build $F <recipe>.nix --no-out-link
```

The SiliconForest tree is not edited. Every change to it is a patch applied in these recipes
(`lkpi/patches-layer/`), and each patch header says whether it should go upstream.

## What is here

| Path | What |
| --- | --- |
| `pins.nix`, `rust/toolchain.nix` | The pinned SiliconForest flake, cross toolchain, and the Rust target (`aarch64-apple-xnukext`), sysroot, `mkCrate`, `mkKextObject` |
| `rust/`, `kext/`, `build-rust.nix`, `build-kext.nix`, `build-kc.nix` | Phase A: `SFRustHello`, plain Rust in a kext |
| `lkpi/` | The Rust-for-Linux `kernel` crate (Linux 7.2.11) on SiliconForest's Linux layer: bindgen, helpers, module gating, patches, the module matrix |
| `lkpi/sfrustlkpi/`, `build-lkpi-kext.nix`, `build-lkpi-kc.nix` | Phase B/C test kext `SFRustLKPI` |
| `lkpi/sfrustplat/`, `build-plat-kext.nix`, `build-plat-kc.nix` | Phase D test kext `SFRustPlat` (a `platform::Driver` on the GPU node) |
| `agx/`, `build-agx-kext.nix`, `build-agx-kc.nix` | The GPU driver kext `SFAgx` and its kernel collection |

## GPU driver: how it is built

`SFAgx.kext` is the Asahi AGX driver from `sources/gpu-plan/linux-aurora/drivers/gpu/drm/asahi`
(Linux 7.2.11), common code plus the G13 backend, for the M1 (T8103). It is GPL-2.0 as a whole.

### Inputs

| Input | Used for | How it is taken |
| --- | --- | --- |
| SiliconForest tree at `e7aa00a6` (flake, `pins.nix`) | the Linux layer (`src/Kernel/fbsdkpi`), kext build system, `kc-builder` | flake output `drm-msm-arm64` and its `src` |
| `sources/gpu-plan/linux-aurora/rust` | the `kernel` crate and its vendored crates | `builtins.path` |
| `sources/gpu-plan/linux-aurora/drivers/gpu/drm/asahi` | the driver | `builtins.path`, then `agx/patches/` |
| `sources/gpu-plan/linux-aurora/include/uapi/drm/asahi_drm.h` | the driver's uAPI for bindgen | by path |
| `../m1-drm72/patches/drm-kmod` (8 patches), `../m1-drm72/patches/siliconforest` (2) | DRM core at Linux 7.2, GEM shmem helper | applied to the flake's drm-kmod source and to the tree |
| `../m1-gpu-rtkit/src` | Apple mailbox + RTKit port, `SFRtkitPlatform.cpp` | copied into the kext as `rtkit/` |
| `../m1-gpu-dt` | `SFGpuDT.kext` (powers GFX, publishes `gpu@206400000`); `kernel/sf_linux_fwnode.c` for the layer | `build-kext.nix` imported; one file copied into the layer |
| `../m1-lkpi-extras` | xarray, `module_param`, `mm`, `seq_file`, extra bindings | sub-paths: `c/`, `helpers/`, `bindgen/`, `rust/patches/` |
| `../m1-apcie-host/build-kc.nix`, `../m1-bwfm/build-kc.nix` | the base kernel collection without / with Wi-Fi | imported by `build-agx-kc.nix` |

### Build steps (attributes of `lkpi/default.nix`, in order)

1. `tree`: the tree slice with `lkpi/patches-layer/*.patch`, plus `sf_linux_fwnode.c` and
   `sf_xarray.c`/`sf_xarray.h` copied in. `finalTree`, `finalDrmkmod`: m1-drm72's series on top.
2. `bindingsGen`: bindgen against the layer's include set (`cflags.nix` is the one place the C
   flags are written), the C helpers compiled one object per file into `libhelpers.a`.
3. `rustTree`: Linux's `rust/` with every `kernel` module gated (`tools/gate.py`), Linux-only
   errnos gated, then `lkpi/patches/` and m1-lkpi-extras' `rust/patches/`.
4. `kernelAgx`: the `kernel` crate with the module list `modules.agx` (`modules.nix`).
5. `agxSrc`: the driver with `agx/patches/`. `asahi`: the driver crate (`--cfg sf_g13`,
   staticlib). `agxObject`: one prelinked object, exports `___asahi_init`, `___asahi_exit`,
   `_sf_dev_printk_selftest`.
6. `build-agx-kext.nix`: `SFAgx.kext` (CMake target in the tree: the layer, the DRM core with the
   shmem helper, the RTKit port, `agx/kext/*`, the Rust object) and `SFDRMDri.kext`.
   `$out/nix-support/` has the compile lines, `nm -u` and the link map.
7. `build-agx-kc.nix`: the kernel collection. `--arg wifi true` adds the Wi-Fi kexts. The recipe
   fails if fewer than 4 pages are left before the kernel's `__TEXT`.

### Patch series

Counts are added/removed lines.

`lkpi/patches-layer/` (against the SiliconForest tree; all "upstream: yes" except 0008):

| Patch | +/- |
| --- | --- |
| 0001 page size follows the kernel (16 KiB) | +9 -1 |
| 0002 xnu seam: non-blocking malloc, lock storage, physmem, page bulk allocation, owned-page mapping | +255 -5 |
| 0003 Haiku locks embed the xnu lock | +44 -36 |
| 0004 allocator: exact sizes, alignment, realloc | +58 -16 |
| 0005 driver-core surface of Linux 7.2 | +229 -4 |
| 0006 of: text properties named by the node | +27 -1 |
| 0007 xarray source in the LinuxKPI list | +1 -1 |
| 0008 platform bus pre-probe hook (interim) | +9 -0 |
| 0009 `si_meminfo()` reports the machine's memory | +7 -2 |
| 0010 `dma_map_sg` for platform devices | +10 -4 |
| 0011 page pool for single pages, vmap through owning descriptors | +204 -16 |
| 0012 DRI ioctl restart codes are EINTR | +5 -0 |
| 0013 thread and area tables off the image | +14 -2 |
| 0014 a thread slot is dropped when its thread_t is reused | see file |
| 0015 xnu memory pressure runs the shrinkers and trims the page pool | see file |
| 0016 scoped_guard labels are unique | see file |
| 0017 backlight and platform_driver fields of Linux 7 | see file |
| 0018 of_find_compatible_node continues after `from` | see file |
| 0019 a dma-buf made by another DRM kext is recognised | see file |
| 0020 select/poll/kqueue on DRM fds wake on events (also patches SFDRMDri) | see file |
| 0021 dma-buf sync-file ioctls (a GPL-2.0-only file: ported from the pin's dma-buf.c) | see file |
| 0022 select/poll/kevent on sync_file and dma-buf descriptors (two GPL-2.0-only files; needs the kernel of `../m1-dcp/kernel.nix`) | see file |
| 0023 mmap of a dma-buf descriptor (a GPL-2.0-only file; needs kernel patch 0003 and, for shmem objects, `../m1-drm72/patches/drm-kmod/0009`) | see file |
| 0024 a sync_file made by another DRM kext is recognised | see file |
| 0025 WQ_HIGHPRI work queues run above ordinary kernel threads (boot-arg `sflkpi_wq_highpri=0` for the old behaviour) | see file |
| 0026 a linux file's descriptor ops run in the layer copy that made the file (EFAULT on every ioctl of the other kext's dma-buf, boot mm01) | see file |
| 0027 backlight class helpers as Linux has them (ops_lock, get/set brightness) | see file |
| 0028-0040 | see the files |
| 0041 taskqueue: a task freed by its own function is not read again (the GPU-teardown panic of 14 November; SF-AC) | see file |

Counts for 0002, 0005, 0008 and 0011 above are from an earlier state; each patch's first
lines say what it holds now (0002 also carries the seam halves of 0009, 0011, 0014, 0015; 0008
also enters a device in the bus table before its probe).

`lkpi/patches/` (against Linux `rust/`): 0001 +24 -12, 0002 +1, 0003 +16, 0004 +3 -3,
0005 +10 -3, 0006 +7 -3. m1-lkpi-extras adds 1001 (+73) and 1002 (+16).

`agx/patches/` (against `drivers/gpu/drm/asahi`):

| Patch | +/- | |
| --- | --- | --- |
| 0001 G13-only build (`--cfg sf_g13`) | +50 -0 | G17 and M3 backends not compiled |
| 0002 SiliconForest's Linux layer | +42 -25 | no `address_space`; firmware page tables reached through a mapping; one `c_char` cast |
| 0003 temporary diagnostics | +64 -9 | `SFAgx: diag:` lines; remove when no longer needed |

The patch series are regenerated with `tools/regen/` (`layer/regen.sh setup|split`,
`agx/regen.sh setup|split`, `rust/mkpatches.sh`); `lkpi/patches-layer/0007` is hand-written
and kept by the layer script. The patch files are the source of truth.

Both collection recipes end with `tools/kc_overlap.py`, the lab launcher's image-gate rule: a
collection whose segments would overlap (kc-builder puts `__PRELINK_INFO` after the last kext
and accepts it running into the kernel) fails the build.

### Stubs

`SF_STUB` in `agx/kext/sf_agx_linux.c`: `asahi_sysfs_register`, `asahi_sysfs_register_idle`,
`asahi_sysfs_unregister`. The layer has no sysfs, so `agx_stats` and `gpu_memory` are not
exported. Registered shrinkers are never called (m1-lkpi-extras README).

### Boot

```
tools/agx_boot.sh <name> [extra boot-args]
```

builds `build-agx-kc.nix` and the Mesa off-screen test and boots once. Boot-args:

| Boot-arg | Effect |
| --- | --- |
| `sfgpudt=2 sfgpudt_stringprops=1` | SFGpuDT powers GFX and publishes the GPU node (required) |
| `sfagx=1` | SFAgx attaches (default off) |
| `sfagx_diag=1` | allocation/mapping preflight before the probe, per-mapping and per-firmware-message lines |
| `asahi.<param>=<value>` | the driver's module parameters, e.g. `asahi.debug_flags=0x10` |
| `sfrtkit_debug=1`, `sfrtkit_irq=0\|1\|2` | RTKit port (see `../m1-gpu-rtkit/README.md`) |

State on hardware is recorded in `reports/sprint-010/` by whoever ran the boot; this directory
only holds build recipes.

### Module matrix

`nix-build $F lkpi -A matrix` compiles each `kernel` module the driver could use on top of the
core tier. Last run: 28 compile, 6 blocked (`debugfs`, `maple_tree`, `fs`, `cred`, `cpufreq`,
`time::hrtimer`); the G13 driver path uses none of the six.
