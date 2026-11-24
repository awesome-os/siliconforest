<!-- Research input for the roadmap tickets; produced by a read-only research agent on 2026-11-09. Unverified on hardware. -->

# (A) GPU userspace

**A1. Current desktop (read from source; no timings exist)**
- The compositor is `chonkstep-darwin`, crate `sources/chonkstep-darwin/crates/wm-darwin` (1,603 lines of `src/`), not wlroots or sway. It uses only Smithay 0.7's `wayland_frontend`, with wm-core and tiny-skia; `wl_shm` only, no dmabuf, no GL.
- Pixels go through PDGOP to an `IOGOPFramebuffer` user-client mapping of the iBoot framebuffer: 2560x1600, stride 0x2800, depth `0x1001e` (x2r10g10b10), PA 0xbd5d5c000, 0xfa0000 bytes. The mapping is write-combined via `integrations/puredarwin/m1-wayland/iogop-vram-write-combine.patch`.
- No vsync and no scaling: `refresh: 0`, `Scale::Integer(1)` (`server.rs:59-66`); buffers with scale ≠ 1 are refused (`server.rs:244`).
- Loop (`main.rs:56-66`): one thread; dispatch, drain HID, repaint if dirty, flush, `sleep(8 ms)`.
- Each dirty frame is full-screen with no damage tracking:
  1. new 16 MB pixmap, fill, composite (`scene.rs:152`);
  2. per-pixel `convert()` into a second new 16 MB buffer (`pixels.rs:118`);
  3. 4.1M single `write_volatile` stores (`presenter.rs:106`).
- Every pointer event sets dirty (`input.rs:100`); SHM commits are copied per pixel (`pixels.rs:17`).
- `reports/sprint-010/passive-child-lag-observation-notes.json` says nothing was measured. Inferred bottleneck: three 16 MB passes plus 32 MB of fresh allocation per pointer motion. The lab's Darwin boots are m1n1-hypervisor guests (AGENTS.md); the overhead is unmeasured.

**A2. What Mesa asahi/honeykrisp need** (structure from the newer local checkout `a1969111`; `147a62f6190` itself was not read)

| Need | Evidence | PureDarwin status |
|---|---|---|
| Render node, `drmGetVersion` name `asahi`, `drmGetDevice2` reporting `DRM_BUS_PLATFORM` | `hk_physical_device.c:1857`; loader | Provided: `PDDRMDri.cpp` (445 lines) platform nodes plus `kern.pd_dri.devices`; libdrm `puredarwin-dri.patch` (207 lines). Precedent PDDRMmsm; untested for asahi. |
| 12 `DRM_IOCTL_ASAHI_*`, `GEM_CLOSE` | `agx_device.c` (1,800 lines) | ioctl transport exists (`fbsdkpi/drm/src/pd_drm_cdev.c`, 233 lines); the driver is the kernel track. |
| mmap of GEM offset on the device fd, 16 KiB alignment | `agx_device.c:284,461` | Provided: `pd_cdev_mmap_register` device pager (`kern_mman.c:296`). Fixed-address use untested. |
| PRIME handle/fd | `agx_device.c:482,617` | Provided: `drm_prime.c` plus `pd_dma_buf.c` (421 lines). |
| dma-buf sync-file import/export | `agx_device.c:1630,1649` | Missing: the dma-buf ioctl handler accepts only `DMA_BUF_IOCTL_SYNC` (`pd_dma_buf.c:98`). |
| dma-buf fd mmap | generic consumers | Missing: `dma_buf_mmap` returns `EOPNOTSUPP` (`pd_dma_buf.c:361`). |
| Passing dma-buf and sync fds over a Unix socket | linux-dmabuf, explicit sync | Missing: `DTYPE_PDLFILE` is not in `fg_sendable()` (`xnu/bsd/kern/kern_descrip.c:390`). |
| Sync objects | `drmSyncobj*` calls | Core has `drm_syncobj.c`; ioctl waits should work (inferred). |
| sync_file wait and merge | `agx_fence.c:169`, libsync | Partial: `pd_sync_file.c` (212 lines) has merge/info. `poll()` yields `POLLNVAL` because `pd_lfile` has `fo_no_kqfilter` (`pd_lfile.c:115`, `sys_generic.c:1790`); `select` is level-only with no wakeup (`pd_lfile.c:87`). |
| Same-file check (kcmp) | winsys screen dedup | Provided: `F_PD_SAMEFILE`, patched in `nix/pkgs/mesa/mesa.nix`. |
| Build | `meson.build:161,1034` | Missing: `mesa.nix` (502 lines) builds upstream 26.1.6 with freedreno/radeonsi variants and `-Dgbm=disabled`. Needs an asahi variant on Omacom `147a62f6190`: host `mesa_clc`/`vtn_bindgen2`, a `sys/sysmacros.h` shim, and keeping `src/asahi/meson.build:6` off its macOS-tooling branch. |
| GBM | compositors | Missing: `src/Libraries/libgbm/gbm.c` is a 283-line PDSurface veneer. |

