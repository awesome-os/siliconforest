# Chonkstep compositor: what it offers GPU clients, and what it needs

Written by the compositor side (`sources/chonkstep-darwin`, local branch `wm-darwin`).
Read by the Mesa side (Wayland EGL platform on the Darwin Mesa build, GLES Wayland client).

Status on 10 November: **section 1 is implemented** on the local `wm-darwin` branch (commits
`5597bc5` linux-dmabuf, `d59e0d9` implicit sync and checks) and built into the binaries
`tools/wm_gl_boot.sh` stages. Checked on the host by wire tests of the protocol and buffer
lifetime (with file-backed stand-in fds); importing a real client dma-buf into the
compositor's GL context has **not run on the M1 yet** (`WM_GL_STEP=wlgpu` and
`WM_GL_STEP=dmabuf` are the boots for that). It agrees with
`m1-mesa-asahi/WAYLAND-NEEDS.md`; the one point that differed (synchronisation) is changed
below.

## 1. Server protocol surface for dma-buf clients

Global: `zwp_linux_dmabuf_v1`, **version 3** (no feedback objects; a client that binds 4 or 5
will find the global advertised at 3).

On bind the server sends, for each supported pair, `format` (for version < 3 clients) and
`modifier(format, modifier_hi, modifier_lo)`:

| DRM format | Modifier |
|---|---|
| `XRGB8888` (`XR24`) | `DRM_FORMAT_MOD_LINEAR` (0) |
| `ARGB8888` (`AR24`) | `DRM_FORMAT_MOD_LINEAR` (0) |

More modifiers (the asahi ones the GL import lists besides linear, `0x0c00000000000002` and
`0x0c00000000000001` on boot km01) will be added only after the compositor has imported such a
buffer into its own GL context on the M1. Do not rely on them; allocate **linear**.

Requests the server handles:

- `zwp_linux_dmabuf_v1.create_params`, `.destroy`
- `zwp_linux_buffer_params_v1.add(fd, plane_idx, offset, stride, modifier_hi, modifier_lo)`:
  one plane only (`plane_idx` 0). A second plane is a protocol error (`plane_idx`).
- `zwp_linux_buffer_params_v1.create_immed(buffer_id, width, height, format, flags)` and
  `.create(width, height, format, flags)` (answered with `created` or `failed`).
  The buffer is imported into the compositor's GL context at this point, and the request
  succeeds only if that import does. A refused description (`y_invert` or other flags, a
  second plane, `stride < width * 4`, a modifier that is not advertised, an import EGL
  rejects) is `failed` for `create` and the protocol error `invalid_wl_buffer` for
  `create_immed`. A **format** that was never advertised is the protocol error
  `invalid_format` for both.
- `zwp_linux_buffer_params_v1.destroy`

What a client buffer must be:

- One plane, `XRGB8888` or `ARGB8888`, linear, row 0 = top row, `stride >= width * 4`,
  `offset + stride * height <=` the dma-buf's size.
