# SF-Q: SiliconForest in QEMU (development environment)

## Goal
Userland work (compositor and shell, terminal, settings menu, themes, audio server, session
scripts) is proven on this workstation in QEMU, in under a minute per run and without the one
M1 on the bench; the M1 is needed only for the final hardware proof. Second goal (owner,
11 November): a full SiliconForest OS installed in QEMU, on a disk that persists.

How to use it, what the guest contains and the coverage table:
`integrations/puredarwin/qemu-dev/README.md`. Tools: `tools/qemu_desktop.sh` (throw-away loop),
`tools/qemu_install.sh` (persistent install).

## Decision: arm64 `virt` under TCG is the primary target
| | arm64 `virt`, TCG | x86_64, KVM |
|---|---|---|
| Our binaries | run unchanged (same compositor, terminal, coreaudiod, libSystem as the M1) | all of it would have to be rebuilt for x86_64: Rust target, linker wrapper, PDGOP/IOKit/xkbcommon statics, sysaudio, the libSystem patch |
| Display | ramfb through the firmware's GOP: IOGOPFramebuffer, the PDGOP presenter the CPU renderer already uses | `-vga std` through GOP: the same |
| Input | xHCI USB keyboard/mouse/tablet: PDHID nodes, `--input pdhid` as on the M1 | the same, plus PS/2 |
| New compositor backend needed | none | none |
| Measured here, upstream image unmodified | DEBUG kernel, 4 vCPU, 4 GiB: shell prompt after 85 s (host load 7) | RELEASE kernel, 2 vCPU, 4 GiB, upstream `image-minimal`: kernel banner at 14.6 s (the firmware takes that long), root mounted 17.7 s, launchd 24.9 s, console login started 42 s; no zsh prompt on the serial line within 120 s in two runs |
| Measured here, tuned | session script running at 31-38 s, desktop with two terminals at 45-60 s (host load 12-24) | not tuned |
| Exercises arm64 code | yes | no |

x86_64 with KVM was expected to be the fast one and was not, out of the box: its userland start
is no quicker than the tuned arm64 guest's. It stays a possible later target for a CI-style
check; nothing of ours is built for it.

Where the arm64 time went and what was done: 40 s were XNU's `IOKitInitializeTime` waits
(patched in the kernel binary, as `PCIE_TIMEWAIT` does on the M1); `pmap_startup` grows with
memory (4 GiB: 10 s, 1 GiB: 2.6 s, with a DEBUG kernel); the DEBUG kernel (replaced by
upstream's RELEASE `virt` kernel); waiting for the zsh login before starting anything (the
session is a launchd job now). What is left is userland process start-up: about half a second
per `exec` under emulation, and QEMU's CPU time is about equal to wall time, so other builds on
the host stretch every number here.

