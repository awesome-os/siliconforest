# What a GPU Wayland client (Mesa EGL, pin 147a62f6190) needs from the compositor

Read from `src/egl/drivers/dri2/platform_wayland.c` and `src/loader/` at the pin. Nothing here
has run yet. "Mesa" below means the client-side EGL Wayland platform (`eglGetDisplay` on a
`wl_display`, `eglCreateWindowSurface` on a `wl_egl_window`).

## Short version for the compositor

Implement **`zwp_linux_dmabuf_v1` at version 3**. That is enough with the Darwin Mesa build
from this directory (it carries one patch for device discovery, see "Render node"). Version 4
(feedback) is not required and is more work; if you advertise 4, everything in "If you
advertise version 4" becomes mandatory, because Mesa then ignores the v3 events.

| Item | Required | Detail |
|---|---|---|
| `zwp_linux_dmabuf_v1` | **yes, version 3** (advertise exactly 3 unless you implement feedback) | Mesa binds `min(advertised, 4)` and ignores the global below 3 |
| events on bind | **`modifier(format, hi, lo)`** for each pair you accept | Mesa ignores the `format` event entirely; a format with no `modifier` event does not exist for it |
| formats | **`XR24` (XRGB8888) and `AR24` (ARGB8888)** | these give the usual 8-bit EGL configs. Optional extras Mesa knows: `XB24`/`AB24`, `XR30`/`AR30`, `XB30`/`AB30`, `RG16`, 16-bit and float formats |
| modifiers | **`DRM_FORMAT_MOD_LINEAR` (0x0)** for both formats | asahi allocates with explicit modifiers and takes the intersection of yours with its own (`0x0` linear, `0x0c00000000000001`, `0x0c00000000000002`). Advertise the two Apple ones only if your importer (the same Mesa, `EGL_EXT_image_dma_buf_import_modifiers`) accepts them; linear is what the KMS demo proved. Do not advertise only `DRM_FORMAT_MOD_INVALID` |
| `create_params` → `add` → `create_immed` | **yes** | Mesa always uses `create_immed(width, height, format, flags=0)` (interface v2+), never `create`/`created`. One plane: `add(fd, plane_idx=0, offset=0, stride, mod_hi, mod_lo)` with the modifier it chose. A failed import must be a protocol error on the params object (`invalid_wl_buffer`), not silence |
| `wl_buffer.release` | **yes** | a surface has at most 4 buffers; Mesa blocks in `eglSwapBuffers` (spamming `wl_display.sync`) until one is released |
| `wl_surface.frame` → `wl_callback.done` | **yes** | default swap interval is 1: the next `eglSwapBuffers` blocks until the callback of the previous commit fires. With interval 0 it uses `wl_display.sync` instead |
| `wl_surface.damage_buffer` | if `wl_surface` version ≥ 4 | below that Mesa sends `damage(0, 0, INT32_MAX, INT32_MAX)` |
| fd passing | **yes** | the dma-buf fd arrives by `SCM_RIGHTS` in `add`; Mesa closes its copy right after sending |
| `wl_drm` | **no** | this build has no `wl_drm` code at all (`-Dlegacy-wayland=` empty, the default) |
| explicit sync (`linux-drm-syncobj-v1`) | **no** | not used by EGL at this pin |
| `wp_presentation` | no | bound at version 1 if present and used for timing feedback only |
| `wl_fixes` | no | bound if present |
| `wl_shm` | not for GPU clients | Mesa uses it only in its software fallback, which the demo treats as a failure |

