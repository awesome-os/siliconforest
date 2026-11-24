## Goal
A system that boots like a product: optimised kernel, our logo instead of scrolling text, straight to the desktop with no typed commands.

## Decision
Mostly configuration and small patches, in this order: DEVELOPMENT kernel, then quiet boot with a logo, then multi-user boot. A full RELEASE kernel and booting without the hypervisor come later.

## What exists (read from source; `docs/roadmap/research/cpu-boot.md`)
- **Kernel variant:** `nix/m1-bringup.nix` selects DEBUG, and `xnu/CMakeLists.txt:151-154` has a hard error "T8103 bringup requires DEBUG". Four lab behaviours depend on DEBUG: trust-cache placement, a device-tree property the debug guest requires, `debug=0x14e`, and a bring-up define that disables CTRR/PPL and adds progress breadcrumbs.
- **Text on screen** comes from five settings: `-v`, the launcher forcing text video mode, `serial_video_mirror=1`, `pdtextboot` (default on), and the single-user shell.
- **Logo:** m1n1 draws a logo at start and supports a custom one as a payload (`m1n1_logo_256128`); XNU has a graphics boot mode that paints no characters.
- **Single-user:** `-s` plus a harness that waits for a shell prompt and types one command. The RAM root already contains `launchd`, a console-login job and several daemons.
- **Persistent root:** none. Internal storage is Apple NVMe behind a coprocessor; the project forbids writing to it.

## Work items
| ID | Deliverable | Acceptance on the M1 | Size |
|---|---|---|---|
| D1 | DEVELOPMENT kernel: lift the build gate, add the nix attribute, re-verify the four DEBUG dependencies, matching kext/collection variant | Banner reads `DEVELOPMENT_ARM64_T8103`; Wi-Fi join test passes; a timed CPU loop is faster than DEBUG | ~5 files, <150 lines |
| D2 | Boot profiles: `debug` (today's behaviour) and `quiet` (no `-v`, graphics video mode, no mirror, `pdtextboot=0`), serial log complete in both | Camera recording shows no text in `quiet`; serial capture still has the full log | ~100 lines |
| D3 | SiliconForest logo in m1n1 and kept on screen until the compositor's first frame (no clear, no mode change in between) | Recording: logo from m1n1 start to desktop | m1n1 build + assets |
| D4 | Multi-user boot: drop `-s`; a launchd job starts the session (compositor, `pd-netd`); needs SF-B B5 for the harness | `launchctl list` shows the jobs; desktop and Wi-Fi come up with no typed input | ~300 lines |
| D5 | Boot-time budget: timestamps at fixed points (kernel entry, IOKit matched, root mounted, launchd, first frame, Wi-Fi online) and a report | Budget table committed; three largest costs identified | small |
| D6 | RELEASE kernel (trust-cache placement rework) | Banner reads `RELEASE_ARM64_T8103` | unknown; do after D1-D5 |

## Dependencies
D2 and D3 are independent of D1. D4 needs SF-B B5. A text-free boot to the desktop needs D2 + D3 + D4.

## Interfaces this ticket owns
Boot-args and boot profiles; the session launchd job; the logo asset pipeline.

## Risks
- The DEVELOPMENT variant may expose timing bugs that DEBUG hid (or the reverse).
- Removing the single-user shell removes the harness's proof of life until B5 lands.
- Logo continuity depends on the compositor taking over the framebuffer without clearing it.

## Out of scope
Persistent root filesystem; booting without the hypervisor. Both need their own tickets after this roadmap's first pass.
