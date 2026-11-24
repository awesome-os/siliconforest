## Goal
A real display driver: vsync and tear-free page flips, brightness control, display sleep, later external displays.

## Decision
**Port Omacom's Linux DCP driver onto PureDarwin's LinuxKPI/DRM layer as a kext**, under its MIT licence option, and expose it through the tree's existing KMS-to-IOFramebuffer bridge. Reasons:
- The display coprocessor speaks a firmware-version-specific RPC; the Linux driver encodes it (about 9,500 lines for the internal panel). A native rewrite re-implements that for no gain.
- PureDarwin already bridges KMS drivers to its framebuffer interface (`fbsdkpi/drm/include/pd_drm_fb.h`: modes, set-mode, flip with vblank time, scanout-buffer export; `pd_amdgpu_fb.c` is the model).
- 32 of the driver's 43 files are `GPL-2.0-only OR MIT`.

Fallback, only if the port stalls: a minimal flip service on m1n1's small iBoot-endpoint client (`src/dcp_iboot.c`, 341 lines, MIT). It has no brightness and no vblank event.

## What exists (`docs/roadmap/research/display-gpu-userspace.md`, part B)
- Today the panel shows the framebuffer iBoot set up, through `IOGOPFramebuffer`. That cannot change mode, flip, report vblank, set brightness, sleep or drive external displays.
- The DCP is an RTKit coprocessor with its own DARTs; one of them (`disp0`) is left locked by iBoot. m1n1 leaves the internal panel's firmware as iBoot started it.
- Linux driver: `drivers/gpu/drm/apple`, 15,256 lines in 43 files. Internal-panel core about 9,500 lines; external display adds about 2,400 plus Type-C code.
- Missing in our LinuxKPI: `component.h`, `of_graph.h`, RTKit header, `phy.h`, `reset.h`, DMA GEM/FB helpers.
- **Brightness is not standalone**: Linux applies it through a frame swap, so "backlight only" is not a valid first step.

## Work items
| ID | Deliverable | Acceptance on the M1 | Size |
|---|---|---|---|
| H1 | RTKit for LinuxKPI (`apple_rtkit_*`) on SF-A's RTKit; wake the DCP read-only | Endpoint and service announcements logged; panel still lit; console returns | 2 weeks |
| H2 | DART for the `dcp` stream and the locked `disp0` stream behind LinuxKPI's iommu API (on SF-A A5) | Map/unmap test; existing framebuffer mapping preserved | 2 weeks |
| H3 | LinuxKPI gaps: component, of_graph, DMA helpers, reset, power domain, clock | Driver core compiles and links | 1-2 weeks |
| H4 | Takeover and one full-screen swap | A nonce frame is visible; swap-complete count about 60 per second | 3-4 weeks |
| H5 | Brightness service and keys | Brightness steps visible on the panel | 1 week after H4 |
| H6 | Bridge glue: flip and scanout-buffer export to the compositor | Compositor page-flips with no tearing | 1-2 weeks |
| H7 | Display power: off/on without reboot (needed by SF-K) | Panel sleeps and wakes | 1-2 weeks |
| H8 | External display over USB-C (second coprocessor, DisplayPort endpoint) | A monitor lights up | 6+ weeks; shared with SF-L stage 3 |

## Dependencies
SF-A (A3 RTKit, A5 DART, A2 PMGR). Independent of the GPU until zero-copy scanout (H6 with SF-G G7).

## Interfaces this ticket owns
The DCP kext; LinuxKPI additions for component/of_graph/DMA helpers (coordinate with SF-F, which also extends LinuxKPI: one owner per header, listed in the index).

## Risks
- Taking over from iBoot can blank the panel with no fallback console: keep serial, test under the hypervisor first.
- Firmware version: the driver has templates for two firmware versions; which one this M1's display firmware matches is unverified.
- The locked DART limits remapping.
- `/dev/dri` has no select wakeups today, so vblank events need the SF-G G3 fix or polling.
