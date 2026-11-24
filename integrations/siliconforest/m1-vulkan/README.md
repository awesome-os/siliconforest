# m1-vulkan: Vulkan on the M1 under Darwin (Honeykrisp from Omacom's Mesa)

State, results on the M1 and open items: `docs/roadmap/issues/SF-AC-vulkan.md`. This file says
how the pieces are built and run.

## Sources

| Piece | Source | Licence |
|---|---|---|
| Mesa (Honeykrisp, Vulkan runtime, Wayland WSI, gallium asahi) | omacom/mesa `integration/aurora` `b7b03c5b3b8` (what omarchy-pkgs `mesa-aurora`/`vulkan-aurora` package with `-Dvulkan-drivers=asahi`), GitHub archive, sha256 in `common.nix` | MIT |
| Patches | `../m1-mesa-asahi/patches/0001`-`0011` (shared with the GL builds; apply to `b7b03c5` unchanged), `patches/0101`-`0103` here (Vulkan only) | MIT (Mesa's) |
| Vulkan loader, vulkaninfo, vkcube | Khronos 1.4.341 from the flake's nixpkgs, through SiliconForest's recipes (`nix/pkgs/mesa/vulkan-loader.nix`, `vulkan-tools.nix`) | Apache-2.0 |
| `test/vk-tri.c`, `test/sync-probe.c`, `tools/*` | original | MIT |

## Build (x86-64 Linux builder, nix only)

```
export PATH=$HOME/.nix-profile/bin:$PATH
F="--extra-experimental-features nix-command --extra-experimental-features flakes"
cd integrations/siliconforest/m1-vulkan
nix-build $F mesa-native.nix --no-out-link     # host tools for b7b03c5 + Linux Honeykrisp/lavapipe/drm-shim
nix-build $F mesa-darwin.nix --no-out-link     # arm64 Darwin: libvulkan_asahi.dylib + GL + EGL (Wayland)
nix-build $F stage.nix --no-out-link           # the tree for /private/var/db/sfvk on the M1
nix-build $F test/test.nix -A linux --no-out-link
```

`mesa-darwin.nix` is `../m1-mesa-asahi/mesa-darwin-build.nix` called with `vulkan = true`, this
directory's `common.nix` and `mesa-native.nix`, and `patches/*.patch` added. The new arguments
alone left the GL derivations unchanged (`.drv` paths compared); the clock and ETIME fixes below
then went into the shared part on purpose, so the GL builds of `../m1-mesa-asahi` (147a62f) carry
them too from now on. **The GL Mesa installed on the M1 is still the one built before them**: the
compositor's `/usr/local/lib/mesa-asahi/` libraries (`libgallium-26.3.0-devel.dylib`,
`libEGL.1.dylib`, `libGLESv2.2.dylib`, from 12 November; `libdrm.2.dylib` is not affected) would change when it is next rebuilt and installed; that has
not been done (SF-AC).

`patches/experimental/` is applied by no recipe (zink without MoltenVK; it builds, it does not run yet).

| Patch | Why |
|---|---|
| `0101-vulkan-image-drm-format-modifier-on-darwin` | `vk_image.drm_format_mod` exists only for Linux/BSD; SiliconForest is a DRM system (patch 0001), Honeykrisp and the WSI need it |
| `0102-asahi-vulkan-icd-link-args` | Honeykrisp was the one ICD not linked with `vulkan_icd_link_args`; on Darwin that carries `-undefined dynamic_lookup` (weak entrypoint tables), as SiliconForest's PowerVR build does |
| `0103-hk-instance-accept-mach-o-uuid-as-build-id` | `vkCreateInstance` returned `VK_ERROR_INITIALIZATION_FAILED` on the M1: Mach-O's build id is the 16-byte `LC_UUID`, Honeykrisp wanted a 20-byte SHA-1 |

| `../m1-mesa-asahi/patches/0011-c11-monotonic-time-is-the-kernels-uptime-on-darwin` (shared) | Mesa's absolute DRM deadlines come from `CLOCK_MONOTONIC`; the kernel's DRM layer counts `CLOCK_UPTIME_RAW` (mach_absolute_time), and Darwin's `CLOCK_MONOTONIC` was 10.06 s behind it on the M1: every finite `vkWaitForFences` returned `VK_TIMEOUT` at once (`test/wait-probe`) |

Not a patch file (shared with the GL builds as well): the recipe force-includes `sf-compat/sf-etime.h`
(`#define ETIME ETIMEDOUT`). The kernel's LinuxKPI returns Linux's `ETIME` as `ETIMEDOUT` (60),
as FreeBSD's does; Darwin's `<errno.h>` has a STREAMS `ETIME` (101). Mesa compares DRM wait results
with `ETIME`, so a syncobj wait that timed out became `VK_ERROR_UNKNOWN` on the M1.

## UAPI: kernel `asahi_drm.h` (linux-aurora `6a08a08e`, what SFAgx is built from) against Mesa

`tools/uapi_compare.py <kernel header> <mesa header>`: every `struct drm_asahi_*` laid out by
clang (size and field offsets), every `DRM_IOCTL_ASAHI_*` number, every `DRM_ASAHI_*` constant.

