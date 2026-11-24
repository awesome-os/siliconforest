## Goal
Boot to a usable desktop as fast as possible, with numbers (GitHub issue #27). Two kinds of time are kept apart: **harness** time exists only because the M1 is booted over USB from a second Mac; **Darwin** time would exist on an installed system.

## Method
`integrations/puredarwin/m1-boot-speed/tools/bs_timeline.py <run directory>` splits one boot into segments on one clock: this host's `times.txt`, the controller's cycle-directory file times, the launcher's `BQ_T` lines and the arrival time of each console chunk (0.1 s resolution). It works on any `tools/pcie_iterate.sh` boot of the desktop. `tools/bs_boot.sh <bsNN>` boots the default desktop unattended for 45 s and prints the table.

## Before (boots `ht01`, `df01`; `tb11`, `ds06`, `ds07` agree within 0.3 s)
| Segment | Seconds | Kind |
|---|---|---|
| Packet assembled, delivered to the controller | 4.3 + 5.4 | harness |
| Inputs verified, Linux baseline, reboot requested | 1.5 | harness |
| Linux shuts down, m1n1 appears on USB | 15.0 | harness |
| m1n1 bootstrap stage, new USB enumeration, worker start | 6.5 | harness |
| Boot picture painted (2.8) and read back (1.9) | 4.7 | harness |
| Hypervisor init, kernel collection upload | 3.3 | harness |
| RAM root upload: gzip 2.5, USB 13.4, decompress 10.6 | 28.1 | harness |
| XNU start to display driver registered | 1.2 | Darwin |
| `IOKitInitializeTime`: 30 s RTC wait + 27 s registry wait | 59.2 | Darwin |
| Root mount to launchd | 0.3 | Darwin |
| launchctl `IOKitWaitQuiet` before the shell | 5.5 | Darwin |
| Prompt, typed command, script start | 0.3 | harness |
| Guest script before the compositor | 0.4 | Darwin |
| Compositor start to first flip | 1.44 | Darwin |
| First frame to both terminals ready | 0.15 | Darwin |

First frame 126.1 s after the cycle starts (68.5 s after the hypervisor starts the guest); terminals 0.15 s later; Wi-Fi online 28 s after the script starts. Not in the table: the wrapper's own `nix-build` lookups before the packet is assembled, 20 to 70 s each, three or four per boot.

## Causes found
- **62 s, one cause.** `StartIOKitMatching()` adds a busy count to the registry root until the IOKit daemon reports ready; only kernelmanagerd's Daemon Ready request releases it and nothing in PureDarwin sends one. The root is therefore never quiet. PureDarwin's `waitQuiet(30 s)` in `IOKitInitializeTime` always times out (27 s after the drivers finish), and so does launchctl's `IOKitWaitQuiet(5 s)` (single-user: `launchctl.c` `do_single_user_mode2`; multi-user: after loading the LaunchDaemons). The other 30 s is the upstream wait for an `IORTC` resource; there is no RTC driver.
- **RAM root.** 159 MB of the 419 MB in it is `/System/Library/Kernels` (a DEBUG kernel for the virt machine and its link archive), 68 MB of the 150 MB sent after gzip. Nothing opens it.
- **Boot picture.** The 0.4 MB payload was inflated straight into the framebuffer (inflate reads its output back; that memory is not cached): 2.8 s. Reading all 16 MB back over USB: 1.9 s.
- **Serial console.** Not a cost under the hypervisor: 47 KB of log arrives in the first 1.5 s. On a real 115200 baud UART the 61 KB before the root mount would be about 5.3 s if written synchronously; that matters for an installed system with a serial console, not here.
- **Fixed sleeps.** The Wi-Fi job sleeps 4+2+8+4 s; the Touch Bar wait polls once a second.

## Changes (all in `integrations/puredarwin/m1-boot-speed/` unless a path says otherwise)
1. **Kernel** (`kernel-patches/0001`, `kernel.nix`, `build-kc.nix`; collection `/nix/store/z66w83kk8gj61xv5ych34rz1zv18hk70-puredarwin-kc-arm64-0.1`, the default desktop collection with only the kernel replaced). While the daemon's busy count is outstanding, `waitQuiet()` on the registry root returns when that count is the only one left; the count itself stays, so nothing that a really quiet root triggers (kext autounload, hidden media) starts to happen. `IOKitInitializeTime` takes its two timeouts from boot-args: `pd_quiet_wait=<s>` (registry, default 30) and `pd_rtc_wait=<s>` (clock; default 30, or 0 once the registry was quiet), and names the busy services when the registry wait runs out.
   - Boot `bs01` (this kernel, nothing else changed): the registry wait still ran out, and named why: `wlan` is busy. `IONetworkController::start` waits for the `IOBSD` resource, which is published after `IOKitInitializeTime`; a registry wait before BSD can never succeed on a machine with a network driver. So the desktop boots with **`pd_quiet_wait=0 pd_rtc_wait=0`**: root mounted 0.8 s after the kernel starts.
   - launchctl's 5 s wait is gone without rebuilding userland: 0.13 s from launchd to the shell, and the shell now really waits for every driver's `start()`.
2. **Launcher** (`launcher-fast-quiet.patch`, a superset of `m1-quiet-boot/quiet-boot.patch` for `PCIE_LAB_PATCH`).
   - The old 5 s wait also kept the console silent while the harness typed. Without it, drivers that finish in the background (Wi-Fi firmware until 2.3 s after root mount, sound jack detection at 4.0 to 4.3 s) log into the challenge's answer and the strict console proof fails (`bs01`: `NO_COMMAND_LINE_OBSERVED`). The launcher now types the challenge once a prompt has been seen and the console has been silent for 1.0 s. The proof is unchanged.
   - Boot picture: inflated into ordinary memory and copied (0.19 s), 16 rows read back instead of all 16 MB (0.02 s).
3. **RAM root** (`tools/stage_ramdisk_slim.py`, selected by the new opt-in `PCIE_STAGER` in `tools/pcie_assemble.py`): runs the stager unchanged, then removes `/System/Library/Kernels`, shrinks the file system to 384 MiB and zeroes free blocks; 92 MB on the wire instead of 150 MB. The staged image is cached by content, so a boot that stages the same files links it.
4. **Guest script** (`guest/darwin-gpu-desktop-fast.in`): no fixed sleeps. Wi-Fi waits for `en0`, for power on, and retries the join and the address; the audio server starts in the background once `/dev/pdaudio` exists; the Touch Bar state is polled every 0.1 s.
5. **Wrapper lookups** (`tools/bs_nix_path.py`): the store path of a recipe is cached under a key of every file the recipe names. Each `nix-build --no-out-link` of an already built recipe took 23 to 70 s here (the flake is evaluated each time); the desktop wrappers do three.

Boot with all of it: `tools/bs_boot.sh <bsNN>`. Changes to the defaults (`tools/gpu_desktop.sh`, `darwin-gpu-desktop-interactive.in`) are in `default-changes.patch` for the owner to apply.

## After
Boots `bs02` and `bs03` (same configuration, all five changes), against `ht01`/`df01`/`ht02` before. Seconds. Boot `df02`, the same through `tools/gpu_desktop.sh` after the defaults were changed: first frame 7.27 s after the hypervisor starts the guest, 51.2 s after the cycle starts.

| Segment | Before | `bs02` | `bs03` | Kind |
|---|---|---|---|---|
| Wrapper: store paths looked up, boot picture packed | 152 (`bs01`, three evaluations, loaded host) | 1.5 | 0.9 | harness |
| Packet assembled | 4.2 | 12.7 (image built) | 3.0 (image from cache) | harness |
| Packet delivered to the controller | 5.4 | 4.6 | 4.5 | harness |
| Inputs verified, Linux baseline, reboot requested | 1.5 | 1.2 | 1.1 | harness |
| Linux shuts down, m1n1 appears on USB | 15.0 to 17.5 | 13.3 | 12.9 | harness |
| m1n1 bootstrap, new USB enumeration, worker start | 6.5 | 6.3 | 6.3 | harness |
| Boot picture | 4.7 | 0.22 | 0.22 | harness |
| Hypervisor init, kernel collection upload | 3.3 | 3.3 | 3.3 | harness |
| RAM root: gzip / USB / decompress | 2.5 / 13.4 / 10.6 | 1.5 / 8.3 / 6.9 | 1.5 / 8.3 / 6.9 | harness |
| XNU start to root mounted | 60.4 | 0.83 | 0.82 | Darwin |
| Root mounted to launchd | 0.3 to 0.5 | 1.31 | 1.32 | Darwin |
| launchd to the shell | 4.9 to 5.6 | 0.13 | 0.03 | Darwin |
| Shell up to the challenge typed (console silent for 1 s) | 0.1 | 3.02 | 3.02 | harness |
| Challenge, command, script start | 0.2 to 0.4 | 0.42 | 0.41 | harness |
| Guest script before the compositor | 0.4 | 0.16 | 0.17 | Darwin |
| Compositor start to first flip | 1.44 | 1.45 | 1.45 | Darwin |
| First frame to both terminals ready | 0.15 | 0.15 | 0.14 | Darwin |
| **First frame after the cycle starts** | **126.1** | **48.6** | **48.2** | |
| First frame after the hypervisor starts the guest | 68.5 | 7.32 | 7.23 | |
| Both terminals ready after the hypervisor starts the guest | 68.6 | 7.46 | 7.37 | |
| Network up (address and ping) after the script begins | 28.4 | 9.9 | 10.0 | |
| Darwin segments, sum | 68.3 | 4.0 | 3.9 | |

From the wrapper's start to the first frame, bench queue excluded: 56.6 s (`bs03`, caches warm). Before, the same span was 136 s plus the three evaluations.

Root to launchd grew by a second: launchd now starts while the display, GPU and Touch Bar drivers are still starting (display driver registered 2.2 s after the kernel starts instead of 1.2 s). The total is what counts: the shell is up 2.3 s after the kernel starts.

### Two further settings, measured, not in the default patch (boots `bs04`, `bs05`)
`pdsound_delay=500` in the boot-args (the sound driver waits 2 s before it touches hardware; its jack-detection lines at 4.0 to 4.3 s were the last thing the launcher waited out) and `BS_RAMDISK_MIB=320` (45 MB free in the RAM root, as before this work, instead of 108 MB).

| | `bs02` | `bs03` | `bs04` | `bs05` |
|---|---|---|---|---|
| Shell up to the challenge typed | 3.02 | 3.02 | 1.88 | 1.66 |
| First frame after the hypervisor starts the guest | 7.32 | 7.23 | 6.25 | 6.14 |
| Both terminals ready after the hypervisor starts the guest | 7.46 | 7.37 | 6.35 | 6.27 |
| RAM root decompressed on the M1 | 6.89 | 6.89 | 6.35 | 6.34 |
| Linux shuts down, m1n1 appears on USB | 13.3 | 12.9 | 16.9 | 17.1 |
| First frame after the cycle starts | 48.6 | 48.2 | 50.2 | 50.2 |

The sound driver's own lines and its state at the audio server's start are the same in all four boots. The cycle total did not improve because the Linux shutdown happened to take 4 s longer in `bs04` and `bs05`; that segment varies from 12.9 to 17.5 s and is now the largest source of variance.

`bs01` is the kernel alone (stock script, launcher and RAM root, no boot-args): root mounted at 60.4 s as before, shell 1.4 s after root instead of 6, no desktop (console proof failed, see Changes 2).

## Open
- **Console silence is a heuristic.** 3.0 s of the 7.2 s goes to waiting until drivers stop logging (last line: sound jack detection, 4.25 s after the kernel starts, because `PDAppleSound` waits 2 s before it touches hardware) plus the 1 s of silence. A kernel line that arrives in the 0.2 s the challenge takes still fails the boot. The durable fixes are a session started by a launchd job instead of a typed command (SF-D D4), or drivers that stay busy until their background bring-up is done.
- **Harness, not this stream's to change** (measured, for the owner): Linux `systemctl reboot` until m1n1 answers on USB 12.9 to 17.5 s; m1n1 bootstrap 1.9 s; wait for the new USB enumeration and worker start 4.4 s; return to Linux after the session 40 s. Linux is not the slow part of the first: its journal ends within a second of the reboot request (`bs05`: request 20:40:07.3, last journal line 20:40:08, m1n1 on USB 20:40:24.4), so the rest is the reset, Apple's firmware and m1n1's first stage. Only a boot that does not go through Linux (`PCIE_CHAIN`, off) avoids it.
- **RAM root upload is still the largest item (16.7 s).** USB runs at 11 MB/s, the single-core inflate at 58 MB/s of output. 24 MB of the 92 MB sent is the compositor binary (19 MB of `__const` that barely compresses) and 14 MB is `libicudata`. Decompressing on a second core while the next part uploads would save about 6 s; not tried.
- **Drivers are slower while BSD starts beside them.** The display coprocessor took 0.3 s to boot when the kernel sat in its 60 s wait and takes 1.3 s now (display driver registered 2.2 to 2.3 s after the kernel starts instead of 1.2 s); launchd's exec takes 1.3 s instead of 0.4 s. Not explained. The shell cannot come up before the drivers are done, so this is about a second of the 6 to 7 s.
- **Wi-Fi bring-up is 2.3 s after the root mounts** in every boot, old and new; of that, 0.5 s is a 500 ms poll for the interface name in `PDBwfm::bringUp`. Its last log line is what the launcher's silence gate waits for once the sound delay is shortened.
- **Compositor start, 1.45 s** (0.93 s to renderer selection, 0.52 s to the first flip): not this stream's tree; numbers only.
- **Kernel defaults.** The collection needs `pd_quiet_wait=0 pd_rtc_wait=0`; without them it boots as before. Whether PureDarwin's registry wait in `IOKitInitializeTime` should exist at all, and what should publish `IORTC` (the SMC has the clock), are tree decisions.
- **Real install.** None of the harness time exists there. Darwin's own segments add up to 3.9 s (kernel to root 0.8, launchd 1.3, script 0.2, compositor 1.45, terminals 0.15); reading the kernel and the root from storage instead of USB is not measured. A 115200 baud serial console would add about 3 s for the 32 KB logged before the root mounts.
