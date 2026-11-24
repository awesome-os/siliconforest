## SF-AC: Vulkan on the M1 (Honeykrisp from Omacom's Mesa) — hand-over (agent VK, 14 November 2026)

Goal agreed with the owner: Vulkan on the M1 under SiliconForest with Omacom's Mesa ("Omacom has
better vulkan"). Milestones, in order: (1) `vulkaninfo` lists the Apple G13 device; (2) a headless
triangle renders and its readback hash matches a reference; (3) vkcube (or equivalent) in a window
on the Chonkstep compositor with correct presentation sync.

### State

| Milestone | State |
|---|---|
| 1 `vulkaninfo` lists the G13 | **Observed on the M1** (installed SiliconForest, staged, over SSH): GPU0 "Apple M1 (G13G B1)", `DRIVER_ID_MESA_HONEYKRISP` (Omacom names it "Borealis, derived from Honeykrisp"), API 1.4.363, driver-reported conformance 1.4.6.2, 153 device extensions (swapchain, timeline semaphore, external memory dma-buf, DRM format modifier, synchronization2, dynamic rendering, BDA, sparse binding), one 7.02 GiB heap. |
| 2 headless triangle, hash | **Observed on the M1** (`vk-tri`, runs `vk-tri-linear-005` and `vk-tri-copy-006`): `flat_fnv 0x5965baca` = the CPU reference in the program = lavapipe on the builder; the interpolated triangle differs from the CPU reference by 0 (lavapipe: 1); `VKTRI PASS` both with a linear host-visible target (render only) and with an optimal target read back by `vkCmdCopyImageToBuffer` (render + a compute dispatch on Honeykrisp). Numbers: fence after 1.49 ms (first submit), 0.377 ms per submit+wait over 100 frames (copy mode; 0.312 ms render only); device creation 54 ms with a warm shader cache (359 ms cold), pipeline 1.7 ms warm (139 ms cold). |
| 3 vkcube on Chonkstep | **Observed on the M1** (runs `vkcube-007`, `vkcube-008`, on DESK3's running `.next` compositor): Khronos vkcube 1.4.341, Wayland WSI, FIFO: 600 frames in about 10 s and 1200 in about 20 s (60 Hz pacing), exit 0. The compositor imported the swapchain's 8 dma-bufs zero-copy (`XR24`, modifier linear, stride 2048, "EGL image, no copy") and its own check at commits 30 and 150 compared the composited region with the client's buffer: 250,000 pixels, 0 differing, `client-pixels-are-on-the-frame`. Two renderer captures 500 ms apart show the textured LunarG cube complete and rotated (`runs/sfvk/vkcube-008-ab.png`, not committed). |

Presentation sync is implicit sync: Mesa's WSI attaches the render fence to the dma-buf
(`DMA_BUF_IOCTL_IMPORT_SYNC_FILE`, verified by `sync-probe`), and the compositor polls the dma-buf
readable before it renders with it (`poll-dmabuf-before-render`). Not measured: the compositor's
`fence_waits`/`fence_timeouts` counters (printed only when the compositor exits; the compositor is
DESK3's and was not restarted), and a load where a missing fence would show (vkcube's frames are
cheap). The compositor does not offer `wp_linux_drm_syncobj_v1`; nothing was requested from DESK3.

What made it work, in order (each found on the M1):
1. three build fixes for Honeykrisp on Darwin (patches 0101-0103: `vk_image` DRM modifier, ICD link
   arguments, a Mach-O UUID as the build id) — `vkCreateInstance` failed before 0103;
2. `ETIME` = `ETIMEDOUT` in all of Mesa (the kernel's LinuxKPI returns `ETIMEDOUT`);
3. **the clock**: Mesa's absolute DRM deadlines came from Darwin's `CLOCK_MONOTONIC`, which is
   `gettimeofday() - kern.boottime` (µs resolution; checked: 1794692401.974217 - 1794691325.275227
   = 1076.698990 s = the value read at the same moment), about 10.06 s **behind** the kernel DRM
   layer's clock (`pdos_uptime_us`, i.e. mach_absolute_time = `CLOCK_UPTIME_RAW`; why
   `kern.boottime` lies 10 s after that clock's zero was not established). Every finite wait shorter than that expired at once
   (`wait-probe`: a 300 ms wait took 0.0 ms with `CLOCK_MONOTONIC`, 302 ms with `CLOCK_UPTIME_RAW`),
   so `vkWaitForFences` returned `VK_TIMEOUT` (or `VK_ERROR_UNKNOWN` before fix 2) immediately and
   the test exited with its GPU work still in flight. Patch `m1-mesa-asahi/patches/0011` makes
   Mesa's monotonic time `CLOCK_UPTIME_RAW` on Darwin.

The clock, cleaner fix: Linux defines the DRM deadline clock as `CLOCK_MONOTONIC`, which on Linux is
the kernel's `ktime_get`. Darwin has no kernel clock equal to its userspace `CLOCK_MONOTONIC`: that
one is derived from the wall clock and moves when the time is set. The kernel's choice
(mach_absolute_time) is the right one for deadlines; the fix belongs in user space (done in Mesa;
libdrm callers that build deadlines themselves need the same). One caveat for later (SF-K, sleep):
`CLOCK_UPTIME_RAW` stops during sleep and `CLOCK_MONOTONIC_RAW` (mach_continuous_time) does not;
the kernel side would have to say which it counts once the machine sleeps. Nothing in the kernel
was changed for this.

The same two problems (clock, `ETIME`) are in the GL Mesa (147a62f) installed on the M1; the
compositor does not notice because it waits without a deadline. The fixes are in the shared recipe
now (`m1-mesa-asahi/mesa-darwin-build.nix`), so the next GL build carries them; **the installed GL
Mesa was not replaced**. Files that would change on an install: `/usr/local/lib/mesa-asahi/`
`libgallium-26.3.0-devel.dylib`, `libEGL.1.dylib`, `libGLESv2.2.dylib` (not `libdrm.2.dylib`).

The kernel side of synchronisation that the research report listed as missing is **present in
the installed kernel** (`sync-probe`, 15 of 15 on the M1): timeline syncobj signal/query/wait/
transfer, sync_file export/import and `poll()` (POLLIN, not POLLNVAL), syncobj fds, SCM_RIGHTS of
syncobj/sync_file/dma-buf descriptors, `DMA_BUF_IOCTL_EXPORT_SYNC_FILE`/`IMPORT_SYNC_FILE`, `poll()`
on a dma-buf. These are the layer patches 0021/0022/0024 of `m1-rust-kext/lkpi/patches-layer`. The
probe used already-signalled fences; a syncobj wait on pending GPU work woke on completion (vk-tri,
1.5 ms); a `poll()` wake-up on a pending sync_file or dma-buf was not tested on its own.

### How to build and run

`integrations/puredarwin/m1-vulkan/README.md`. In short: `nix-build stage.nix`, then
`tools/sfvk.sh push` copies the tree to `/private/var/db/sfvk` on the M1 (checksums compared);
`tools/sfvk.sh run <cmd>` runs with `VK_ICD_FILENAMES`/`DYLD_LIBRARY_PATH` set; `tools/sfvk-careful.sh`
is the wrapper for any run that could take the machine down (announces in `runs/m1-coord/log.txt`,
syncs, streams the kernel log off the machine every second).

### What is on the M1 and how to revert

- `/private/var/db/sfvk` (the stage: Honeykrisp, loader, vulkaninfo, vkcube, vk-tri, probes, the GL
  off-screen test; `zink/` holds the dev-build GL libraries and gl-bench) and `/private/tmp/sfvk-*`.
  No installed file of the system was replaced by the Vulkan work. Revert: `rm -rf /private/var/db/sfvk*`.
- **The kernel:** `sf/kernel.bin` on SFBOOT is `56759b9f…` (the taskqueue fix) since 22:43Z;
  `sf/kernel.bin.old` is `821cb932…`. Revert from the M1's Linux:
  `sudo bash ./sf-bootbin-install.sh <PARTUUID of SFBOOT> --rollback` (SF-R).
- On the M5: `~/vk-serial/` (the UART capture script and its captures).

### UAPI: kernel `asahi_drm.h` (linux-aurora `6a08a08e`) against Mesa

`m1-vulkan/tools/uapi_compare.py` (clang record layouts, ioctl numbers, constants):

| | Mesa `147a62f` (GL on the M1) | Mesa `b7b03c5` (this Vulkan) |
|---|---|---|
| items equal | 130 of 163 | 148 of 178 |
| ioctl numbers (12) | all equal | all equal |
| `drm_asahi_params_global` | kernel 616, Mesa 600 (size-negotiated tail) | identical (616) |
| `drm_asahi_cmd_render` / `cmd_compute` | kernel 448/200, Mesa 320/128 (size in `cmd_header`) | same |
| constants only in the kernel | 21 (feature bits 17-29, sparse tile, render/compute resources, cumulative signals, ...) | 5: `COMPUTE_WIDE_VISIBILITY`, `RENDER_CACHED_PROGRAM_PROBE`, `RENDER_FRAGMENT_DEPENDENCY(_COMPUTE)`, `VM_CREATE_HAL200` |
| only in Mesa | wrapper structs `*_with_prior`, `*_with_ray_query` | those plus `*_with_stage_prior`, `render_program(_v2)`, `compute_program`, flags `COMPUTE_USES_MATRIX`, `RENDER_COMPUTE_STAGE_BARRIERS`, `RENDER_SHADER_WRITES`, `RENDER_PROGRAM_*` |

All of the Mesa-only items are chosen from kernel feature bits or are G17/M5 paths. Our kernel
reports `features = 0x1` (SOFT_FAULTS only; `agx-params` on the M1), so Honeykrisp takes the
legacy G13 paths: 240-byte render commands, 128-byte compute commands, no exact-prior barriers.

### The crash of 21:06Z (found and fixed below)

What happened (runs/sfvk/vk-tri-00*.txt, runs/m1-coord/log.txt):

| Run (uptime) | What the test did | Kernel |
|---|---|---|
| 1 (~100 s) | submit (render, then compute), fence wait returned at once (clock), `exit(1)` without destroying the device | `firmware consumed its first channel command after 306 us` (the context invalidation of the teardown, first of the boot); nothing else |
| 2 (~402 s) | the same, with `ETIME` fixed: `VK_TIMEOUT` at once, `exit(1)` | nothing in the next ~60 s |
| 3 (~460 s) | `ASAHI_MESA_DEBUG=sync`: an infinite timeline wait inside `vkQueueSubmit`, then as run 2 | the machine stopped answering at ~21:06:30Z (uptime ~500 s) and came back in Linux by itself |

No panic text survived: the serial cable was disconnected then, SiliconForest writes no panic log,
the PMU count was read only after clean reboots. (The hypothesis below was wrong; the reproduction
further down found the real cause.) Lead hypothesis at the time: the death came
about 100 s after run 2, which is the GPU scheduler's job timeout (`drm_sched` timeout 100000 ms
in `queue/mod.rs`); run 2 exited while its job (which takes about 1.5 ms) could still be in
flight, so the scheduler's timeout work may have run after the queue's scheduler was finalised, or
the timeout path (`timed_out` -> `NoDevice` -> `retain_job_on_enodev`, m1-drm72 patch 0005) misbehaved
on a job the teardown had cancelled. linux-aurora's Rust wrapper keeps the scheduler allocation
alive for every job (`Job._scheduler: Arc<SchedulerInner>`) and `drm_sched_fini` cancels the timeout
work, so a fault would be in our C port of the scheduler or in LinuxKPI's delayed work
(`cancel_delayed_work_sync`), not in the driver; reading did not find it.

Since then every GPU run went through `tools/sfvk-careful.sh` with working waits (no test exits
with work in flight now): five runs, no crash, no driver message. The bug is still there for any
process that dies with GPU work queued (a crashing GL or Vulkan app could take the machine down).

The bench cable is back (14 November ~21:50Z) and the disk-booted kernel's console reaches the M5
(`macvdmtool serial`, no reset; `/dev/cu.debug-console`; the installed boot-args have
`serial=0x13 serial-device-name=uart0 debug=0x14e`): a panic's text is now captured by the careful
wrapper. Next step: reproduce deliberately (submit and exit at once) in a RAM-root bench boot,
where a panic costs no disk, read the panic, then fix in one kernel change.

Bench attempt (14 November 22:11-22:18Z, gui.lock, logged): the guest script is
`m1-vulkan/test/bench-teardown.in` (a baseline triangle, then (a) `_exit` right after 200 submits,
(b) the DRM files closed after 200 submits with the process alive, (c) `kill -9` while rendering,
each followed by 130 s of "alive" lines and new kernel log; `vk-tri` has the modes
`VKTRI_EXIT_AFTER_SUBMIT`/`VKTRI_CLOSE_AFTER_SUBMIT`). **Nothing was booted:** `tools/agx_boot.sh
vkb01` failed at RAM-root staging (the 480 MiB image was full: agx_boot stages the GL test's 31 MB
of libraries, the stage added 63 MB), `vkb02` with only the seven files the test needs failed the
stager's "at least 16 MiB free" rule; a third attempt through `tools/pcie_iterate.sh` without the GL
files was not run (refused by the session's permissions; the coordinator's one-retry rule). The M1
went SiliconForest -> Linux (clean `reboot`) -> SiliconForest (`e2fsck -fn` rc 4, the unrepaired
21:06Z state) with the daily compositor as DESK3 had left it. Whether the bench path works since the
disk install is therefore still unknown. A next attempt needs a RAM root with room (a base image
without the desktop payload, or the slim stager `m1-boot-speed/tools/stage_ramdisk_slim.py` that
`tools/gpu_desktop.sh` uses) and `PCIE_EXTRA_FILES` with the seven files.

### The crash reproduced and explained (14 November 22:22-22:27Z, disk system, UART captured)

Session `vk-teardown-*` (runs/sfvk/; announced, gui.lock, `macvdmtool serial` and a capture of
`/dev/cu.debug-console` on the M5 verified with a marker first, `sync` before each trigger):

- **(a) `_exit` right after 200 submits** (about 75 ms of GPU work left in flight): the machine
  survived the 120 s watch. Kernel log: `QueueJob 1664..1668: Job canceled on DRM scheduler
  teardown`, then five `GPU timeout` reports with fault info `reason: Unmapped`, reads of the
  test's shader code (`0x11fffbc000`, unit UL1C), compute data (`0x2fffeac000`, gCDM) and the VDM
  control stream (`0x2fffed0400`, dVDM_CSD) in VM slot 2, each followed by `Attempting recovery...`.
  The process's GEM handles were closed (and their GPU mappings dropped) while the firmware still
  ran its jobs.
- **(b) the DRM files closed after 200 submits, process alive**: the kernel panicked 0.2 s after
  the trigger (`runs/sfvk/vk-teardown-001-uart.txt`): `panic(cpu 5 caller ...): Mutex
  0xfffffe0011d05618 is unexpectedly not owned by thread ... @lock_mtx.c:148`, panicked task
  kernel_task. Symbolized against the installed collection (KC slide `0xb0b8000`; PDAgx's UUID
  `6B3B1C5C...` equal in the panic, the collection and the kext build) and PDAgx's link map:
  `thread_entry` -> `tq_handle_thread` -> `taskqueue_run_locked+0x10c` -> `pdos_mtx_unlock` on
  `Giant`. (c) was not reached.

Cause, found by reading with that stack: Haiku's copy of FreeBSD's taskqueue in our LinuxKPI layer
(`src/Kernel/fbsdkpi/haiku/freebsd_network/fbsd_subr_taskqueue.c`, compiled into every kext built
with `add_fbsdkpi_driver`: PDAgx and PDDcp in the installed collection) reads `task->ta_flags`
**after** `task->ta_func` returns, to decide whether to drop Giant. A LinuxKPI work item embeds its
task, and a Linux work function may free its own work item (the GPU recovery work does); the read
is then a use after free, and freed memory with `TASK_NEEDSGIANT` set unlocks a Giant nobody
holds. FreeBSD's own taskqueue never touches the task after the call. Fix: layer patch
`m1-rust-kext/lkpi/patches-layer/0041` (the flag is read before the call), one change for both
kexts (PDDcp builds from the same patched layer, `m1-dcp/build-kext.nix`). The 21:06Z crash had the
same shape (a process exited with work in flight; no text survived then).

**The fix on the M1 (22:41-23:08Z):** `sf/kernel.bin` `56759b9f…` = the installed kit `821cb932…` (rebuilt
without the patch: byte for byte) plus patch 0041; only PDAgx and PDDcp change in the collection (their
`taskqueue_run_locked` no longer reloads `ta_flags` after the call). Installed with `sf-bootbin-install.sh`
(`kernel.bin.old` = `821cb932…`), one-time boot, up at 22:43Z (`kern.kernelcacheuuid` 742E0857 -> D184D33A).
With the UART captured (`runs/sfvk/vk-teardown-002-uart.txt`, no panic line): variants a, b, c with 200
frames, then with 3000 frames in flight; a with 3000 drove 10 GPU timeouts and 10 firmware recoveries
(the event that preceded the 22:24Z panic) and the machine stayed up through b and c after it. Health
afterwards: the compositor alive (same pid since boot), the GL off-screen test passes by hash
(`agx-offscreen-test --expect-b 8fee6b41…`, installed GL Mesa), vkcube 300 frames in a window. One
observation: while the display is asleep (idle, 300 s) the compositor sends no frame callbacks, so vkcube
(FIFO) waits until the display wakes; that is pacing, not a GPU hang. The new kernel stays installed.

The first event, mappings dropped under jobs the firmware still runs, is not our port's: in
linux-aurora a GEM handle close calls `File::unbind_gem_object` -> `Vm::drop_mappings`, and Linux's
`drm_file_free` calls `drm_gem_release` (handle close) **before** the driver's `postclose` (the
queues). Only G15 and G17 VMs carry `VmLifetime` (`Vm::with_status`/`with_tracked_status`), which
defers handle-close unmaps while accepted jobs can still use the object; G13 VMs unmap at once, so
the same GPU faults are expected on Linux for a process killed with work in flight, and the
firmware recovery handles them. What must be robust is the recovery path, which the fix is about.
Bringing `VmLifetime` to G13 would avoid the faults; not attempted.