- `ARGB8888` pixels are **premultiplied** alpha (as on every Wayland compositor).
- Importable by `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` in the compositor's context
  (Mesa asahi, surfaceless EGL, GLES 3.2) and bindable with `glEGLImageTargetTexture2DOES`
  to `GL_TEXTURE_2D`. A buffer allocated on `/dev/dri/renderD128` (driver `asahi`) with
  `DRM_IOCTL_ASAHI_GEM_CREATE` and exported with `drmPrimeHandleToFD` is the tested shape
  (it is what the compositor's own scanout buffers are).

Surface semantics, the same as for `wl_shm` buffers on this compositor today:

- `wl_surface.attach` + `damage`/`damage_buffer` + `commit`. Buffer scale 1 only, no
  transform, attach offset 0,0 (anything else is a protocol error, as for shm).
- The buffer is **not copied**. The compositor samples the client's memory when it renders,
  so the client must not draw into a buffer again until it receives `wl_buffer.release`.
  `release` is sent when the surface has a newer buffer committed and the frame showing the
  newer one has been handed to the display. A client therefore needs at least two buffers,
  three to never stall.
- **Implicit sync: the client need not finish before it commits.** (Changed after
  `WAYLAND-NEEDS.md`: ordinary Mesa clients only flush at `eglSwapBuffers`.) What the
  compositor does: before it renders a frame it polls the dma-buf fd of every freshly
  committed buffer for readability, without blocking, and holds the frame back until all are
  readable (their write fences have signalled); its loop wakes on that fd. A buffer still not
  readable after 100 ms is counted (`WMDMABUF total ... fence_timeouts=`) and the frame is
  rendered anyway. On top of that the compositor samples the buffer through the same Mesa
  asahi driver, which waits for the fence in its own submission. Neither path has been seen
  working between two processes yet; a client that calls `glFinish()` before the swap (as
  `agx-wl-demo` does) depends on neither.
- `wl_surface.frame` callbacks fire on the display's flip-complete event (60 Hz), one per
  presented frame. Use them to pace rendering.
- `xdg_wm_base` / `xdg_toplevel` as for shm clients; window size comes from the first
  buffer. `xdg-decoration`: server-side decorations are available.
- Not offered: `wp_presentation`, `wp_viewporter`, `wl_subcompositor`, `wl_drm`,
  `zwp_linux_explicit_synchronization_v1`, dmabuf feedback (v4+), YUV or multi-plane formats.

## 2. What the Mesa Wayland EGL platform will meet

- It must work without `wl_drm` and without dmabuf feedback: pick the render device itself
  (`/dev/dri/renderD*` whose `drmGetVersion` name is `asahi`), not from a compositor event.
- It must accept a `zwp_linux_dmabuf_v1` global of version 3.
- It must allocate linear `XRGB8888`/`ARGB8888` back buffers (the `modifier` events list only
  linear).

## 3. Requests to the kernel / Mesa side

### 3.1 Two-finger click and two-finger scroll from the built-in trackpad (open)

A person using the desktop could not open the desktop menu with a two-finger click. The
compositor cannot fix this alone:

- What it reads: `/dev/usb_hid_mouse`, 12-byte SFHID pointer records
  (`SFHIDPushMouseEvent` / `SFHIDPushAbsoluteMouseEvent`): `u32 sequence, u8 device,
  u8 buttons (bits 0-2), i8 dx | u16 x, i8 dy, i8 wheel, u8 absolute, u16 y`. There is no
  finger count in a record.
- Where they come from: `IOHIDEventService.cpp` turns the trackpad's generic HID pointer
  reports into those records (`buttons & 7`). The SPI trackpad child
  (`m1-trackpad-child/DESIGN.md`) leaves the device in its basic pointer mode ("Mode gate
  remains closed ... neither enables multitouch nor decodes vendor touch data"), in which a
  physical click is button 1 whatever the number of fingers on the pad, and there is no scroll.

Request, to whoever owns the trackpad driver: with the pad in multitouch mode (Linux
`applespi` is the reference for the mode switch and the finger frames), translate in the
kernel, so the record format and every reader stay as they are:

1. physical click with exactly two fingers down -> report **button bit 1 (right)** instead of
   bit 0 for that press and its release;
2. two fingers moving without a click -> **`wheel`** steps (the record's signed byte; the
   compositor maps one step to one wheel detent, 120 in `v120`), and no pointer motion.

Nothing changes on the compositor side when this arrives: a right-button record already opens
the desktop menu (the unattended session proves that path with synthetic records:
`WMAUTO menu ... right_button_record_opens_menu=true`), and `wheel` is already delivered to
clients as vertical scroll. If the driver would rather hand the finger count to userspace, the
smallest change is the count in the record's `device` byte's upper bits or a new 13th byte;
say which here and the compositor will do the translation instead.

Until then the desktop has a workaround that needs no kernel change: **Control + click is a
secondary click** (the whole press is the right button).

## 4. Facts about the compositor side that may help

- Selected at run time; a compositor running the CPU renderer (`--renderer cpu`, or no DRM
  nodes) will not advertise `zwp_linux_dmabuf_v1` at all.
- Socket: `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`, as the session scripts set them
  (`integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-session.in`).
- Our own client for testing the server side (allocates on the render node, fills on the
  CPU, attaches through `zwp_linux_dmabuf_v1`) is
  `integrations/siliconforest/chonkstep-darwin/client-dmabuf/`.
- What the compositor prints about dma-buf clients: `WMDMABUF import id= size= format=
  modifier= stride= zero_copy=1` for the first eight buffers; `WMDMABUF check window=
  commit=30|150 ... differing_pixels= max_channel_diff=` (the window's region of the
  compositor's own GL frame against the client's buffer read through a CPU `mmap` of the
  dma-buf fd, so **the fd should be mappable read-only**; if it is not, the line says
  `not-mappable` and gives a hash and `flat=` instead); `WMDMABUF total ...` at exit.
