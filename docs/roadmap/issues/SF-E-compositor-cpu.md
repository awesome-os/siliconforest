## Goal
Make the existing CPU-rendered desktop responsive before any GPU work lands.

## Why this is separate and early
The research found the desktop's slowness has a specific, fixable cause in our own compositor, independent of the GPU and the hypervisor. Fixing it needs no kernel work and no hardware bring-up.

## What exists (read from source; `docs/roadmap/research/display-gpu-userspace.md`, part A1)
- The compositor is `chonkstep-darwin` (crate `wm-darwin`, 1,603 lines), using Smithay's Wayland front end with tiny-skia. Clients use shared-memory buffers only.
- Output is the 2560x1600 boot framebuffer through a write-combined mapping. No vsync, no scaling.
- Every dirty frame is full-screen, with no damage tracking:
  1. a new 16 MB pixmap is allocated, filled and composited (`scene.rs:152`);
  2. every pixel is converted into a second new 16 MB buffer (`pixels.rs:118`);
  3. 4.1 million individual volatile writes copy it to the framebuffer (`presenter.rs:106`).
- Every pointer event marks the scene dirty (`input.rs:100`), so moving the mouse does all of that each time.
- Nothing was ever measured: `reports/sprint-010/passive-child-lag-observation-notes.json` says so.

## Decision
Keep Chonkstep's compositor. Add damage tracking, buffer reuse and row-wise copies. Measure first so the gain is a number.

## Work items
| ID | Deliverable | Acceptance on the M1 | Size |
|---|---|---|---|
| E1 | Frame instrumentation: per-frame time split into composite, convert, present; bytes written; frames per second; exported on demand | Baseline numbers committed for idle, pointer motion and a terminal scrolling | 1 day |
| E2 | Persistent buffers (no per-frame 16 MB allocations) | Allocation count per frame is zero in steady state | 1 day |
| E3 | Damage tracking: repaint and present only changed rectangles; pointer motion damages only the old and new cursor rectangles | Pointer motion writes kilobytes, not 16 MB; frame time drops accordingly | 2-3 days |
| E4 | Row-wise conversion and block copies to the framebuffer (`copy_nonoverlapping` per row; consider composing directly in the framebuffer's pixel format) | Full-screen repaint time at least halves versus E1 baseline | 1-2 days |
| E5 | Client buffer commits copied by damage region instead of whole surfaces | Terminal scrolling cost scales with changed area | 1 day |

## Acceptance for the ticket
Numbers before and after for the three E1 scenarios, taken on the M1 in the same boot configuration, committed as a report.

## Dependencies
None. Benefits multiply with SF-C (CPU speed) and SF-D D1 (non-DEBUG kernel).

## Interfaces this ticket owns
The renderer and presenter inside `chonkstep-darwin/crates/wm-darwin`. SF-G later adds a GL renderer beside this one; keep the renderer behind one trait so both can exist.

## Out of scope
GPU rendering, vsync, zero-copy clients (SF-G, SF-H).