### Kernel round of agent GFX2 (14-15 November): video decode and Wi-Fi statistics on the disk system

Two changes, each its own kit build, each booted once and checked before the next (`sf-bootbin-install.sh`
from Linux, `asahi-bless --next`; `m1-vulkan/tools/sf-kernel-cycle.sh` does the whole cycle). Base check
first: the kit rebuilt from the tree gave the installed `56759b9f…` byte for byte.

| Kit | Change | `sf/kernel.bin` | `.old` | Checked on the M1 |
|---|---|---|---|---|
| k1 | `pdavd=1` on the command line (configuration record only; `kern.kernelcacheuuid` unchanged) | `35044741…` | `56759b9f…` | `/dev/pdavd` present; `m1-avd/tools/avd_disk_test.sh run`: H.264 High, HEVC Main, VP9 1080p, 300 pictures each, **900 of 900 identical** to the workstation's software decode (framemd5), 0 errors, timeouts or DART faults; 2.3-2.8 ms per picture |
| k2 | k1 + `if_attach_nx=46` + IO80211Family counting packets out (collection: IO80211Family, PDBwfm) | `7ad3b4a9…` | `35044741…` | 50 MiB pushed into the M1 over SSH: `ipackets` 334 -> 36856, `ibytes` +52.4 MB, `opackets` 197 -> 4196 (k1, same test: all three stay 0). Screen (capture during a 100 MiB push, 01:56Z): Activity Monitor's Network tab "Data received/sec 15.3 MB", totals counting; the dock's EN0 tile "13 M" down, "19 K" up. Activity Monitor's per-process Sent/Rcvd columns stay 0 (another source, not covered) |

