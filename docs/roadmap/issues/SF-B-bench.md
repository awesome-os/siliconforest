## Goal
Make the single test machine usable by several agents without collisions, and able to run the tests the roadmap needs (long sessions, more than one CPU, no typed input).

## Why it matters
There is one M1 and one M5 controller. Every hardware claim in this project comes from a boot on that M1. Today one agent drives it by convention. Several agents working in parallel need a queue, and three roadmap tickets need harness changes before they can be tested at all.

## What exists
- `tools/pcie_iterate.sh`: build kexts and kernel collection, assemble a boot packet, deliver to the M5, boot once, return the M1 to Linux, fetch the console. About five minutes a round.
- `tools/pcie_assemble.py`, the launcher tree under `runs/m1-desktop-010/pcie-m2-host-001/lab` (untracked; its delta is `integrations/puredarwin/m1-apcie-host/launcher/host_progress.patch`).
- A lock file on the M5 that stops two boots overlapping, and an "incomplete cycle" latch.
- Limits: runtime capped at 180 s; single-user boot with a scripted serial challenge; an inspector that assumes one paused CPU; the hypervisor owns one USB-C port.

## Decision
Keep the M5-driven loop and the "always return to Linux" rule. Add a queue in front of it rather than a new system.

## Work items
| ID | Deliverable | Acceptance | Size |
|---|---|---|---|
| B1 | Track the launcher tree in git (today only a patch is tracked) and make `pcie_iterate.sh` take the test template and kext set as arguments instead of environment variables | A fresh clone can build and boot the Wi-Fi join test with one documented command | 2-3 days |
| B2 | Bench queue: a script on the builder that serialises boot requests from several sessions (lock, FIFO, per-request log directory, who holds the bench) and refuses to start if the M1 is not in verified Linux | Two sessions submit at once; boots run one after the other; each gets its own console capture | ~300 lines |
| B3 | Long-session mode: runtime beyond 180 s (configurable to hours) with periodic console snapshots and a watchdog that still returns the M1 to Linux | A 30-minute session completes and is recovered | ~200 lines in launcher/controller |
| B4 | Multi-CPU-safe launcher: remove the single-CPU assumptions in the stack/framebuffer inspector and the `smp=False` settings, behind a flag (needed by SF-C) | Existing one-CPU tests unchanged; flag off by default | ~150 lines |
| B5 | Unattended-boot proof without typed input: a daemon in the guest emits the session nonce on serial (needed by SF-D multi-user boot) | A boot with no `-s` is recognised as successful | ~150 lines |
| B6 | Result summary per boot: one JSON with kext set, git commit, test template, pass/fail markers found | Every boot directory has it; the queue prints a one-line verdict | small |
| B7 | CI on GitHub for what needs no hardware: nix builds of all kexts and the kernel collection, patch export check | A pull request shows build status | 2-3 days |

## Rules every roadmap ticket inherits (also in the index)
1. One boot at a time, through the queue. Never leave the M1 outside Linux.
2. A claim is "proven" only with the console lines from a boot on the M1; builds are "compile evidence".
3. No writes to the M1's internal storage, no DFU, no firmware flashing.
4. Firmware, credentials, MAC addresses and captures stay out of git (`private/`, `runs/`, `artifacts/` are ignored).

## Dependencies
None. Blocks: SF-C (B4), SF-D (B5), the Wi-Fi soak (B3).

## Risks
The launcher's hash-pinned pieces from the earlier process make it brittle; B1 should drop pins that no longer protect anything.