**A3. Compositor use**
- In-tree wlroots 0.20.1 is built `-Drenderers=[] -Dallocators=[]` (`nix/pkgs/wayland/wlroots.nix:103`). Its PureDarwin backend (858 lines) copies rows into the PDGOP framebuffer or flips a PDSurface. neuwld is built with drm disabled.
- A GLES-on-GBM/DRM path needs real GBM and a KMS device; neither exists before a DCP driver.
- "Scanout" before DCP means the GPU renders to an off-screen buffer, then a readback and one CPU copy into the iBoot framebuffer. There is one buffer and no flip, so tearing is unavoidable and pacing is by timer.
- Zero-copy clients need: a linux-dmabuf global, EGL dma-buf import (`DRM_CAP_PRIME` is already patched for Apple in `mesa.nix`), sendable fds, and implicit or explicit sync. The last two are missing.

**A4. Recommendation and work items** (sizes are my estimates)

Keep Chonkstep's own compositor and add a GL renderer to it. Do not switch to wlroots.

| # | Item | Acceptance test | Size |
|---|---|---|---|
| 0 | CPU-path fix, independent of the GPU: damage tracking, buffer reuse, row-wise pack | Per-frame counters exported; pointer motion repaints only damaged rows | 3-5 days |
| 1 | `mesaAsahiArm64Build` on `147a62f6190` | Dylibs link; loader selects asahi from the driver name | 1-2 weeks |
| 2 | libdrm enumeration test for a platform render node | `drmGetDevices2` returns the node, on the msm target | 2 days |
| 3 | `pd_lfile`: sendable, kqueue filter, select wakeups | Sync fd crosses a socketpair; `poll` blocks then wakes on signal, on an existing PD DRM target | 1 week |
| 4 | dma-buf: sync-file ioctls, mmap | Round-trip test | 1 week |
| 5 | Stage 1: `src/Userspace/pdgltri` (surfaceless EGL, readback-checked, already in tree) on asahi | Pixel hash equals the llvmpipe reference; a renderer string is not evidence (`specs/GRAPHICS_CONTRACT.md`) | Days after the kernel driver |
| 6 | Stage 2: GLES renderer in wm-darwin (Smithay `renderer_gl` on surfaceless EGL, inferred feasible), readback to framebuffer | Nonce frame matches the CPU renderer; frame time logged | 2-3 weeks |
| 7 | Stage 3: linux-dmabuf, client EGL Wayland | An es2gears-class client with no SHM copy | 2 weeks; needs 3 and 4 |
| 8 | Stage 4: honeykrisp ICD (`vulkan-loader.nix`, `pdvktri.nix` exist), then Zink | Headless triangle readback | 1-2 weeks |

Items 0-4 need no GPU kernel driver.

Risks:
- PureDarwin's Mesa patches use `--replace-fail` against 26.1.6 and will break on the fork.
- Fixed-address mmaps are untested on this path.
- Readback cost at 16 MB per frame.
- `asahi_drm.h` feature negotiation between the Mesa and kernel pins.

# (B) Display controller

