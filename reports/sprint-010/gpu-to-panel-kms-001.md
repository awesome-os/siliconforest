# GPU-rendered frames on the panel by page flip, 60 fps, no copy

Boot `km01` (10 November 2026). Collection: PDAgx + PDDcp (no Wi-Fi), `integrations/puredarwin/m1-dcp/build-kc.nix --arg agx true`.
Boot-args `pdgpudt=2 pdgpudt_stringprops=1 pddispdt=2 pddcp=2 pdagx=1`. One command:
`DCP_TEST=kms tools/dcp_boot.sh <new-name> <collection>`. Raw lines:
`runs/m1-desktop-010/pcie-enum-attempt-km01/guest/console.bin` (`AGXKMS`, `DL46`, `PDDcp: prime`).

Path, all zero-copy: a 16 MB linear buffer is created on the GPU render node, exported as a dma-buf, imported into
GL as a render target (`EGL_EXT_image_dma_buf_import`, linear modifier, AR24) and into the display driver as a
framebuffer (XRGB8888). Mesa's asahi driver renders into it on the GPU; an atomic page flip hands it to the display
coprocessor; the flip-complete event is the frame clock. Three buffers rotate. No Mesa change was needed.

Results at 2560x1600, 5 s per scene, one E core under the hypervisor:

| Scene | Frames | fps | Render ms (median / p99) | Wait for flip ms (median) | Missed refreshes | Flip interval ms (min / max) |
|---|---|---|---|---|---|---|
| Plasma shader | 300 | 59.8 | 2.25 / 2.60 | 14.20 | 0 | 16.50 / 16.83 |
| 10,000 blended quads | 301 | 60.2 | 6.57 / 7.28 | 9.87 | 1 | 7.06 / 28.16 |
| Lit mesh with depth | 301 | 60.3 | 2.20 / 2.41 | 14.24 | 0 | 6.20 / 16.80 |

For comparison, the same scenes through readback and CPU copy to the boot framebuffer reached 37, 32 and 37 fps
(`gpu-panel-demo-001.md`).

Correctness, without anyone watching: for a fixed frame of each scene, the scanned-out buffer read with
`glReadPixels`, the same memory read through a CPU mapping, and the same frame rendered into an ordinary GL target
are identical in all 16,000 blocks. Every imported buffer's 1,000 pages translate correctly through the display DART.
Exit status 0, GL error 0.

Not established:
- Nobody watched the panel.
- The Linux KMS reference fingerprint was not staged, so these frames are compared with our own render path, which
  earlier matched Linux in five of six frames.
- Short intervals (6-7 ms) appear at scene starts; one missed refresh in the quads scene. Causes not examined.
- dma-buf sync-file ioctls are missing (`ENOTTY`); Mesa falls back to a blocking wait. poll/select on the DRM fd
  are missing; the demo blocks in `read()`.
- Wi-Fi is not in this collection (about 700 KiB short). The text console is not restored after the demo.
