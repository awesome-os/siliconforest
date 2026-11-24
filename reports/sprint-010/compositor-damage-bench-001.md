# Compositor: damage-tracked renderer, measured on the M1

Boot `wm01` (10 November 2026), one E core under the m1n1 hypervisor, 2560x1600 panel.
Binary: `chonkstep-darwin-desktop` from the local `wm-darwin` branch of
`sources/chonkstep-darwin` (commits ee2ce99..4f03f06, not pushed), staged as
`/usr/bin/chonkstep-wm-bench`; guest script `integrations/puredarwin/chonkstep-darwin/darwin-wm-bench.in`.
All four runs exited 0. Median time per frame:

| Scenario | Original renderer | Damage renderer | Factor |
|---|---|---|---|
| Full repaint | 290 ms | 37 ms | 8x |
| Pointer motion | 290 ms | 0.038 ms | 7,600x |
| 800x600 client, whole buffer | 309 ms | 9.7 ms | 32x |
| 800x600 client, 20 damaged rows | 308 ms | 0.30 ms | 1,000x |

Where the original 290 ms went: 115 ms compositing, 107 ms pixel conversion, 66 ms writing
16.4 MB to the panel mapping, plus 32.8 MB of allocation per frame. The original renderer
repainted everything for every event, so the desktop ran at about 3 frames per second.

The damage renderer's full repaint is 4.7 ms composite, 8.6 ms convert, 24 ms present; the
panel mapping costs about 15 ms more per full frame than heap memory (off-screen run: 22 ms).

Not yet done: the base RAM root still carries the old desktop binary; a real interactive
session with the new binary has not been run; no vblank, so small updates can tear.
Raw lines: `runs/m1-desktop-010/pcie-enum-attempt-wm01/guest/console.bin` (`WMBENCH`).