> **Correction, 2026-11-10.** Part B below was read from `/home/chrisk/source/aurora-linux`, which is a 7.1.12 tree
> from a different fork, not the lab pin. The pin is Omacom `linux-aurora` branch `integration/aurora` at `6a08a08e`
> (7.2.11, the kernel this M1 runs), checked out at `sources/gpu-plan/linux-aurora`. At the pin:
> - `drivers/gpu/drm/apple` is 132 files, 61,295 lines (36 test files, 13,708 lines). The module without audio and
>   tests is about 44,300 lines; the internal-panel core for firmware 12.3/13.5 is 19,664 lines of C plus headers
>   (`dcp.c` 5,216, `iomfb_template.c` 2,833, `iomfb.c` 2,786, `afk.c` 2,348, `parser.c` 1,917, `apple_drv.c` 1,361).
>   `dcp.c` references the fabric, external, 14.7, 26.6 and H17P code unconditionally, so a port compiles all of it
>   or cuts and stubs.
> - Licence: 120 of 127 source files are `GPL-2.0-only OR MIT`, 2 `GPL-2.0+ OR MIT`, `dcp_trace.c` GPL-2.0, four
>   headers without an SPDX line.
> - For J293 the bind path is `apple_rtkit_init` (`dcp.c:3996`) then plain `apple_rtkit_wake` (`:4014`); the
>   early-AP wake is for other hardware. All `file:line` references below are to the old tree and are wrong for the pin.
> - 20 headers the driver includes are absent from our layer and 6 more are empty placeholders (`component.h` matters
>   most); the list is in `integrations/puredarwin/m1-gpu-rtkit/README.md`, "DCP mode".
> - State at OS start, from this M1's console (not from source): m1n1 power-cycles the DCP, sets the mode through the
>   iBoot endpoint and quiesces it; it reports `dart-dcp` locked and `dart-disp0` not locked, the reverse of B5 below.

**B5. What DCP is and its state at OS start**
- It is a coprocessor running RTKit firmware. T8103 nodes (`arch/arm64/boot/dts/apple/t8103.dtsi:625-701`):
  - `dcp@231c00000` with five display register ranges;
  - ASC mailbox `@231c08000` (AIC 427-430);
  - `dcp_dart@23130c000`;
  - `disp0_dart@231304000`, which iBoot leaves locked (comment at `:705`), with `piodma` on stream 4;
  - power domain `ps_disp0_cpu0`, clock `clk_disp0`, a PMGR scratch register (`apple,bw-scratch`).
- Endpoints (`dcp-internal.h:66-75`): system 0x20, disp0/iBoot 0x23, dpavserv 0x28, AV 0x29, DPTX 0x2a, IOMFB 0x37, among others.
- The IOMFB RPC is firmware-specific: one template (`iomfb_template.c`, 1,561 lines) is compiled for 12.3 and for 13.5. This M1's GPU firmware is 13.5 per AGENTS.md; DCP matching it is inferred.
- State at OS start (m1n1, MIT): on an internal retina panel `display_init()` returns "already initialized" without starting DCP (`src/display.c:774`). The exception is the Sequoia power-cycle path (`:767`): m1n1 boots RTKit, sets the mode, then quiesces rather than sleeps (`display_shutdown(DCP_SLEEP_IF_EXTERNAL)`, `main.c:299,339`; `display.c:804`).
- Either way the OS inherits firmware loaded by iBoot scanning out one fixed buffer. Linux then calls `apple_rtkit_wake` (`dcp.c:2543`).
- Under the hypervisor: the same `main.c` sequence runs before the proxy. The grep of `proxyclient/m1n1/hv/__init__.py` showed only video boot-arg edits; that file was not read in full. Which m1n1 path this M1 takes is unknown; read the m1n1 log.
- The simple framebuffer (IOGOPFramebuffer here; `apple,simple-framebuffer` with `drivers/gpu/drm/sysfb/simpledrm.c` on Linux) cannot do: mode set, flip or vblank, brightness, DPMS or sleep, external displays, HDR or colour management, overlay or cursor planes.

**B6. Linux driver** (`/home/chrisk/source/aurora-linux/drivers/gpu/drm/apple`, 15,256 lines in 43 files)

- **Licence:** `GPL-2.0-only OR MIT` on 32 files, so the MIT option is available. Exceptions: `dcp_trace.c` is GPL-2.0, and four headers (`audio.h`, `av.h`, `dptxep.h`, `hdmi-codec-chmap.h`) have no SPDX line.
- **Internal-panel core:** `dcp.c` 2,910, `afk.c` 1,293, `parser.c` 1,062, `iomfb.c` 681, template 1,561, `apple_drv.c` 887, `plane.c` 490, `dcp_backlight.c` 258, `connector.c` 156, `systemep.c` 137, `ibootep.c` 29.
- **External display adds:** `dptxep.c` 878, `av.c` 434, `audio.c` 776, `epic/dpavservep.c` 302, plus atcphy, Type-C mux and DP-tunnel code outside the directory. `dcpext` is enabled for T8103 in this tree (`t8103-jxxx.dtsi:179`).
- **Dependencies:**
  - RTKit (`drivers/soc/apple/rtkit*.c`, 1,599 lines) and mailbox (538);
  - DART (`apple-dart.c` 2,576, `io-pgtable-dart.c` 483) through the iommu API;
  - DRM atomic and KMS helpers, GEM-DMA and FB-DMA helpers;
  - component, `of_graph`, backlight class, phy, mux.
