# Compositor on the GPU with page flips: benchmark on the M1

Boots `wg01` (backend probe) and `wb01` (benchmark), 10 November 2026. One E core under the hypervisor, 2560x1600.
Binary: `chonkstep-darwin-desktop` from the local `wm-darwin` branch (commits 54d0007, e9b84e8; not pushed), carried here as
`integrations/puredarwin/chonkstep-darwin/chonkstep-source.patch.gz`. Collection: PDAgx + PDDcp (no Wi-Fi).
Commands: `tools/wm_gl_boot.sh <name>` (probe), `WM_GL_STEP=bench tools/wm_gl_boot.sh <name>`.
Raw lines: `runs/m1-desktop-010/pcie-enum-attempt-wb01/guest/console.bin` (`WMBENCH`).

Median time per frame, same boot:

| Scenario | CPU damage renderer, boot framebuffer | GL renderer, page flip |
|---|---|---|
| Full repaint | 37.08 ms | 1.64 ms |
| Pointer motion | 0.038 ms | 1.63 ms |
| 800x600 client, whole buffer | 9.71 ms | 5.00 ms |
| 800x600 client, 20 damaged rows | 0.308 ms | 1.81 ms |
| Continuous animation, 5 s | not comparable (no refresh clock) | 60.0 fps, 300 flips |
| Idle | renders nothing | renders nothing |

The GL figures exclude the wait for the flip; with it every presented frame takes one refresh (16.7 ms).
The GL renderer redraws the whole scene each dirty frame (about 1.4 ms on the GPU), so small updates cost more
than the CPU renderer's damage path but always fit in one refresh and are tear-free.

Pixel check, GL against the CPU reference for a full nonce frame: 8,296 of 4,096,000 pixels differ, each by 1 in one
channel (translucent blending rounds differently on this GPU than on the host GPU, where the result was identical).

Backend probe (`wg01`), 5 s of clears: poll 58.5 fps with 2 missed refreshes, blocking read 59.5 with 0, reader
thread 59.4 with 1.

Not done: an interactive session on the GL renderer has not booted (staging ran out of RAM-root space); nobody has
watched the panel; clients are still shared-memory.