- Against `b7b03c5`: 148 of 178 items equal. All 12 ioctl numbers equal. `drm_asahi_params_global`
  is now identical (616 bytes; at `147a62f` Mesa had 600). Differences: `cmd_render` 448 (kernel)
  vs 320 (Mesa) and `cmd_compute` 200 vs 128, size-negotiated through `drm_asahi_cmd_header.size`
  as before; Mesa-only wrapper structs (`*_with_prior`, `*_with_ray_query`,
  `*_with_stage_prior`, `render_program*`, `compute_program`) and flags `COMPUTE_USES_MATRIX`,
  `RENDER_COMPUTE_STAGE_BARRIERS`, `RENDER_SHADER_WRITES`, `RENDER_PROGRAM_*`; kernel-only
  `COMPUTE_WIDE_VISIBILITY`, `RENDER_CACHED_PROGRAM_PROBE`, `RENDER_FRAGMENT_DEPENDENCY*`,
  `VM_CREATE_HAL200`. Every feature bit name and value is the same on both sides.
- Against `147a62f` (the GL Mesa on the M1): 130 of 163 equal, as `../m1-mesa-asahi/README.md`
  found (21 constants the kernel has and that Mesa lacked then; b7b03c5 has caught up).
- What Honeykrisp sends on G13 is chosen from the kernel's feature bits: `*_with_prior` only with
  `EXACT_PRIOR_BARRIERS`, `*_with_stage_prior` only on G17/M5, `*_with_ray_query` only for a
  render with native ray queries (`RENDER_RAY_QUERY`); otherwise the 320-byte prefix
  (`hk_drm_render_command_size`). Which bits our kernel reports: SF-AC.

## Run on the M1 (installed SiliconForest, over SSH; nothing installed system-wide)

```
tools/sfvk.sh push                         # stage -> /private/var/db/sfvk (old one kept as sfvk.prev)
tools/sfvk.sh run vulkaninfo --summary
tools/sfvk.sh run sync-probe
tools/sfvk.sh run vk-tri 100
tools/sfvk.sh revert                       # sfvk.prev back
tools/sfvk-careful.sh <name> '<command>'   # a GPU run that might take the machine down (see below)
```

Any run that submits GPU work goes through `tools/sfvk-careful.sh`: it refuses while
`runs/m1-coord/gui.lock` or `core.lock` is held, writes the announcement to `runs/m1-coord/log.txt`,
runs `sync` on the M1, and streams the kernel log and the command's output to
`runs/sfvk/<name>.txt` on the workstation once a second, during the run and for 110 s after it (the
GPU scheduler's job timeout is 100 s). A crash leaves the last second of kernel log here. It also
routes the M1's uart0 to the USB-C debug pins (`macvdmtool serial` on the M5, no reset) and captures
`/dev/cu.debug-console` there (`tools/m5_uart_capture.py`) for the whole run: the installed kernel's
console is uart0 (`serial=0x13`), so a panic's text lands in `runs/sfvk/<name>-uart.txt`. The routing
does not survive a reboot of the M1. Results of every run: `runs/sfvk/` (not committed).

`run` sets `VK_ICD_FILENAMES=/private/var/db/sfvk/icd.d/asahi_icd.json`,
`DYLD_LIBRARY_PATH=/private/var/db/sfvk/lib` (vulkaninfo and vkcube `dlopen` the loader by leaf
name) and puts `bin/` first in `PATH`. Removing the stage: `rm -rf /private/var/db/sfvk*`.

## Tests

- `vk-tri [frames]`: 256x256 RGBA8, a flat triangle that must match a CPU reference exactly (vertices
  on a 1/4-pixel grid, no pixel centre on an edge) and an interpolated one compared with a
  tolerance of 2; `flat_fnv` is the decisive number (`0x5965baca`). On the builder: lavapipe
  `VKTRI PASS` (flat 0x5965baca, full 0xcdfedca7, interpolation max difference 1); Honeykrisp on
  the noop drm-shim `VKTRI FAIL` (the shim renders nothing; the test does fail when nothing is
  drawn).
- `vk-tri` options (all in its header): `VKTRI_MODE=copy|linear|fill`, `VKTRI_TIMEOUT_MS`,
  `VKTRI_HOLD_S` (stay alive after a failure), `VKTRI_EXIT_AFTER_SUBMIT=<n>` (submit n times and
  `_exit` without waiting: a process dying with GPU work in flight, for the kernel teardown bug).
- `gl-bench [frames]`: an off-screen GLES2 fill-rate workload with an exact-image check, to compare GL
  drivers (native asahi against zink) on the M1; built by hand against the dev tree for the 14 November
  comparison (`SF-AC`), not by a recipe.
- `wait-probe`: finite syncobj waits with deadlines from `CLOCK_MONOTONIC` and from `CLOCK_UPTIME_RAW`
  (no GPU work); prints the clocks. On the M1 before patch 0011: 0.0 ms and 302 ms for a 300 ms wait.
- `agx-params`: `GET_PARAMS` group 0, every field and the feature bits (read-only).
- `sync-probe`: timeline syncobj ioctls, sync_file export/import/poll, syncobj fds, SCM_RIGHTS of
  syncobj/sync_file/dma-buf descriptors, `DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE`, poll on a dma-buf.