- **Present in PureDarwin's LinuxKPI:** atomic/KMS core (`fbsdkpi/drm/drm.cmake`), `backlight.h`, `platform_device.h`, a 29-line `iommu.h`.
- **Missing or empty there:** `component.h`, `of_address.h`, `of_graph.h`, `soc/apple/rtkit.h`, `phy.h`, `reset.h`, `drm_gem_dma_helper.h`, `drm_fb_dma_helper.h`.
- **Backlight is not standalone:** brightness is applied through an atomic commit, i.e. a swap (`dcp_backlight.c:159-172`, `iomfb_template.c:514`).
- **Formats:** XRGB/ARGB8888, XBGR, 2101010, NV12/P010 (`plane.c:136-165`). Cursor planes may be rejected, so software cursor is the fallback (`plane.c:65`).
- **Maturity:** this exact tree drives this M1 daily (inferred from `docs/GPU_PLAN_NUMBERS.md`). From memory, unverified: the driver is not in mainline Linux, and OpenBSD ships a port of it (apldcp/apldrm) on its own DRM compatibility layer.

**B7. Options and recommendation**

1. **Port the Linux driver onto LinuxKPI as a DRM kext (recommended).** PureDarwin already bridges KMS drivers to IOFramebuffer through `fbsdkpi/drm/include/pd_drm_fb.h` (66 lines): modes, `set_mode`, `flip` returning vblank sequence and time, and `export_fd` (a dma-buf of the scanout buffer). `pd_amdgpu_fb.c` (885 lines) is the model. This gives the compositor and the GPU work exactly what they need.
2. **Native IOKit rewrite.** This re-implements about 9.5k lines of firmware-version-specific RPC for no benefit.
3. **Minimal native flip service on m1n1's iBoot-endpoint client** (`src/dcp_iboot.c`, 341 lines, MIT: set power, set mode, swap begin/set-layer/end). Supporting code is `afk.c` 1,258, `rtkit.c` 861, `dart.c` 1,144. `PDSun60iDisplay` (573 lines) is the precedent for the flip interface. It has no brightness and no vblank event, so it is a fallback only.

Work items (sizes are my estimates):

| # | Item | Acceptance test | Size |
|---|---|---|---|
| 1 | Shared RTKit and mailbox as LinuxKPI `apple_rtkit_*`, reusing the lab's `PDAppleSMC` client | DCP wake; endpoint and service announcements logged; panel still lit; console returns | 2 weeks |
| 2 | DART for the `dcp` and locked `disp0` streams behind the iommu API, extending `m1-apcie-host/dart/pd_dart_pool.cpp` | Map/unmap test; existing framebuffer mapping preserved | 2 weeks |
| 3 | LinuxKPI gaps: component, `of_graph`, DMA helpers, reset, PMGR domain, clock | Driver core compiles and links | 1-2 weeks |
| 4 | IOMFB bring-up: takeover, one full-screen swap | Nonce frame visible; swap-complete count about 60/s; first real vsync | 3-4 weeks |
| 5 | Backlight service | Brightness steps observed on the panel; value restored after DPMS | 1 week after 4 |
| 6 | `pd_drm_fb` glue: flip and `export_fd` | Compositor page-flips tear-free | 1-2 weeks |
| 7 | DPMS and sleep | Panel off and on without reboot | 1-2 weeks |
| 8 | External display: dcpext, DPTX, atcphy | A monitor lights up | 6+ weeks; defer |

- Backlight alone is not a valid first deliverable; the first one is item 1, read-only.
- Shared platform dependencies: RTKit, DART, PMGR, AIC interrupts. SMC is not needed for the panel.
- Dependency on GPU work: none until zero-copy scanout (item 6 with the A-side dma-buf items).

Risks:
- Taking over from iBoot can blank the panel with no fallback console; keep serial and test under the hypervisor first.
- The locked DART limits remapping.
- Firmware-version mismatch (12.3 vs 13.5 templates).
- `/dev/dri` select has no wakeups (`pd_drm_cdev.c`), so vblank events would need polling or the `pd_lfile` fix from A.