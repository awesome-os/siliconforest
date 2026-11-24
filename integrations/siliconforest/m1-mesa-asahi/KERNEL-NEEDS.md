# What the Mesa side needs from the kernel side (kept by the m1-mesa-asahi work)

Each entry: the exact call, what the kernel answers today, where the call comes from, what
happens instead, and whether a path exists that avoids it. "Observed" means seen on the M1;
"expected" means derived from reading both sides and not yet seen in a boot log.

## Zero-copy present (`agx-panel-demo --kms`)

### 1. dma-buf sync-file ioctls (expected, not yet observed; not blocking)

- Calls: `ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE)` (`_IOWR('b', 2, struct dma_buf_export_sync_file)`)
  and `ioctl(dmabuf_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE)` (`_IOW('b', 3, struct dma_buf_import_sync_file)`).
- Kernel today: `ENOTTY` for every dma-buf ioctl except `DMA_BUF_IOCTL_SYNC`
  (`src/Kernel/fbsdkpi/drm/src/sf_dma_buf.c`, `sf_dma_buf_file_ioctl`).
- Call sites (Mesa 147a62f6190): `agx_export_sync_file` and `agx_import_sync_file` in
  `src/asahi/lib/agx_device.c`, called from `agx_batch_submit` in
  `src/gallium/drivers/asahi/agx_batch.c` for every buffer flagged shared, once before and once
  after each submission. Every imported buffer is shared, so every frame of the KMS path makes
  both calls.
- What Mesa does instead, unmodified:
  before the submission it brackets the buffer with `DMA_BUF_IOCTL_SYNC` start/end (which the
  kernel accepts and does nothing for); after it, the failed import makes it wait for the
  submission with `drmSyncobjWait(INT64_MAX)` before returning from the flush.
- Effect: correct pictures, but every flush that draws to a scanout buffer is synchronous, and
  nothing tells another process (a future compositor's client) when a shared buffer is ready.
  The demo calls `glFinish` before each flip anyway, so its numbers do not change.
- Avoiding path: none on the Mesa side. A buffer allocated by Mesa and exported with
  `EGL_MESA_image_dma_buf_export` is flagged shared the same way.
- Needed for: implicit synchronisation between processes (ticket G4). Not needed for the demo.

### 2. Flip-complete without a blocking read (known, from the DCP work)

- `poll`/`select` on the card node do not wake. The demo blocks in `read()` for
  `DRM_EVENT_FLIP_COMPLETE`, as `m1-dcp/test/dcp-flip-test.c` does. A compositor with other
  event sources needs the wake-up (ticket G3) or a thread per display.

### 3. Explicit fences on the atomic commit (not used, so not known)

- `IN_FENCE_FD` on the plane and `OUT_FENCE_PTR` on the CRTC are not set by the demo; it uses
  `glFinish` before the commit. `EGL_ANDROID_native_fence_sync` is advertised by this Mesa, so
  a fence-driven loop (commit while the GPU still renders) is possible once the display
  driver accepts `IN_FENCE_FD` and sync files can be waited on. Whether SFDcp exposes either
  property has not been checked.

### 4. To confirm in the first boot (the unproven part)

These are ordinary calls on paths that have not run on this kernel; the demo prints a
`step:` line before each and an `AGXKMS_FAIL call="..." errno=N` line if one fails.

| Step | Kernel call underneath | Proven so far by |
|---|---|---|
| `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` | `DRM_IOCTL_PRIME_FD_TO_HANDLE` on Mesa's render-node fd for a dma-buf exported from **another open file of the same device**; `lseek(fd, 0, SEEK_END)` on the dma-buf; `fcntl(F_DUPFD_CLOEXEC)` on it; `DRM_IOCTL_ASAHI_VM_BIND` of a non-private object | nothing (the PRIME test imports into the other driver) |
| extension advertised at all | `DRM_IOCTL_GET_CAP(DRM_CAP_PRIME)` on the render node must report import and export | nothing |
| rendering into the imported buffer | `DMA_BUF_IOCTL_SYNC` on the dma-buf fd (entry 1), then a normal submission | nothing |
| `glReadPixels` of the imported buffer | `DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET` + `mmap` on Mesa's handle of the imported object (a second mapping of pages the demo has already mapped through its own fd) | nothing |
| card-node import, `ADDFB2`, commit, event | as in `dcp-flip-test --prime-from auto` | boots pr01, pr02 |
