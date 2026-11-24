## Goal
OpenGL (then Vulkan) from Omacom's Mesa on SiliconForest, used by the desktop compositor and by applications.

## Decision
- Build Omacom Mesa at commit `147a62f6190` (the version that ships with the kernel driver we port) as a new variant of PureDarwin's existing Mesa cross-build.
- **Keep Chonkstep's compositor and give it a GL renderer**; do not switch to wlroots. The in-tree wlroots is built without renderers and would need more work than adding one renderer to ours.
- Before the display-controller driver exists, "scanout" is: GPU renders off-screen, one copy into the boot framebuffer per frame. Tearing is accepted until SF-H.

## What exists (`docs/roadmap/research/display-gpu-userspace.md`, part A)
Mesa's Asahi driver needs these from the OS; PureDarwin's `/dev/dri` path (used today by the Adreno driver) provides most:

| Need | Status |
|---|---|
| Render node, driver name, platform-bus device enumeration | Provided (`PDDRMDri`, patched libdrm); untested for asahi |
| 12 driver ioctls | Transport exists; driver is SF-F |
| mmap of buffers on the device fd | Provided |
| PRIME handle/fd | Provided |
| dma-buf sync-file import/export | **Missing** (`pd_dma_buf.c` accepts only one ioctl) |
| dma-buf fd mmap | **Missing** (returns unsupported) |
| Passing dma-buf and sync fds over a Unix socket | **Missing** (file type not sendable in the kernel) |
| Sync-file `poll`/wakeups | **Partial** (`poll` returns invalid; `select` never wakes) |
| GBM | **Missing** (current `libgbm` is a 283-line veneer) |
| Mesa build | **Missing**: tree builds upstream 26.1.6 with GBM disabled; patches use exact-match replacement and will break on the fork |

## Work items
| ID | Deliverable | Acceptance | Size | Needs GPU driver? |
|---|---|---|---|---|
| G1 | `mesaAsahiArm64Build` on `147a62f6190` (host shader tools, header shims) | Libraries link; loader picks asahi from the driver name | 1-2 weeks | no |
| G2 | libdrm enumeration test for a platform render node | Test passes on an existing DRM target | 2 days | no |
| G3 | Kernel file plumbing: sendable DRM/dma-buf/sync fds, a kqueue filter, select wakeups | A sync fd crosses a socket pair; `poll` blocks then wakes | 1 week | no |
| G4 | dma-buf sync-file ioctls and mmap | Round-trip test passes | 1 week | no |
| G5 | Off-screen render with readback (`pdgltri`, already in the tree) on asahi | Pixel hash equals the software-renderer reference. A renderer string is not evidence. | days | yes (SF-F F14) |
| G6 | GL renderer in the compositor (Smithay GL on surfaceless EGL), readback to the framebuffer, behind the same renderer trait as SF-E | A nonce frame matches the CPU renderer; frame times logged and better than SF-E's | 2-3 weeks | yes |
| G7 | Zero-copy clients: linux-dmabuf protocol, client-side EGL on Wayland | A GL client runs with no shared-memory copy | 2 weeks | yes; needs G3, G4 |
| G8 | Vulkan (honeykrisp), then Zink | Headless triangle readback | 1-2 weeks | yes |

G1-G4 can start today, in parallel with SF-F.

## Dependencies
SF-F (F14) for G5 onward. SF-E for the renderer trait. SF-H for vsync and tear-free output.

## Interfaces this ticket owns
The Mesa nix variant; `fbsdkpi/pd` file-descriptor plumbing for DRM (`pd_lfile`, `pd_dma_buf`, `pd_sync_file`); the compositor's GL renderer.

## Risks
- Readback of 16 MB per frame may dominate until SF-H provides real scanout.
- Fixed-address mmaps on this path are untested.
- Feature negotiation between this Mesa and our kernel driver build must match what Linux reports on this M1.