Synchronisation is implicit: Mesa flushes and commits without waiting for the GPU. The
client's driver attaches its render fence to the dma-buf (`DMA_BUF_IOCTL_IMPORT_SYNC_FILE`),
and a compositor that samples the buffer through the same Mesa waits on it automatically
(`DMA_BUF_IOCTL_EXPORT_SYNC_FILE` in asahi's submit path). Both ioctls exist on the current
kernel (boot nk01). A compositor that reads the buffer some other way must wait on the
dma-buf itself (`poll` for `POLLIN`).

What the compositor does with the buffer is the path the KMS demo proved from the other side:
import the fd with `EGL_EXT_image_dma_buf_import` (fourcc, width, height, plane 0 fd, offset,
pitch, modifier lo/hi) into an `EGLImage` and texture from it.

## Render node: how the client finds the GPU without udev

Unmodified Mesa at this pin has exactly one way: the compositor's **default feedback**
(`get_default_feedback`, version 4) sends `main_device` as a `dev_t`, and Mesa maps it to a
render node with `drmGetDeviceFromDevId` and opens that. With a version 3 server there is no
device, `wl_drm` is compiled out, and hardware initialisation fails; `eglInitialize` then
falls back to software rendering over `wl_shm`.

The Darwin build here adds one patch (`patches/0006-egl-wayland-open-render-node-without-feedback.patch`):
when the server gave no device, Mesa enumerates DRM devices itself (`drmGetDevices2`, which
SiliconForest's libdrm answers from `sysctl kern.sf_dri.devices`) and opens the first one that has
a render node. On this system that is the asahi node; the display driver has none. So:

- v3 server + this Mesa: works, no device information needed from the compositor.
- The assumption baked in: client and compositor use the same GPU. True here.

## If you advertise version 4

Then all of this is required, because at version ≥ 4 Mesa ignores `modifier` events:

- `get_default_feedback`: `format_table(fd, size)` (an fd the client can `mmap` read-only,
  `MAP_PRIVATE`; 16-byte entries: `u32 format, u32 pad, u64 modifier`), `main_device`,
  at least one tranche (`tranche_target_device`, `tranche_flags`, `tranche_formats` with
  `u16` indices into the table, `tranche_done`), then `done`.
- `get_surface_feedback` for every EGL window surface, same event set; Mesa reallocates the
  surface's buffers when it changes.
- `main_device` and `tranche_target_device` carry `sizeof(dev_t)` bytes: **4 on Darwin**, not
  8 as on Linux. Send `st_rdev` of the DRM node. An open `/dev/dri` node here is a clone whose
  minor differs from the node's; SiliconForest's libdrm compares only the low byte of the minor,
  so either value works.
- The format-table fd must be mappable by the client; on this OS that means a regular or
  `shm_open` file, since there is no `memfd`.

## What the client side needs on this OS (my side, for reference)

- libwayland-client and libwayland-egl for Darwin (`wayland-egl-backend` ≥ 3), wayland-scanner
  on the build machine, wayland-protocols ≥ the version Mesa's meson asks for.
- `ppoll` (Mesa's Wayland dispatch helper uses it; SiliconForest's Mesa recipe already shims it).
- `poll` on the Wayland socket and `SCM_RIGHTS` for dma-buf fds.

## Checked against `chonkstep-darwin/NEEDS.md` (as published on 10 November)

The planned server surface (version 3; `modifier` events for XR24 and AR24, linear only;
`create_immed`; `wl_buffer.release`; frame callbacks on flip-complete; no `wl_drm`, no
feedback, no `wp_presentation`) is sufficient for the Mesa build here. Its section 2 is met:
device discovery without compositor help is patch 0006, Mesa accepts a version 3 global, and
with only the linear modifier on offer asahi allocates linear back buffers.

One point differs from how Mesa behaves:

- NEEDS.md says rendering must be complete at commit and suggests `glFinish()` before it.
  `eglSwapBuffers` does **not** finish; it flushes and commits. `agx-wl-demo` calls
  `glFinish` itself before every swap, so it complies. An ordinary Mesa client will not.
  It is still safe as long as the compositor samples the buffer through Mesa's asahi driver
  (as it does: `EGLImage` texture): the client's driver puts its render fence on the dma-buf
  and the compositor's driver waits for that fence when its own submission uses the buffer.
  That relies on the dma-buf sync-file ioctls of the current kernel (boot nk01) and has not
  been seen working between two processes yet; `agx-wl-demo` does not depend on it.

Smaller notes: Mesa attaches at offset 0,0 and never sets a buffer scale or transform; it keeps
up to 4 buffers per surface and reuses a buffer only after `release`; with an XR24 config (the
demo prefers one) alpha never matters.
