# Wayland client submitting GPU buffers through linux-dmabuf

`darwin-wayland-dmabuf-client --socket ABSOLUTE --seconds N` allocates three linear XRGB8888
640x400 buffers on the asahi render node (`DRM_IOCTL_ASAHI_GEM_CREATE`, as the compositor does
for its scanout buffers), exports each with `drmPrimeHandleToFD`, and gives the fds to the
compositor through `zwp_linux_dmabuf_v1` (`create_params`, `add`, `create_immed`). It then
animates: on every frame callback it paints a buffer the compositor has released (through the
render node's CPU mapping; no GL) and commits it with full damage. It never touches a buffer
between commit and `wl_buffer.release`, because the compositor samples the client's memory.

Every frame has a 64x64 block of colour 33,99,cc in the middle. The compositor's `WMEXT` line
reads that pixel back from its own GL frame.

libdrm is opened at run time (`/usr/local/lib/mesa-asahi/libdrm.2.dylib`, override
`DMACLIENT_LIBDRM`); nothing but libSystem is linked.

Lines: `DMACLIENT buffers=...`, `DMACLIENT advertised XR24:0x0 AR24:0x0`,
`DMACLIENT done callbacks= commits= releases= stalls= seconds= fps= result=ok|FAILED`.
Exit 0 needs at least one frame callback and one release. A callback is compositor progress,
not proof of what the panel shows.

Build (host check, then the guest binary, as the other clients):

    cargo build --offline --locked --release --target-dir target-host
    CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER=$PWD/../darwin-linker.py \
      cargo build --offline --locked --release --target aarch64-apple-darwin --target-dir target

Boot: `WM_GL_STEP=dmabuf tools/wm_gl_boot.sh <name>` (guest script `../darwin-wm-gl-dmabuf.in`).
