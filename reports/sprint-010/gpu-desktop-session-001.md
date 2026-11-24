# GPU desktop session on the M1: compositor on GL and page flips, driven by synthetic input

Boot `ws05` (10 November 2026). Collection: Wi-Fi + PDAgx + PDDcp on the patched kernel
(`integrations/puredarwin/m1-dcp/build-kc.nix --arg agx true --arg wifi true --arg slim true`).
Command: `WM_GL_STEP=session tools/wm_gl_boot.sh <new-name>`. Compositor from the local `wm-darwin` branch
(commit 2c140fe, not pushed; carried as `chonkstep-source.patch.gz`), installed as the default desktop binary.
Raw lines: `runs/m1-desktop-010/pcie-enum-attempt-ws05/guest/console.bin` (`WMSTAT`, `WMAUTO`).

The desktop binary chose `renderer=gl output=kms` by itself, started two terminals, and then fed itself synthetic
input through the same entry native HID records take (`CHONKSTEP_DARWIN_AUTOPILOT=1`; compositor-generated events,
not physical input):

| Phase | Result |
|---|---|
| Pointer sweep, 8 s | 479 frames, 59.8 fps, 0 missed refreshes, composite 1.63 ms average |
| Window drag, 4 s | window moved 240,117 for a pointer move of 240,120; 52.9 fps, 1 missed refresh |
| Typing `echo wmgl` + Enter into the terminal | 20 key events; the terminal's own buffer changed in 924 pixels |
| Screenshot check after each phase | GL frame read back equals the CPU renderer's frame of the same scene: 0 of 4,096,000 pixels differ, four times |

First frame: 148 ms composite and 174 ms present (texture uploads, first pipeline compiles, the blocking mode set).
Steady state: about 1.6 ms composite and 0.23 ms for the commit call. One composite of 43 ms and one commit of 24 ms
occurred; causes not examined.

The `CODE SIGNING: cs_invalid_page … allowing` line appears for the compositor as it does for `launchd` and
`launchctl` on every boot; the address lies in the page before the run-time-loaded libdrm mapping.

Not established: nobody watched the panel and no physical key or trackpad input was used in this boot; Wi-Fi was
not joined (firmware not staged in this test); applications still render on the CPU; sessions are capped at 180 s
by the bench.

## GPU-rendered application in the GPU desktop (boot `wl04`)

Command: `WM_GL_STEP=wlgpu tools/wm_gl_boot.sh <new-name>` (client bundle from `m1-mesa-asahi/agx-wl-demo.nix`).
Raw lines: `runs/m1-desktop-010/pcie-enum-attempt-wl04/guest/console.bin` (`AGXWL`, `WMDMABUF`).

`agx-wl-demo` is an ordinary Wayland client: xdg-shell window, EGL window surface from Mesa's Wayland platform
(Darwin build with patches 0006-0010), GLES on the asahi driver. It renders into buffers Mesa allocates on the GPU
and sends them to the compositor as dma-bufs over `zwp_linux_dmabuf_v1` version 3; the compositor imports each
once as a texture, with no copy, and presents by page flip.

| Scene (800x600 window) | Frames in 4 s | fps | Render ms (median) | Longest frame interval |
|---|---|---|---|---|
| Plasma | 219 | 54.6 | 1.18 | 137 ms |
| 10,000 quads | 234 | 58.4 | 2.12 | 50 ms |
| Lit mesh | 231 | 57.7 | 1.84 | 50 ms |

- Client: `GL_RENDERER="Apple M1 (G13G B1)"`, 2 dma-buf buffers created, 0 shared-memory pools, 684 attaches,
  682 releases, exit status 0. Its window-versus-offscreen check passes (at most 507 pixels off by 1).
- Compositor: 2 imports, 0 refused, 684 commits, 0 fence timeouts. The window's region of the composited frame is
  not flat; it could not be compared with the client's buffer because that boot's kernel could not map a dma-buf
  descriptor (fixed since for display-driver buffers, boot `mm01`; GPU-driver buffers still fail).
- Mesa reported `DMA_BUF_IOCTL_IMPORT_SYNC_FILE failed on export … errno 14` twice and fell back to waiting for
  the writer. The same errno appears for every dma-buf ioctl on a GPU-driver dma-buf in boot `mm01`. Open kernel bug.
- Three Mesa issues found and fixed on the way (Wayland build only): the render node is found without protocol
  version 4; `mincore` cannot be used as a pointer test on XNU; a fence that cannot be attached at export is not fatal.

Not established: nobody watched the panel; frame-interval spikes of 50-137 ms at scene starts are unexplained.
