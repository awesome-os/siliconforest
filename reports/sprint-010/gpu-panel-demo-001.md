# GPU demo on the built-in panel, with timings (Darwin vs Linux on the same M1)

Boot `pd01` (10 November 2026): `AGX_TEMPLATE=.../m1-mesa-asahi/supervisor/darwin-agx-panel-demo.in
tools/agx_boot.sh pd01`, Wi-Fi + GPU collection. `agx-panel-demo` renders three scenes at
2560x1600 on the GPU (GLES 3, off-screen target), reads the frame back and copies it to the panel
through the existing framebuffer mapping. 21 runs of 30 frames, all completed, exit 0, 17.8 s.
Nobody was watching the panel; what is proven is that the frames were rendered, read back and
written to the panel mapping, not what the eye saw.

Median time per frame (ms). Darwin runs on one E core under the hypervisor; Linux uses all cores.

| Scene | GPU render only | + readback | + present to panel (best path) |
|---|---|---|---|
| Plasma shader, Darwin | 1.9 | 10.3 | 26.8 (37 fps) |
| Plasma shader, Linux | 0.9 | 2.1 | 4.2 (to heap memory, no panel) |
| 10,000 blended quads, Darwin | 3.1 | 14.7 | 31.2 (32 fps) |
| 10,000 blended quads, Linux | 2.1 | 4.9 | 9.8 |
| Lit mesh with depth, Darwin | 1.8 | 10.1 | 26.7 (37 fps) |
| Lit mesh with depth, Linux | 0.9 | 2.3 | 3.8 |

Reading: the GPU itself draws a full 2560x1600 frame in 2-3 ms under Darwin. The cost is getting
16 MB to the screen through the CPU: about 8 ms to read back and 16-24 ms to write the panel
mapping on one E core. The old CPU compositor needed 290 ms per full frame, the damage-tracked
one 37 ms. Faster presentation needs the display controller path (scanout of GPU buffers,
ticket SF-H), more CPU cores (SF-C), or both.

Correctness (boot `pd02`, with per-block fingerprints from Linux staged into the guest): five of
the six full-resolution frames are identical to Linux in all 16,000 blocks. The sixth (mesh
scene, 10-bit target) differs in exactly 2 of 4,096,000 pixels, each by one step of 1023 in one
colour channel (pixels 1234,518 and 2156,203). The difference is deterministic (5 of 5 repeats
equal), the depth buffers are identical to Linux's, freshly allocated GPU buffers are zeroed
(two rounds of 32 MiB checked), and waiting before readback or using a separate depth buffer
changes nothing. Cause not established; a last-bit difference in CPU-side maths (the scene's
matrices come from libm, which differs between the two systems) would produce exactly this, but
that is a guess. The 8-bit target of the same scene is bit-identical.
