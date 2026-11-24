This is the entry point for anyone picking up SiliconForest work. The status section below was written on 10 November 2026 and is kept as written; the current state is in the repository's README. Each linked ticket is self-contained: it states the architectural decision already made, why, what exists, the references to port from, work items with on-hardware acceptance tests, and what it depends on. Pick a ticket, read it and its research file, and work its items in order.

SiliconForest is a fork of PureDarwin (Darwin/XNU) for Apple Silicon Macs. First target: 13" M1 MacBook Pro (J293, T8103).

## Where things stand (proven on the M1)
- XNU boots under the m1n1 hypervisor to a CPU-rendered Wayland desktop with the internal keyboard and trackpad.
- PCIe and Wi-Fi work end to end with no outside help: radio power, link at 5 GT/s, BCM4378 firmware, WPA2, DHCP, DNS, auto-rejoin.
- Rust runs in a kext, and the Rust-for-Linux `kernel` crate's core runs on our Linux compatibility layer.
- Not yet: more than one CPU, full CPU speed, GPU, real display driver, sound, Bluetooth, sleep, USB-C, an optimised quiet boot.

## Tickets
| Code | Ticket | Decision in one line | Depends on |
|---|---|---|---|
| SF-A | Shared Apple platform services | Extract PMGR, RTKit/SMC, DART, GPIO, I2C into one kext with fixed interfaces | nothing |
| SF-B | Test bench and harness | Queue in front of the one M1; long sessions; multi-CPU and no-typing boots | nothing |
| SF-C | Full CPU speed and all eight cores | m1n1 frequency call now; native core start ported from m1n1 | SF-B (B4) |
| SF-D | Optimised kernel, quiet boot with logo, multi-user boot | DEVELOPMENT kernel, boot profiles, m1n1 logo payload, session job | SF-B (B5) |
| SF-E | Responsive desktop on the CPU | Damage tracking and buffer reuse in our own compositor | nothing |
| SF-F | GPU kernel driver | The Asahi Linux Rust driver (as carried in Omacom's `linux-aurora`) on the Rust-for-Linux crate over our Linux compatibility layer | SF-A |
| SF-G | GPU userspace | Mesa's `asahi` driver (Omacom's branch, `147a62f6190`); GL renderer in our compositor | SF-F, SF-E |
| SF-H | Display controller | Port the Asahi Linux DCP driver (from Omacom's tree; MIT option) onto the compatibility layer's DRM | SF-A |
| SF-I | Sound | Native IOKit drivers on IOAudioFamily; headphones first; speakers only behind a hard safety cap | SF-A |
| SF-J | Bluetooth | Port the Linux controller driver (MIT option); host stack in userspace, to be chosen | SF-A (A8), SF-M |
| SF-K | Sleep and power | Native glue around XNU's power management; idle sleep first, real suspend second | SF-A, SF-H, SF-C |
| SF-L | USB-C, docks, Thunderbolt | Four stages; native kexts for USB and power delivery, Linux port for Thunderbolt | SF-A, SF-H |
| SF-M | Wi-Fi hardening | Fix the firmware stall, soak, multi-CPU audit, WPA3, tree patches | SF-B, SF-C |

## What can run in parallel right now
- **No dependencies, start today:** SF-A, SF-B, SF-E, SF-C item C2, SF-D items D1-D3, SF-G items G1-G4, SF-F (in progress), SF-M items M1/M2/M6.
- **After SF-A's interfaces (item A1) are agreed:** SF-I, SF-J, SF-H, SF-L stage 1, SF-K phase 1 can all proceed against the headers while A2-A8 are implemented.
- **Suggested order for user-visible wins:** SF-E (days; in progress), SF-D D1 (non-DEBUG kernel), SF-C SMP so performance cores are used, then SF-A, then the feature tickets. The frequency step alone (C1, done) measured no gain.

## The one shared resource: the bench
There is one M1 and one controller. Until SF-B's queue exists, coordinate boots by hand. Rules:
1. One boot at a time. Never leave the M1 outside Linux; every session ends with the M1 back in verified Linux.
2. "Proven" means console lines from a boot on the M1. A build is "compile evidence" and must be reported as such.
3. No writes to the M1's internal storage, no DFU, no firmware flashing, no boot-policy changes.
4. Firmware, credentials, device addresses and captures never go into git (`private/`, `runs/`, `artifacts/` are ignored).
5. The loop: `tools/pcie_iterate.sh` builds, assembles, delivers, boots once, returns the M1 to Linux and fetches the console (about five minutes).

## Conventions that prevent churn
- **Each ticket owns directories and interfaces**, listed in its "Interfaces this ticket owns" section. Change someone else's by asking in their ticket, not by editing.
- **Shared platform code lives only in SF-A.** Do not copy PMGR, RTKit, SMC, DART, GPIO or I2C code into a feature kext.
- **The Linux compatibility layer (`src/Kernel/fbsdkpi`) has two tickets extending it** (SF-F and SF-H). Header ownership: SF-F owns everything the Rust crate needs (scheduler, GPU VM, sync, allocation, device model); SF-H owns component, of_graph, DMA GEM/FB helpers, backlight. RTKit-for-LinuxKPI is SF-H H1 and SF-F reuses it.
- **One Rust toolchain** for the kernel: `integrations/puredarwin/m1-rust-kext/rust/toolchain.nix` (SF-F).
- **Code location:** new work goes under `integrations/puredarwin/<area>/` with a nix recipe; `tools/pcie_export_patch.sh` exports it as patches against the PureDarwin tree. Edit the lab sources, then regenerate.
- **Logging:** the serial console is the debugger. One line per stage with a fixed prefix; nothing per packet or per frame. Log before the first access to a new register block.
- **Mapping rule learned the hard way:** on-SoC registers are mapped non-posted (`kIOMapInhibitCache`); PCI BARs are mapped posted-write (`kIOMapPostedWrite`). The wrong one halts the guest with an SError.

## How we choose what to port from
Decided per ticket, with reasons there. The pattern so far:
- **Chip drivers with a permissive or dual licence and little framework** (Wi-Fi, Bluetooth controller, Type-C PHY): port directly.
- **Drivers buried in a Linux framework we lack** (audio on ASoC, Type-C and power-delivery frameworks): write native IOKit drivers using Linux, OpenBSD and m1n1 as references.
- **Drivers whose value is firmware-specific protocol knowledge** (GPU, display controller): keep the original code and bring the framework to it through the compatibility layer.
- **Bring-up sequences** (PCIe, power domains, CPU start, tunables): port from m1n1 (MIT).
- GPL-2.0 kexts ported from Linux are acceptable; prefer ISC, MIT or BSD when the code is equally good, and keep SPDX lines and copyright notices in every ported file.
- Reference pairing proven on this M1: `linux-aurora 7.2.11.aurora6`, `mesa-aurora 147a62f6190` (omacom/mesa branch `integration/aurora`), `m1n1-aurora 1.6.1`.

## Research inputs
`docs/roadmap/research/` holds the source analysis behind these tickets (CPU and boot; sound and Bluetooth; sleep and Thunderbolt; display and GPU userspace) and `docs/GPU_PLAN_NUMBERS.md` holds the GPU measurements. They are source readings, with inferences marked; nothing in them was run on hardware.

## Upstream and credit
SiliconForest is a fork of [PureDarwin](https://github.com/PureDarwin/PureDarwin) (BSD-2-Clause for its own files). We build on one revision of its branch `upstream-xnu-12377`, track that branch, and adopt later upstream commits as patches that keep their upstream commit ids, authorship and licence. Where a ticket lists upstream commits, they are PureDarwin's work, not ours. Fixes we make to upstream's code are meant to go back to PureDarwin.

The hardware drivers are ports of the [Asahi Linux](https://asahilinux.org) project's Linux drivers (GPL-2.0; some files GPL-2.0 OR MIT), kept under those licences with their notices. We read them from Omacom's `linux-aurora` integration tree, so where a ticket says "Omacom's driver" or "Omacom Mesa" it means the Asahi driver, or Mesa's `asahi` driver, as carried in that tree. Bring-up sequences come from m1n1 (MIT), the Wi-Fi driver from OpenBSD's `bwfm` (ISC).

Frameworks adopted from the [Finch](https://github.com/calebtrueman/finch) project (MIT OR Apache-2.0) keep Finch's notices; each ticket that uses them carries a "Taken from Finch" table saying exactly what. The desktop is Chonkstep (GPL-3.0-only).

The full record, with revisions and licences, is the `NOTICE` file; licence texts are in `LICENSES/`. If a ticket credits something wrongly, that is a bug: please say so.

## Reading these tickets
They were written as working notes, so some shorthand needs a key:
- **The bench**: the one test machine (a 13-inch M1 MacBook Pro) and a second Mac, called "the M5" or "the controller", that starts and resets it over USB-C. Short names such as `sm03`, `dr21` or `nb07` are single boots on the bench; the records behind them are local files.
- **Streams**: parallel lines of work, several of them carried out by AI coding agents under the owner's direction. Names such as TERM, DESK, FX, FXQA, CODEX-FX, CODEX-TB, EXT4 or "the coordinator" are streams, not people. "The owner" is the maintainer, who owns the machine.
- **The pin, the tip**: two revisions of upstream PureDarwin (`e7aa00a6`, `c713d8fb`).
- **Paths**: `runs/`, `private/`, `artifacts/` and `sources/` are local working directories that are not in the repository. Tickets written before 17 November 2026 say `integrations/puredarwin/…` and `PD…`/`pd…`; these are now `integrations/siliconforest/…` and `SF…`/`sf…` (`docs/design/siliconforest-naming.md`).
- **"Proven on the M1"** means observed in a boot on that machine; "compile evidence" means it only built.