- **AVD**: PDAppleAVD was already a member of the collection (`rebase/build-kc.nix`), inert without the
  boot-arg. The decoder firmware `avd-fw-v2-t0.bin` (aurora-silicon/avd-fw, MIT, 49152 bytes, from the
  M1's Linux) was copied to `/Library/Firmware/apple/` on the disk (not in git). Codex was told
  (`runs/m1-coord/requests-for-CODEX.md`). `/dev/pdavd` is root-only (0600), as SF-W says.
- **Wi-Fi receive statistics**: not a missing count in the driver. IO80211Family hands frames to
  `IONetworkInterface::inputPacket`, whose queue flush calls `ifnet_input_extended` with packet and byte
  counts. But en0 was marked `IFCAP_SKYWALK`: macOS's default `if_attach_nx` (0x2f) attaches a Skywalk
  compat netif to every legacy Ethernet interface, and no flowswitch is attached to an interface with the
  legacy transmit model (kernel log: `skip attaching fsw to en0 using legacy TX model`), so the compat
  netif is never activated and input runs through DLIL's own handler, which skips its statistics for
  `IFCAP_SKYWALK` interfaces (`dlil_input.c`: "the stats are already incremented there"). Nobody counted
  them (`SIOCGIFCAP` on the M1: en0 `0x400`). `if_attach_nx=46` is that default without
  `IF_ATTACH_NX_NETIF_COMPAT`: legacy interfaces stay plain legacy, which is what their data path
  already was. Packets out were 0 for another reason: `IONetworkInterface` counts output bytes itself but
  output packets only from the driver's `IONetworkStats`, which no 802.11 driver kept
  (`m1-bwfm/patches/0001`, applied to the tree's IO80211Family). Byte counters read by non-platform
  programs are rounded to 1 KiB by xnu (`rtsock.c`), as on macOS. `m1-bwfm/tools/ifcounters.py` reads
  them on the M1 the way Activity Monitor does (`NET_RT_IFLIST2`).
- `sfvk-careful.sh` lost lines of a command's output at each one-second streaming boundary (`tail -n` of
  a file whose last line was being written); the first AVD comparison showed 3 "missing" pictures per
  clip for that reason. Fixed (lines are streamed by number); the complete output stays on the M1 in
  `/private/tmp/sfvk-careful-<name>.txt`, and the AVD test compares that.
- `e2fsck -fn` before each install: rc 4, the same findings as since the 21:06Z crash (inode 65144,
  blocks 636291-636295, group 17) plus 266 "extent tree could be narrower" optimisations, which are
  not errors. Not repaired.
- **Installed now: k2, `sf/kernel.bin` `7ad3b4a9e6e7e3e1…`** (full hash and how to rebuild it: SF-R, "14-15 November, night"; `install/out/` holds it).
- Revert: `sf-bootbin-install.sh <SFBOOT> --rollback` gives k1 (`35044741…`); the kits are in
  `runs/gfx2/kit-{base,k1,k2}` and the one before in `runs/m1-desktop-010/install-out-before-gfx2-avd-wifi/`.

### Graphics in the default install (agent GFX2, 15 November)

`m1-vulkan/system.nix` + `tools/sf-gpu-install.sh` (also the userland installer's part `gpu`,
`m1-storage/tools/sf_userland_push.sh push gpu`; INSTALL.md says so). On the M1 since 00:19Z:

| Path | What | Before |
|---|---|---|
| `/usr/local/lib/mesa-asahi/libEGL.1.dylib`, `libGLESv2.2.dylib`, `libgallium-26.3.0-devel.dylib` | GL Mesa b7b03c5 (clock + `ETIME` fixes, Wayland EGL platform) | 147a62f, kept as `<name>.old-b7b03c5` |
| `/usr/local/lib/mesa-asahi/libvulkan_asahi.dylib`, `libwayland-client.0.dylib` | Honeykrisp, the client library the new EGL links | new |
| `/usr/local/lib/libvulkan.1.dylib` (+ `libvulkan.dylib`) | Khronos loader 1.4.341 | new |
| `/etc/vulkan/icd.d/asahi_icd.json` | the ICD (the M1's `XDG_DATA_DIRS` is `/usr/share:/share`, so `/usr/local/share` is not searched; the first install put it there and the loader found no driver) | new |
| `/usr/local/bin/vulkaninfo`, `vkcube` (with `SFVK_CUBE_INSTANCES`), `vkcubepp` | tools | new |

`libdrm.2.dylib` stays the installed one (same 212 exports). Records: `/private/var/db/sfgpu/installed-<tag>.txt`
(tags `b7b03c5`, `b7b03c5-etc`, `b7b03c5-cube`); revert: `sf-gpu-install.sh revert <tag>` in reverse order, then
relaunch the compositor.

Observed on the M1 (gui.lock, UART captured, no Vulkan environment variable set):
- the compositor relaunched on the new GL (`WMSTAT selected renderer=gl output=kms width=2560 height=1600`)
  and has run on it since, through a recovery boot; the GL off-screen test passes with the same exact hash
  as with 147a62f (`8fee6b41…`); other agents have run Terminal and Activity Monitor on it since (their holds);
  no Firefox or other GL application is installed on the disk system;
- `vulkaninfo --summary`: Apple M1 (G13G B1), `DRIVER_ID_MESA_HONEYKRISP`, API 1.4.363;
- vkcube in a window: FIFO 300 frames in 6.3 s (with start-up), **MAILBOX 600 frames in 4.5 s** (about
  134 fps), **IMMEDIATE: "Present mode specified is not supported"** (Mesa's Wayland WSI offers no
  immediate mode); the compositor's own check (composited window against the client's buffer at commits
  30 and 150) 4 of 4 identical;
- present race: the cube drawn 1, 300 and 3000 times per frame (`SFVK_CUBE_INSTANCES`), 200 frames each:
  6 of 6 compositor checks identical, 0 differing pixels; but 3000 instances still ran at about 50 fps,
  so these frames were not heavy enough to show a missing fence (heavier runs: below). The compositor's
  counters at the end of a session with 900 dma-buf commits: `fence_waits=0 fence_wait_max_us=27515
  fence_timeouts=0 fence_poll_errors=0` (implicit sync, `poll-dmabuf-before-render`);
- explicit sync: not tried (the compositor has no `wp_linux_drm_syncobj_v1`; not cheap).
- The Vulkan DRM properties do not match the device node: Honeykrisp reports `renderMajor 31,
  renderMinor 22913`, `hasPrimary false`; `/dev/dri/renderD129` is `31, 22657` (card0 `31, 24832`,
  card1 `31, 24577`). Anything that matches a DRM fd to a VkPhysicalDevice by device number (zink on an
  EGL DRM screen, dma-buf feedback) finds none: a lead for "zink does not start", not established.

The drop of 00:53Z (M1 restarted into Linux during a GFX2 hold; recovered at 00:56Z): no panic and no GPU
message; the compositor ended with `PDHID sequence discontinuity: queue overflow or competing reader`
after input at the seat (menus, a window closed; not the inject module, not another agent), then the
session's end restarted the machine. After it `e2fsck -fn` lists new orphan/bitmap findings (not repaired).

### GL, zink, conformance, Metal (after the milestones)

**Native GL vs zink on Honeykrisp** (`m1-vulkan/test/gl-bench.c`: surfaceless EGL, 1024x1024 FBO, 64
opaque rectangles a frame, `glFinish` per frame, exact-image hash against a CPU reference; runs
`gl-zink-compare-00*`, 200 frames, the M1 otherwise idle):

| Driver | ms per frame | fps | image |
|---|---|---|---|
| installed GL Mesa 147a62f (asahi, `/usr/local/lib/mesa-asahi`) | 2.350 | 425.6 | `0x727f9a62` = reference |
| Mesa b7b03c5 asahi (dev build of this stage) | 2.293 | 436.1 | `0x727f9a62` = reference |
| Mesa b7b03c5 zink on Honeykrisp | **does not run** | | |

Zink needed a build change for Darwin without MoltenVK (`m1-vulkan/patches/experimental/0201`, applied by
no recipe; it builds). On the M1, `MESA_LOADER_DRIVER_OVERRIDE=zink` makes EGL's DRM screen fail
("DRI2: failed to load driver") and EGL falls back to surfaceless swrast (softpipe, minutes per run);
why zink refuses the device was not established. So the comparison is native GL only for now; no
change to the compositor's renderer was considered.

**Conformance (agent GFX2, 15 November):** dEQP-VK of VK-GL-CTS 1.4.5.3 cross-built for arm64 Darwin
(`m1-vulkan/cts.nix`: target `vulkan_headless` on the CTS's Unix platform code; two seds) and run on the
M1 by `tools/sfvk-cts.sh` against the system install (no environment), UART captured, each group capped:

| Group | Cases | Pass | Not supported | Fail | Crash / hang |
|---|---|---|---|---|---|
| info | 19 | 17 | 2 (memory budget, device groups) | 0 | 0 |
| api.smoke | 6 | 6 | 0 | 0 | 0 |
| api.object_management.single | 47 | 47 | 0 | 0 | 0 |
| memory.allocation.basic | 102 | 102 | 0 | 0 | 0 |
| synchronization.basic (incl. timeline semaphores) | 29 | 21 | 8 (no exclusive compute queue; one queue only) | 0 | 0 |
| compute.pipeline.basic | 78 | 73 | 5 | 0 | 0 |
| draw.renderpass.simple_draw | 4 | 4 | 0 | 0 | 0 |
| api.copy_and_blit.core.image_to_image.simple_tests | 23 | 15 | 8 | 0 | 0 |
| glsl.arrays.constructor | 28 | 28 | 0 | 0 | 0 |
| texture.filtering.2d.formats.r8g8b8a8_unorm | 9 | 6 | 3 | 0 | 0 |
| api.external.memory.dma_buf | 50 (33 reached) | 16 | 15 (no image export; the CTS's Darwin build has no dup()/fd passing) | 1: `dedicated.device_only.export_multiple_times`, `vkEnumeratePhysicalDevices` -> `VK_ERROR_INITIALIZATION_FAILED` | **1 hang: `dedicated.device_only.fd_properties`** (below) |

Groups `api.external.semaphore.sync_fd` and `api.external.fence.sync_fd` were not run: the dma-buf
group hung the device first. Totals: 377 cases ran to a result (335 pass, 41 not supported, 1 fail), 1 hung; no fail outside the dma-buf group,
the fail and the hang in it.

**The hang (01:22Z):** in `fd_properties` (export a dma-buf, then `vkGetMemoryFdPropertiesKHR` on it,
which in Honeykrisp is a PRIME import of the device's own buffer) deqp-vk went into an uninterruptible
wait in the kernel and stayed there (`kill -9` no effect); from then on a new `vulkaninfo` also blocked
uninterruptibly while opening the device. No kernel message, no GPU timeout; the running compositor kept
working. The test before it had failed already at device enumeration, after exporting one memory
object many times. Cleared only by a reboot (01:37Z). Not analysed further: a kernel item (LinuxKPI
PRIME/dma-buf import of an own buffer, or a lock left held after the failed enumeration), for the next
kernel round, announced against CODEX-TB's base first. These are smoke groups chosen by hand, not a conformance submission;
"Not supported" is what the driver reports for this device, not a failure.
The CTS build is ours; the driver's reported conformance 1.4.6.2 is still Omacom's claim for Linux.

**What the Metal experiment (Darling's Indium: Vulkan 1.3, timeline semaphores, dma-buf import)
would find here:** Vulkan 1.4.363 reported; `timelineSemaphore` and `VK_KHR_timeline_semaphore`
reported, and the kernel's timeline syncobj ioctls work (`sync-probe`), but no Vulkan-level timeline
semaphore test was run; `VK_EXT_external_memory_dma_buf` and `VK_KHR_external_memory_fd` reported,
PRIME import of a dma-buf received over a socket works at the kernel level (`sync-probe`), and the
compositor imports Honeykrisp's dma-bufs, but importing a foreign dma-buf into a `VkDeviceMemory`
was not tested. Those two tests are what to write first.

### Open items

- ~~`/dev/pdavd` absent on the disk kernel~~: done (kit k1, see "Kernel round of agent GFX2").
- ~~Install the GL Mesa with the clock and `ETIME` fixes~~: b7b03c5 installed (see "Graphics in the default install").
- A test that would catch a missing implicit-sync fence (a heavy frame per present), and the
  compositor's fence counters; `wp_linux_drm_syncobj_v1` in Chonkstep (explicit sync) would be the
  better protocol, request to DESK3 when wanted.
- MAILBOX works (about 134 fps); IMMEDIATE is not offered by Mesa's Wayland WSI here.
- Conformance: no CTS run (see above).
- Zink on Honeykrisp does not start (EGL falls back to swrast); patch 0201 is experimental.
- **Kernel, second issue: mappings dropped under in-flight jobs** on G13 (GPU faults + firmware recovery
  when a client dies or closes its DRM file with work queued; reproduced 14 November, see "The crash
  reproduced"): in linux-aurora a GEM handle close unmaps at once on G13 (`File::unbind_gem_object` ->
  `Vm::drop_mappings`, and `drm_file_free` releases handles before the driver's `postclose` stops the
  queues); only G15/G17 VMs carry `VmLifetime`, which defers the unmap while accepted jobs may still use
  the object. The fix is to give G13 VMs the same deferral (or to stop and drain a file's queues before
  its handles are released). Not attempted; after taskqueue fix 0041 the faults are recovered, not fatal.
- **Kernel, third issue (15 November): a process that used Indium cannot exit.** Indium's device polling
  thread waits on a timeline semaphore with an infinite timeout (`vkWaitSemaphores(..., UINT64_MAX)`, a
  DRM syncobj timeline wait); at `exit`/`_exit` the process stays in state `?NE` for good (seen twice;
  the GPU stays usable for others). Linux's wait would return `-ERESTARTSYS` when the task is killed;
  our LinuxKPI's interruptible wait is evidently not ended by XNU's thread termination. To check in
  `m1-rust-kext/lkpi` (signal_pending / schedule_timeout against thread_abort) in the next kernel round.
- **Kernel, fourth issue: the dEQP-VK dma-buf hang** (`fd_properties` after `export_multiple_times`):
  uninterruptible, and new device opens blocked until a reboot (see "Conformance"). Not analysed.
- vkcube waits while the display sleeps (no frame callbacks); a compositor policy question for DESK3.
- `memory_limit = 0` from our kernel's GET_PARAMS (newer kernels report it); Honeykrisp falls back
  to the machine's memory for its heap (7.02 GiB).
- The ray-query render path (`drm_asahi_cmd_render_with_ray_query`, 448 bytes) depends on the kernel
  feature `RENDER_RAY_QUERY`, which our kernel does not report, so it is not used; its tail layout
  against the kernel's 448-byte `cmd_render` was not compared.


### 2026-11-15 23:20Z desktop freeze: devfs lookup, captured before recovery

CODEX-TB captured the still-wedged machine before reset under core/gui/bench:
37 tasks/201 threads, kernel stackshot decoded and symbolicated against exact
installed CIO29 and its AGX/DCP maps, ps,23:15+ dmesg, service ioreg and live UART.
Private evidence/analysis: runs/sftb/gpu-wedge-2320/.

Both compositor28999 main73279 and Firefox GPU helper28583 Renderer72355 wait
in devfs_dntovn+0x94/msleep/lck_mtx_sleep on the same channel. The calls are
open/namei and fstatat/namei, before DRM callbacks. The captured AGX/DCP workers
are idle in taskqueue semaphores; services remain attached/busy0. No corresponding
GPU fault/timeout/recovery appears in the retained ring. The kernel is waiting
for DN_CREATE completion, not a DRM mutex held by the exiting helper. Generic
msleep gives no owner; no live creator was identified in the captured stacks.

The installed shared PDDRMDri already returns clone slots on close, but stat-only
lookups allocate cloned vnodes without ever calling open/close. devfs reclaim
never returns those reservations. Actual frozen source under host ASan/UBSan
reproduces1023 leaked slots, then ENOMEM on stat lookup1024; devfs_dntovn's early
failure skips DN_CREATE clear/wakeup, leaving later callers blocked indefinitely.
The historical lookup count and original failing caller were not captured on
hardware. This mechanism matches the observed wait; it does not establish that
the older dEQP dma-buf hang or syncobj wait had the same cause.

Offline fix: creation-error cleanup and actual-minor retirement, interruptible
PCATCH creation wait, opt-in vnode-reclaim clone API, and shared DRM reservation
ownership through final reclaim instead of early close/error reuse. Existing
GPU-file teardown still owns pages/mappings; no in-flight backing is freed to
make exit pass. The earlier taskqueue0041 fix and GPU/DCP drivers are unchanged.
Actual-source tests cover15000 lookup/open lifecycle cycles, pool failure/reuse,
minor1, abort, delayed reclaim, legacy consumers and live-cookie refusal.

The source, frozen native recipes, link verification and next-window gate are in
[m1-devfs-clone/README.md](../../../integrations/puredarwin/m1-devfs-clone/README.md).
This is the highest-priority addition to the SAME future Firefox AVD/K14/HID
install, not another boot now. Native SIGKILL/IPC closure and metadata stress
remain unqualified until an owner-opened window; fixed-image probe is built only.

Recovery returned standard .next/socket/inject and a non-input stats reply on
exact CIO29 at23:37:34Z, UART stopped/locks released. One M5 reset was used.
Read-only e2fsck returned4 with bitmap/count differences, journal replay skipped;
no repairs made. Correction: initial clean request used /sbin/reboot rather
than supported fenced /bin/reboot, so a shutdown-kernel hang was not proved.
No extra recovery boot was used to retest that mistake. CODEX-TB then resumed
its machine pause and continued only workstation work. No AVE or push.


### Devfs-only recovery kernel install in progress, 2026-11-16 00:54Z

The owner's later durable Firefox authorization supersedes the earlier machine
pause for Firefox kernels; idle and hourly waits are lifted. Thunderbolt and AVE
remain paused. The first image deliberately contains only devfs/core + shared
PDDRMDri changes, with55 of57 executable module UUIDs unchanged from CIO29.
Normal installation retained CIO29 as rollback; the supported /bin/reboot reached
Linux without an external reset. Read-only filesystem checking returned4 with
bitmap/count errors and two beyond-EOF extent records in Firefox YouTube SQLite
files. No repair was performed because this exceeds the conditional bitmap/count
repair permission. Private full output, inode/path evidence and filesystem features
are retained in runs/sftb/devfs-install-0041/.

Observed on the new M1 boot: DESK8-next003 hash46dc9bde runs with responsive
non-input inject stats; four processes each completed5000 stat,5000 open-close
and5000 failed O_DIRECTORY opens. Reservations allocated60000/freed60000;
used slots3 before/after, zero allocation failures. GL exact pixel hash and
vulkaninfo each passed50 launches. Firefox20 fresh-profile visible-window
qualification is in progress, so final adoption is not yet claimed.

Identification correction: Mach-O LC_UUID/embedded kernel UUID1FDC2A71… is
not kern.kernelcacheuuid. XNU obtains that sysctl from the prelink plist's
_PrelinkKCID, which is65F85F90-FF31-4541-A51E-8247DD0C4881 in the actual capsule
and live M1. Decompressed capsule bytes equal the built collection. The initial
UUID assertion failure and premature explanation are preserved with the corrected
proof in the run directory; no image change was needed.


Devfs-only qualification completed2026-11-16 01:03:43Z: the image remains
installed as the normal SiliconForest kernel with CIO29 rollback. Twenty
consecutive fresh profiles04–23 map/commit/capture a window and cleanly Quit/reap
their whole family; slots3 after every launch, zero allocation failures. Median
map5.4175s, range4.055–6.844s. LOOKED at the20-capture montage: ALL content areas
are blank/stale NewTab chrome. Thus window creation/close passes while Firefox
page-paint is STRICTFAIL, handed to FX/DESK; no presentation-correctness claim.
DESK8-next003/inject responds, owned UART reader/route are gone, locks released.


### Native Firefox42 import panic and rollback, 2026-11-16

The five-module image0d31b0c0… booted successfully. INFO reported ABI1/caps65543
(import bit16); the gesture ring reported776 bytes/version2. Clone stress on
the actual asahi renderD129 allocated/freed60000 reservations, slots3→3, failures0.
The first harness attempted absent renderD128 (ENOENT, no allocations); that
failure is retained with the corrected-node repeat. The decoder packet's
portable Python placeholder was resolved in a separate native manifest after
all45 files were cold-hash verified; original bytes/receipt remain private.

Strict H264 shared-capture testing then panicked at01:17:15Z in
vm_upl.c:1360, VM_OBJECT_WIRED_PAGE_UPDATE_END's !__overflow assertion. Exact
symbolication gives upl_commit_range+0x1cf4 →upl_commit →
IOGeneralMemoryDescriptor::complete+0x288 →pd_avd_os_dma_free+0x110 →
avd_buf_free/pd_avd_session_destroy/context_destroy. This is teardown accounting,
not a demonstrated Force-CMO fault. The register pair426/−426 is consistent
with a wired-count underflow; MD kind/UPL flags were not captured. No candidate
or after-stats survived; before-stats show jobs0/contexts0. HEVC was not started.
No shared-buffer, cache-visibility, decoder-output or physical-pinch success claim.

The panic automatically returned to Linux; no external reset was needed.
Normal installation restored qualified devfs-only12168f25… /prelink65F85F90….
The consumed next-try mark required one supported sf-session relaunch-next
after boot; DESK8-next003/socket/inject and non-input stats were verified.
UART reader/route stopped, actual interactive lock holder exited01:20:47Z.
Postpanic read-only fsck returned4 (journal skipped plus previous extent/bitmap
errors); no repair, root subsequently mounted. An initial rollback command
found Linux's volatile /tmp installer gone; it made no boot-file write. The
installer/env were reuploaded and the normal rollback then verified its hashes.
All failures and UART are retained privately in runs/sftb/firefox-install-0042/.

The following image43 deliberately retains the ORIGINAL CIO29 AVD, excluding
the failed import/CMO/IRQ input. It retains devfs and K14/HID, adds KERN's
reviewed killable-wait PDAgx/PDDcp and speaker cap25 half-dB steps (control176),
and requires exactly7 changed modules/50 unchanged against CIO29. Source/SDK/
link/host evidence is separate from the forthcoming native wait/speaker gate.
The UPL fix is being prepared independently; no assertion or complete call is removed.


Owner update2026-11-16 01:38Z: extra speaker cap defaults OFF. The current model
passes comparison testing with zero violations; the owner explicitly withdrew
the interim volume reduction. The boot argument pdspeaker_cap_steps remains
available, with default0; model/probe safety limits remain unchanged. The first
cap25 image43 reservation was cancelled before ANY M1 action; its kit is retained
privately. The replacement image43 uses sound SDK c5zjb6pd…, collection gif6ishr…,
capsule11f6f53b2089f2a6…, prelink0C63FD93-8845-4DE8-85E5-C317D51928A0.
It still has7 changed modules/50 unchanged against CIO29 and ORIGINAL AVD.
Native default-cap0 / K14 / killable-wait qualification is pending.


### Private-UPL accounting fix prepared offline, 2026-11-16

The frozen source provides a concrete counterexample for the42 panic: device-pager
objects hold private, noncanonical pages, which vm_page_wire/unwire deliberately
exclude from object/global canonical wired counts. The bulk UPL completion path
subtracts all finally unwired pages and queues them, underflowing the zero object
count for426 private pages. A one-line eligibility guard routes private objects
through the existing per-page completion, including its canonical-only accounting
and noncanonical queue guards. Assertions and real complete/unwire remain intact.
The historical descriptor kind and UPL flags remain unrecorded; source agreement
and a matching counterexample do not replace native verification.

Actual-patch-applied ASan/UBSan tests retain the frozen426-page assertion as an
expected failure and pass patched private/canonical cases, repeated wires,
INACTIVATE/FREE_ABSENT, vector/purgable and empty guards. Media's independent
source review agrees with the per-page accounting/queue behavior. Exact frozen
before/after hashes pass in the full native core build, now in progress. All six
repository checks pass. Source, limits and collection recipe are in
[m1-upl-private/README.md](../../../integrations/puredarwin/m1-upl-private/README.md).

The native SDK helper is built but unrun: it first double-maps a real writeback
GPU BO, unmaps its alias, then imports only DATA after a16KiB header and destroys
the context while checking the original mapping/header. It submits no GPU or
decoder jobs, refuses the wrong kernel/import capability and retains synchronous
phase markers. This is the first proposed hardware gate for a fixed-core/import
image; strict complete-frame/PTS/held64 H264 and HEVC gates follow only if it
passes. No zero-copy, physical-pinch or GPU-fence success is claimed.

Image43 remains the independently ready original-AVD/default-cap0 cohort.
Thunderbolt and AVE trials remain paused; no UPL candidate has been installed.


### Kernel43 qualified and retained normal, 2026-11-16 02:08:31Z

The owner's requested default-off sound input is installed: the live
kern.pd_speaker.cap_steps is0 and the enabling boot argument remains available.
Image43 capsule11f6f53b… /prelink0C63FD93… retains original CIO29 AVD (caps7,
no import), devfs, K14/HID and reviewed killable-wait AGX/DCP. Exactly7 executable
modules differ from CIO29;50 are unchanged. No AVE or dock trial was added.

Observed on the M1:60 infinite binary/timeline syncobj waits across SIGKILL,
SIGTERM and caught SIGUSR1 all returned/reaped, worst2.6ms; four concurrent
children completed60000 stat/open-close/failed-open operations with60000 clone
allocations/frees, slots3→3 and zero failures. Exact GL pixel hashes and
vulkaninfo each passed50 runs. This qualifies the tested interruptible waits,
not every uninterruptible DMA wait or a real Firefox-helper fallback. The latter
remains an FX integration gate. Gesture-ring776B/version2 is live; physical
pinch remains unobserved. Current model comparison still has zero violations.

The supported /bin/reboot reached Linux without external reset. Read-only fsck
returned4 with the same two beyond-EOF extents and bitmap/count errors; no repair
was made and root mounted afterwards. Normal installation kept qualified devfs
as rollback; original CIO29 remains local. DESK8-next004 c4a8c437, socket/inject
and non-input stats were verified. UART reader/route stopped; SystemExit released
actual core/gui/bench at the02:08:31 back-in-SiliconForest marker. Private receipts
are under runs/sftb/firefox-install-0043. No push.

The separate private-UPL core and collection now build successfully. Native
instruction review finds the added object-private bit13 branch to per-page
completion, absent from43. The57-module candidate has exactly8 changed/49
unchanged against CIO29 and only core+AVD differ from43; the real Force-CMO
BL/stub/chained-pointer check passes. It is uninstalled. Its first hardware gate
will be the bounded no-job GPU-BO pin/import/destroy probe, followed by strict
frame/PTS/held64 checks only on success. No zero-copy or decoded-output claim.


### Kernel44 shared capture qualified, 2026-11-16 02:37:43Z

The fixed private-UPL core is now installed and retained normal with the
reviewed import driver, exactly8 changed/49 unchanged executable modules versus
CIO29; only core and AVD differ from qualified43. Capsule7b2eba23…,
prelink9ACED54D…. Speaker extra cap remains0 with its enabling boot argument,
and K14/HID plus the qualified killable-wait drivers remain unchanged.

Observed on the M1:3 no-job rounds each imported/destroyed14 real writeback GPU
BOs carrying6965056 image bytes (426 padded16KiB DATA pages plus a header),
with distinct duplicate mappings,
alias unmap, original mapping alive and header canaries preserved. The real
complete/unpin path ran without the prior UPL overflow. DART allocations,
contexts and quarantine returned to zero; no decoder or GPU job was submitted.

Strict H264600 and HEVC300 pictures both pass the independent Linux full-visible
NV12 MD5+PTS oracle, actual GPU backing/two planes/DATA offset, first picture
held64 callbacks and copied-bytes0. Fresh cold prefixes per codec avoid reusing
Mach-O files after XNU's instruction lowering; all45 staged files were verified
before loading. Kernel jobs900/done900, errors/timeouts/stale/DART faults0,
contexts0/DART KiB0/quarantine0. Final GL pixel hashes and Vulkan each pass10
launches; clone slots remain3. This proves the tested shared-capture paths,
not GPU-consumer completion, producer-task death, browser presentation or60fps.
H264's instrumented decoder average4221us and MD5 callback21377us must not be
read as a playback-rate result. Those remaining integration gates stay explicit.

The clean /bin/reboot reached Linux without reset. Read-only fsck returned4:
the previous two EOF extents plus one new three-block EOF extent in an agent's
Firefox startup cache, with bitmap/count differences. Exact ncheck/stat are
private; no repair and no clean-filesystem claim. Three4KiB blocks beyond a
52208-byte EOF complete a16KiB VM page: a pager/truncation rounding hypothesis
was passed for source audit, not established as the cause. Root mounted.

No installed Linux Firefox was found, so the requested reference was skipped:
no browser install, headless substitute, scores or extra boot. An initial nested
helper syntax error ran no browser; its failure is retained alongside the
corrected availability check. The normal installer kept43 as rollback; original
CIO29 and devfs-only kits remain local. DESK9-next001 df5a3290/socket/inject and
non-input stats were verified; UART stopped, actual core/gui/bench released by
SystemExit at the02:37:43 return marker. Private receipts and classifications:
runs/sftb/firefox-install-0044. No dock/AVE trial or push.


### Kernel44 producer-task lifetime gate, 2026-11-16 03:18:01Z

Three native rounds passed producer GPU-VA unmap, SIGKILL of only our child,
and reap in2.53–2.54ms while the parent kept the consumer BOs. Immediate INFO
returned0, contexts1 and caps0x10007: retirement is lazy, on the next CREATE,
as the frozen driver documents. The real CREATE reaper ran complete/unpin;
consumer data marked after producer death survived retirement and new allocation
pressure. Final contexts/DART bytes/quarantine were0, with no GPU or decoder
jobs and no fault-counter change. This tests that lazy retirement contract,
not immediate-on-death cleanup or GPU-consumer completion.

The first probe's immediate-INFO composite predicate remains a failed run; its
stale errno did not establish an ioctl timeout. Revised source prints each field
and tests the documented CREATE reaper instead of changing the kernel to meet
that mistaken expectation. Both runs remain private under
runs/sftb/firefox-task-death-0044{,-v2}. Same qualified44 and DESK9-next001
remained running; non-input stats responded, UART stopped, actual core/gui/bench
released at03:18:01. No install, reboot, injection, dock or AVE trial.


### Kernel45 ext4 EOF allocation qualified, 2026-11-16 03:45:05Z

Capsule5c323fb6/prelink2BFF4349 is installed normal, source733332ef0. Only
com.apple.filesystems.ext4 differs from qualified44:57 executable modules,
9 changed/48 unchanged versus29,55 other kext sources and core retained.
All6 required checks pass; real VM frozen/fixed controls give e2fsck RC4/0,
28 mmap/write sizes+7 resize steps, clean unmount and29 independently dumped
file bytes each. The source clips write BLOCKMAP requests at current/active
extending EOF; journal ordering, flushes, NVMe and halt fences are unchanged.

On the M1,28 own mmap/write cases+7 resizes/fsync/reopen and29 host byte
oracles pass. The first native st_blocks==data-size assertion FAILS and remains
a failed diagnostic: five fragmented files include extent-tree metadata, which
the frozen ext4_ext_account_meta explicitly counts. Read-only F_LOG2PHYS_EXT
(flags0, no allocation, packed20-byte ABI) then verifies247 initialized data
blocks and681 hole/unwritten tail blocks across all29 files through128KiB.
This establishes the tested absence of initialized rounded EOF tails, not absence
of unwritten preallocation, physical-media readback, whole-filesystem cleanliness
or power-loss safety. The original accounting failure was not relabeled PASS.

GL exact hashes5/Vulkan5 pass; clone slots3/fail0. Import caps65543, gesture
ring776B/version2 and extra speaker cap0 remain; no AVE/boot-arg change. No
new decode/fence/browser-FPS or physical-pinch qualification is added.

Clean /bin/reboot reached Linux without reset. Unmounted-root read-only fsck
returns4 with two legacy EOF extents and known deleted-inode/bitmap/count
errors; the earlier startup-cache record is not reported in this pre-install
check. No repair, and no existing-error improvement is credited to45. Root
mounts. Normal installer retains44 rollback; default Linux and one-time bless
are preserved. Same DESK9-next001 df5a3290/socket/inject stats respond; UART
reader/route stopped and actual core/gui/bench released by SystemExit at the
03:45:05 back marker. Receipts: runs/sftb/firefox-install-0045, private.

The30-file4KiB descriptive sample is worse after this cold boot: write p50
0.288→1.459ms, fsync9.790→11.010ms, unlink11.262→20.358ms. Different boot,
warmth and allocation state prevent causal attribution. This is not a speed
improvement; stage timing/steady-state repeats are needed before a performance
change. Source already batches64 metadata blocks/1s; no durability reduction.
A separate source audit notes ignored fsync/flush errors and commit-error buffer
cleanup, without a native I/O-error observation or a proposed45 journal change.


Follow-up offline fragmented control: --fragmented seeds alternating holes in a
fresh regular scratch image using debugfs, requires a clean precheck, then boots
the same frozen/fixed drivers. Both28 IO cases+7 resizes/29 dumped byte oracles
pass;17 files in each contain counted extent-tree metadata. With
e2fsck -fn -E no_optimize_extents the frozen image has real initialized EOF
extent errors (RC4), fixed image is clean (RC0). The optional optimization
exclusion matches M1 checks and does not suppress consistency errors. The first
fragmented classifier expected only i_size wording and remains failed; the
corrected check ties either exact i_size or logical13..15 bounds errors to the
inode of /m-52208. Private receipts: runs/sftb/ext4-eof-qemu-fragmented-{001,002}.

A further native45 CLI sample (no input/install/reboot; other desktop work active)
gives three30-file p50s: fsync16.129/13.264/12.678ms and
unlink18.619/13.081/13.489ms. No causal44/45 performance claim follows.
Read-only NVMe counters show0 errors/timeouts, flush lifetime mean6.519ms;
392 flushes during the samples imply an approximate interval mean6.760ms
(from rounded cumulative averages). Global background I/O is included. The
source's two journal flushes could account for much of the13ms latency; this
is a measurement-supported lead, not per-call attribution. DTrace was absent
from PATH. Before choosing an optimization, obtain syscall/commit/flush-stage
counts and timings; preserve crash consistency and all existing barriers.


### Urgent Firefox launch check on45, 2026-11-16 04:24Z

TERM reported a real SSH-launched family failing child check-in with
BOOTSTRAP_UNKNOWN_SERVICE, and DESK reported a second-launch crash. The initial
health harness also had missing automation permission and an owner-overlapped
capture; these failures were retained, not counted as kernel failures.

The corrected installed-app gate completed3 compositor application requests
and3 SSH wrapper launches. CODEX-TB independently inspected all six retained
no-input renderer images: each has its own heading/text and three coloured
squares. None of those six Firefox logs contains bootstrap_look_up failure or
MachCheckInListener timeout. This refutes a universal45 launch outage; retain45
with exact44 rollback ready. The earlier genuine intermittent timeout remains
open and is not erased by these six successes. FX owns the fuller runtime gate.
Private receipts: runs/firefox/urgent-launch45-v2 and
runs/sftb/firefox-bootstrap-0045.

Comparison of the actual derivations confirms IDENTICAL immutable core input
sp4zw2… for44/45, all55 other kext source inputs,605-byte boot arguments and
boot.bin. Only ext4 changes. The same Mach SEND_INVALID_RIGHT guard signature
is already present in the saved44 state before45's reboot. These facts exclude
an intentional Mach/bootstrap policy or boot-arg change, not an effect of boot
timing or filesystem behavior. A native SSH parent registered a fresh private
service; its exec-spawned child looked it up successfully. The first probe's
cleanup failed with SERVICE_ACTIVE because its receive right was still alive;
that failure remains. Closing that right before unregister gives all-zero
registration/lookup/cleanup in the corrected probe. No existing service changed.

CORRECTION: an initial TB name-filtered process review incorrectly called a
Firefox parent childless; its PPID graph contains12 plugin-container descendants.
The correction was posted immediately after the full graph was checked.

Pre-install filesystem validation was REAL QEMU, completed03:25:43 BEFORE the
03:41:12 installer: exact frozen11f479… and patchedar0bc… SDKs,16KiB VM/4KiB FS,
28 mmap/write cases,7 resize steps, clean unmount and29 independent Linux
debugfs byte oracles per driver; frozen fsck RC4 reproduces the EOF error,
patched RC0. Actual-source ASan/UBSan counterexample/controls and96 identical
SDK exports also passed. The fragmented17-metadata-file VM control came AFTER
installation and is supplemental. Native45 acceptance had filesystem/GPU/desktop
checks but no fresh painted Firefox launch; the corrected gate closes that gap.
Future kernel acceptance must include a fresh installed-app painted launch via
each route before retaining it normal, as well as the change-specific tests.
No rollback, reboot, bootstrap policy rewrite or user runtime selection was
performed for this check; the owner-specified both-paths-fail trigger was not met.

FX final review04:25:04 confirms all3+3 launches map/paint with no bootstrap
errors, but desktop3 has paragraph glyph corruption (s appears as dots), so
this is launch availability, not full visual quality. The other five reviewed
scenes are correct. The launchd1/sshd4/session5 parent relationships also match
the saved44 boot; complete service timing/state equality is not established.


### Decoder context attribution candidate and DESK crash follow-up, 2026-11-16

The urgent launch check retains45; no further reboot occurred. FX subsequently
recovered DESK's exact minidump and matched the loaded binary UUIDs: a null
CFRunLoop yields fault address0x10 in CFRunLoopRemoveSource during AppRestart /
xpcom-shutdown. A network-reachability caller is in that path. This is a distinct
observed failure from MachCheckIn timeout, not evidence of GPU/devfs wedging;
the teardown cause remains open with the framework owners.

Source-only candidate integrations/puredarwin/m1-avd-context-stats adds ABI1
cap17 and a versioned128-byte context-owned read-only ioctl7. Real synchronous
session counters distinguish own jobs/results from concurrent decoding; no
IRQ writer, allocation, hardware programming, timer or teardown is added by a
query. PID/task ownership and a generation token reject foreign or recycled
contexts. The six existing ioctl sizes/opcodes are unchanged on host and the
actual arm64 SDK. Capture backing/IOVA/prepared byte fields describe ONLY the
live capture set, not all decoder allocations or post-destroy retirement.
An absent context is not proof of zero DART mappings or GPU quiescence.

The literal patched getter and decode function pass ASan/UBSan with fake
providers, including rejection atomicity, two-session interleaving and decode
success/HOLD/error/timeout/sync branches. The real kext SDK build and old/new
arm64 ABI assertions pass. These are offline results, not native attribution
or real DMA/interrupt tests. Media/KERN have the source-review contract; no
collection, install or machine test is reserved for it. Private build/test
receipts are under runs/sftb/avd-context-stats/. Kernel45 retains its original
AVD module. Native acceptance will require two real owned contexts, independent
packet/pixel/PTS counts, ownership/generation refusal and live capture queries,
as well as both installed Firefox routes painting before promotion.

DESK9 staged .next002 (8ed7da8a…) and daily002 (39dff5bd…) for the next boot.
The compositor running during this follow-up remains001; no relaunch occurred.


### Selected upstream candidate over45 (2026-11-16, not yet installed)

Offline collection379sk preserves the exact qualified45 core and57-module
cohort. Only PCI/xHCI/AVD executable UUIDs change; the USB composite matching
metadata adds IAD support. AVD adds the reviewed versioned context-statistics
ioctl, with live-capture-only memory scope; no decoder/cache policy changes.
The other54 UUIDs and52 kext sources match45, debug stripping proves every
non-debug section unchanged, devfs/cache linkage and overlap checks pass.
No endpoint admission, AVE, filesystem or wait-policy change is introduced.

The separately attempted59-module variant also embeds newly built em/igc
(with PCI matching removed), but fails the fixed core's16MiB prelink slot:
payload0x1250000. That full image must not be installed. Do not move the frozen
core or discard unrelated drivers to hide the failure. Network sources remain
SDK-built/staged for the paused dock track. The default candidate omits them.

Actual host controls, arm64 builds and QEMU004 root-port USB/storage/FS gates
pass; receipts are runs/sftb/upstream-12377/. Kernel46 notice is posted, but
installation/native acceptance has not occurred at this checkpoint. It must
verify both installed painted Firefox routes and standard DESK10 state, with
pdm1tpscroll=20 only for desk10-or-newer and45 as rollback. No physical scroll
or native per-context attribution proof follows from these offline checks.

### 2026-11-16 kernel 46 native qualification

Kernel 46 retained the exact qualified 45 core, ext4, GPU and DCP inputs.
Five GL-hash and five Vulkan runs passed; three desktop and three SSH
Firefox launches produced mapped, readable test documents with all three
colour regions, confirmed by capture inspection. No input was injected.
The new AVD per-context API passed two-context isolation, foreign PID/task,
generation and malformed-request checks with API-error counts 1 versus 2.
These probes submitted zero actual decode jobs. Live capture bytes exclude
other buffers and post-destroy state; real decode/import/fence retirement
attribution remains for the media test. The earlier intermittent launch
failure is still open; six successful launches do not prove it eliminated.

The first 46 package stopped in the bootloader on oversized boot arguments,
not a kernel panic. The corrected package was qualified and made normal;
qualified 45 rollback remains. Wi-Fi47 is being prepared as a single-driver
replacement; it does not change GPU/core or claim video/FPS improvements.

### 2026-11-16: kernel 47 Wi-Fi-only native check

Kernel 47 preserves the qualified 46 core and all 56 other executable module
UUIDs. Only PDBwfm changes. Firmware timeout GET and runtime A/B pass; short
same-boot one-second ping median falls from 179 to 5.85 ms with timeout 2000.
Five GL-hash and five Vulkan checks pass, with clone slots returning to three
used and zero failures (519 allocations, 516 frees, peak seven). Three desktop
and three SSH Firefox launches paint their distinct local test pages with
readable text and the expected colours; all six captures were inspected.
These tests inject no input and establish neither video/FPS nor page-load
performance. AVD/import and GPU inputs remain unchanged.

The first desktop launch job exited 1 before starting the session; a supported
launchd job-start retry recovered the standard desktop. Its cause remains open,
with raw evidence and a source retry-policy request filed to DESK. There was no
kernel panic or external reset. Successful post-recovery Firefox launches do
not erase that startup failure or the earlier intermittent bootstrap issue.
Desktop-route owned identities disappeared on quit in 1.33–1.38 seconds.
The SSH harness did not reap/poll its direct Popen launcher or record its state
before identity cleanup; a remaining PID is therefore not exit-hang evidence.
No exit-status qualification is claimed. Kernel 47 is normal with 46 rollback,
UART stopped and install locks released; private receipts are in
`runs/sftb/firefox-install-0047/`.


### 2026-11-16: real pending dma-buf fence and kernel 48 qualification

The request to add missing dma-buf sync-file ioctls was based on stale source
assumptions. Actual kernel 47's frozen AGX/DCP trees already contain export/
import, poll, cross-kext recognition and ioctl-owner dispatch patches. The
existing signalled sync-probe passed again on 47; CODEX-FX withdrew its
unsupported missing-ioctl claim. No GPU kernel patch was required.

The new standalone `m1-dmabuf-fences/pending-probe.c` passes on native kernel 48,
whose AGX/DCP/core modules are identical to 47. A prestarted exec child receives
an actual finite-compute GPU producer's dma-buf and exported reservation
snapshot through SCM_RIGHTS. Both descriptors are first observed not ready;
they wake after 46.52 ms and every word of the 4 KiB GPU output matches the
independent CPU oracle. The original completion fd was closed after the
snapshot, and the queue drains before storage destruction. Fence export,
writer import and reader snapshot export took about 20/10/4 microseconds.
Clone slots return to three used, zero failures. The actual deployed ioctl
implementation also passes extracted-function ASan/UBSan validation, failure
and reference-cleanup controls; the final SDK executable is byte-identical
to the native-tested build.

The previous pending probe failed because it waited on its own unsubmitted
future host timeline point. Timings placed the five-second wait in Mesa's
fence export. The pinned Mesa source waits for threaded submission/input-point
availability before returning that output fence (`VK_SYNC_WAIT_PENDING`).
This is a test dependency error, not evidence of absent ioctls or a kernel
fence bug. The failed control is retained; the successful default uses finite
GPU work and an already-ready child, with exit 77 if pending state is missed.

CODEX-FX and TERM11 have the live UAPI, tiny probe and producer/CPU-consumer
contract in their requests files. Actual subsequent GPU consumer, image reuse,
resize/death and broker/VideoToolbox leases still require client qualification;
this result alone does not authorize dropping those leases. Reproduction,
reference paths and retained failures: `m1-dmabuf-fences/README.md`.

Kernel 48 changes only ext4's journal-image read shortcut, with core and 56
other UUIDs and the 599-byte command line unchanged. Its source/SDK/sanitizers,
real QEMU metadata/I/O oracles and clean scratch fsck passed before installation.
Native cache controls/data pass, GL/Vulkan five each, and three desktop plus
three SSH installed Firefox launches paint readable distinct pages with all
captures inspected. All owned identities disappear naturally in 1.48–1.92 s
without cleanup signals (SSH launcher status zero; desktop exit status unknown).
No synthetic input was injected. Standard DESK10 `.next` starts automatically
this boot and responds to stats. This does not resolve earlier intermittent
bootstrap/startup failures or establish media/FPS/page-load performance.
Qualified 47 is verified rollback. The root's read-only fsck retains known
extent/i_size/bitmap/count errors; no repair or clean-root claim. UART and all
install locks are released. Private receipts: `runs/sftb/firefox-install-0048/`.


### Wind-down hand-over: installed 48, pending 4K capacity work

The owner stopped all new installs and experiments on 2026-11-16. Kernel 48
remains normal (`2ec8d7c4…`, runtime `E042DBE8-9934-4A88-A71A-16DCAD877E05`),
with the complete qualified 47 kit as rollback. SF-R records the full hashes,
installer, retained filesystem errors and the legacy oversized boot.bin.old
caveat. No kernel install or reboot was in flight when the stop arrived.
The standard `.next` compositor and the owner's Firefox were alive at the
wind-down read-only audit. No CODEX-TB native test clients remained.

The sync-file finding is settled at the kernel-UAPI level: the deployed
kernel already implements dma-buf EXPORT/IMPORT_SYNC_FILE, reservation fences,
sync_file descriptors and poll. The native pending-compute probe passed a
real unsignalled-to-signalled cross-process producer/CPU-consumer transition,
verified all 4 KiB and returned clone slots to three. It is retained at
`/private/var/db/sftb/firefox0048/pending-probe`; run it with
`VK_ICD_FILENAMES=/private/var/db/sfvk/icd.d/asahi_icd.json` and
`DYLD_LIBRARY_PATH=/private/var/db/sfvk/lib` under GUI/bench in a later session.
Source/build and exact limits are in `m1-dmabuf-fences/README.md`. No new GPU
kernel patch was needed, and no evidence here qualifies arbitrary GPU consumer,
resize/reuse/death or VideoToolbox lease removal. CODEX-FX/TERM own that part.

The 4K capacity fix is **not done**. Media's 15:38–15:46 evidence on kernel 48
shows CREATE failing for a 1597-page contiguous IOVA range with 1985472 KiB
mapped in a 2 GiB window, despite physical free memory. Existing contexts and
fragmentation both matter. Transactional IMPORT maps the complete new capture
pool before releasing the old one; its peak must be budgeted, not bypassed.
A separate source candidate in `m1-avd-capacity/` expands to 3 GiB below the
32-bit ceiling and adds owner/task/codec/buffer/IOVA-run diagnostics. The full
arm64 SDK and collection built before stop, with only AVD changing from 48.
It has no new host fault tests, QEMU, native decode/data/retirement validation,
packed capsule or install. Do not call the successful build a working 4K fix.
The preparation's first assertion failure and corrected no-fuzz source build
are retained privately. README there names the build output and exact resume
requirements; do not install it during this wind-down.

Ranked kernel defects are filesystem-wide delete/flush contention, 4K decoder
IOVA capacity/fragmentation, unresolved failed-write/ENOSPC filesystem findings,
then intermittent earlier bootstrap/startup and the remaining GPU-client
lifetime cases. The next five actions are to review and host-test the AVD
bounds/allocator/diagnostic patch; budget coexisting CREATE and atomic IMPORT;
rehearse the unchanged boot cohort in QEMU; in a later authorized kernel
session qualify actual concurrent 4K/1080p decode and retirement with 48 rollback;
and have FX/media/TERM measure the real consumer and presentation paths.
Filesystem batching needs its separate crash-consistency gates from SF-R.
No new installation, owner-profile change or push was performed at wind-down.