## State (11 November)
| Item | State |
|---|---|
| Desktop in QEMU from one command, headless, autopilot, screenshots, pass/fail | works: `tools/qemu_desktop.sh` |
| Time from command to desktop | depends on what else the host is doing (8 cores, 16 threads, shared with other builds; QEMU is CPU-bound): 45 s at host load 12 (800x600), 60 s at load 24, 102 s at load 26 with this stream's own LLVM build on the sibling threads (1024x768), 232 s at load 30-58, one run at load 55 did not show a desktop within its 310 s limit. Image staging is 0.4-3.8 s of it. The under-60 s target is met only on a host that is not busy; not measured on an idle host |
| Window mode | works on a private X server (Xvfb): the QEMU window shows the desktop, a click and typed text sent to that window with xdotool arrive in the terminal (`runs/qemu-dev/win1/x-screen.png`). Not opened on the owner's screen |
| Resolution | 1024x768 (the firmware's largest ramfb mode) |
| Build from a source tree (`--src`) | works: 177 s for a first release build of the compositor with 6 jobs (incremental builds reuse `runs/qemu-dev/target`) |
| Persistent install: create, boot, update, reset | works with the minimal userland; state and files survive reboots (shown) |
| Full userland in the install (`--variant wayland`) | works: 6 GiB root (1.4 GiB used), boots to our desktop (70.8 s at host load 19, `--memory 2048`), files survive reboots. In it: `curl -sI https://example.com` gives `HTTP/1.1 200 OK` (CA bundle `/etc/ssl/cert.pem`), `git version 2.54.0`, `OpenSSH_10.4p1`, `Python 3.13.14`, `clang -c` compiles. Not working: clang cannot link (the image has no `ld`), and Python's `print`/`sys.stdout.write` produce no output (exit status 0; `os.write(1, ...)` does print) |
| Network in the guest | works in both modes: `virtio-net-name-interface.patch` names the interface (en0), `qemu-net-up` takes a DHCP lease from QEMU's user network and starts mdnsd. Loop: `QDDESK_NET_UP_RC=0`, `QDDESK_HTTP HTTP/1.1 200 OK` (`runs/qemu-dev/clip2`). Install: ping by name, HTTP with netcat (minimal), HTTPS with curl (full) |

## Runs
- `runs/qemu-dev/t3`, `t4`: autopilot `settle,pointer,drag,type` with the M1's current binaries
  (wm-darwin-scale-desktop-build-007, terminal-application-build-008): PASS,
  `WMAUTO drag ... result=moved`, `WMAUTO type ... result=client-redrew-after-typing`,
  `WMAUTO done result=ok`; screenshot `runs/qemu-dev/t3/shots/t004.png`.
- `runs/qemu-dev/install`: create; boot headless 40 s (desktop at 1024x768, rc 0); boot with a
  command writing `/Users/sf/note.txt` and a theme into the state directory; next boot reads both.
- `runs/qemu-dev/full1`: compositor built from `sources/chonkstep-darwin` as it was in the
  working tree at the time (another stream's uncommitted theme work included), full autopilot,
  1024x768. All phases ran. Passed: drag, type, fullscreen (vi), scrollback, resize, redrag,
  minimize, menu, settings (8 of 8 picks: 5 theme picks with `persisted=1`, 2 scale picks, the
  Wi-Fi rows reading "No Wi-Fi hardware", 1 from the keyboard), chords, menuterm, limit.
  SKIPPED as expected without the hardware: brightness, volume, touchbar, idle. **FAILED:
  clipboard** (`pasted_bytes=3`, the other terminal's line empty) **and scale**
  (`round_trip_same_grid_and_logical_size=false`), so the run's verdict is FAILED and the
  command exits 1. Neither failure has been looked into: they may come from the 1024x768 desk
  or from emulation speed, or be real. Until that is known a green baseline needs `--only`
  without those two phases.
- `runs/qemu-dev/win1`: window mode on Xvfb, by hand through xdotool (see State).

- Clipboard and scale failures of the full autopilot, looked into (`clip1`..`clip4`, `full2`):
  - **clipboard** passes whenever fewer phases run before it: `settle,type,clipboard`;
    `settle,type,fullscreen,scrollback,clipboard` with the M1 RAM root's libSystem and with the
    Homebrew stream's (issue 46); `settle,drag,type,clipboard`. It failed (3 of 10 bytes
    selected) in both full-sequence runs, with the working-tree compositor and with the M1's
    build-007, both on a host at load 26 or more. So it is not the libSystem fix, not the drag
    phase and not fullscreen/scrollback by themselves. What is left is the full sequence or host
    load; one full run on a quiet host would separate those. Not shown to be a compositor bug.
  - **scale**: at 1024x768 the terminals do not fit from 150% up (`window_panel=1024x768`, grid
    forced from 80x24 to 62x22 and 52x18), so the round trip cannot return the same grid, and
    the phase recorded 4 or 5 of its 8 steps before the session's time ran out or the step's
    wait ended. The M1 panel (2560x1600) holds 80x24 at every step. This is the small desk plus
    emulation speed; it needs a larger framebuffer to run meaningfully here.
- Full install (`runs/qemu-dev/install`, variant wayland): three command boots and one desktop
  boot; see State. A file written just before the end of the first command boot was lost because
  QEMU was stopped before `sync`; the command mode now syncs and waits for `QDINSTALL_SYNCED`.

## Found on the way
- Upstream bug: with a generic USB HID keyboard (QEMU's `usb-kbd`), `/dev/usb_hid_kbd` is never
  published (the LED-handler registration sets up the queue, after which the lazy publish is
  skipped). Fixed for this target in `pdhid-publish-keyboard.patch`; worth sending upstream.
- The PDHID nodes appear only with the first event of each kind: `qd_run.py` sends one Shift
  and one pointer step when the session starts.
- A busy host panics the guest with a ticket-spinlock timeout unless `no_lock_timeouts=1`.
- `-cpu max` hangs in the firmware; `neoverse-n2` works.
- This AAVMF build has no bochs/VGA driver, this QEMU no `virtio-gpu-pci`: ramfb is the display.
- `e2fsck` cannot reopen a filesystem at an offset inside an image after journal replay; the
  root partition is copied out and back (`qd_image.py --fsck`).

## Not proven
- Window mode on the owner's own screen (Wayland, GTK): not opened there. The Xvfb run used
  GTK's X11 backend.
- The theme bug (theme switch from the menu sometimes not applied, window controls wrong): not
  reproduced here. The autopilot's `settings` phase passed all five theme picks in `full1`, so the
  autopilot's way of picking does not show it; by hand in a window is the next thing to try
  (`tools/qemu_desktop.sh --window`), then `--only settle,settings` in a loop.
- Anything in the "no" rows of the README's coverage table.
- A desktop running as the user `sf`; sshd, configd's own DHCP, sway/netsurf and the other
  programs of the full userland were not tried.

## Open items
1. In the full userland: a linker for clang (upstream ships none in the image; the lab's
   `puredarwin-native-ld` is a host tool), and Python's lost stdout.
2. A full autopilot run on a quiet host, to settle the clipboard failure.
3. Larger framebuffer: needs a firmware whose ramfb driver has more modes (an edk2 build), or
   the virtio-gpu path (a QEMU with `virtio-gpu-pci` and a presenter that calls PDGOPPresent).
   2560x1600 would give the M1's real geometry at 200%.
4. No reboot per edit: keep the guest up and replace the compositor through a second disk or
   the network, then restart the session job. Would take the loop from about 50 s to a few.
5. Sound: a device coreaudiod can open (upstream has `RavynHDAudio`; our server talks to the
   M1's driver interface), so the volume phases stop reporting SKIPPED.
6. Multi-user: launchd already runs the system multi-user (daemons, console job), but there is
   no login, no su, and passwords are unset; the console shell and the desktop are root's. A
   desktop as `sf` needs a way to drop privileges before the compositor starts and a check that
   PDGOP, the PDHID nodes and the PTYs work for a non-root user.

## What a "full OS" in QEMU still lacks (for a user, not as work started)
- A login: no `login`/`getty`, no passwords, no `su`/`sudo`; root shell on the console.
- A package tool (the Homebrew stream), compilers and build tools, git, curl, ssh (all in
  upstream's full userland, none in the minimal one).
- Man pages, `ifconfig`, `netstat`, `ps` beyond toybox's, a pager other than `more`, an editor
  other than `vi`.
- Network configuration as a service (configd, DHCP, resolver) and time (no RTC driver on
  `virt`: the clock starts wherever the firmware leaves it).
- A clean shutdown from the desktop (the session ends by time or by closing the window; the
  disk is kept consistent by `sync` and a host-side filesystem check).
- Applications: one terminal. No file manager, browser, text editor with a window, settings
  beyond the desktop menu.
- A display larger than 1024x768, GPU rendering, sound output, clipboard or file sharing with
  the host.
- Swap, more than 512 MiB of root (the minimal image's partition), a separate `/Users` volume.
