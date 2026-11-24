## Hand-over: disk-root stream (13 November 2026; read this first)
The SiliconForest partition exists (ext4, Linux `nvme0n1p3`, first block 192,633,862, **16,035,840 blocks of 4096 bytes since 13 November 2026** (61.17 GiB, last block 208,669,701); it was 16,777,216 blocks (64 GiB) in boots dr01 to dr23 and 16,166,912 in dr24 to dr27) and holds the default desktop root. The owner took blocks from its end twice: 610,304 for SiliconForest's own startup-picker volume (the stub) and 131,072 for a 512 MiB FAT boot partition (the two-stage design is decided). With the 41 blocks that were already free behind it, blocks 208,669,702 to 209,411,118 (741,417 blocks) are unallocated and belong to nobody yet; nothing of ours may write there. The filesystem and the partition entry were shrunk from Linux by the coordinator both times (start and identifier unchanged; table backups in `private/disk-root/gpt-backup/`), the fence constant followed (commits `18490a9b6`, `92c390cb0`), and boots dr24 and dr28 to dr30 proved each layout ("The shrunk partition" and "Step 5" below). **A collection built before `92c390cb0` must not be booted with the disk root** (it refuses to open its fence, because the partition arguments no longer equal its compiled-in range; do not rely on that). Its unique GUID and the filesystem UUID are in `private/disk-root/sf-part.env` (never in git; re-read with `sudo blkid` on the M1's Linux).

| Step | State |
|---|---|
| 1 Read-only driver at the tip, our partition visible | **Proven on the M1: boots dr01, dr02** (below) |
| 2 Root on the partition, read-only | **Proven on the M1**: shell (dr05) and the desktop (dr06) under the hypervisor, the desktop natively (dr08); see "Step 2". Open: a session started by launchd from the disk root; the timeline without the upload (step 4) |
| 3 Write-fenced driver | **3a to 3d proven on the M1 (boots dr11 to dr21)**: the separate build `PDAppleNVMeRW` wrote files through ext4 on a second, fenced block device; Linux found the filesystem clean and the files hash-equal; everything outside the partition unchanged; an unclean end is recovered by Linux and by Darwin; a downgrade to read-only leaves the filesystem clean while still mounted; the desktop runs with its root read-write on the partition under the hypervisor and natively, and files and a desktop setting persist across boots. See "Step 3" |
| 3e Daily-use gaps: clock, shutdown/restart, settings | **Proven on the M1 (boots dr22, dr23)**: the time from the hardware clock (read only), the system's own `reboot`/`shutdown` ending in an SMC restart with the root clean and no controller reset, the last boot's `/var/run` and `/tmp` cleared at boot, theme and display scale chosen in the desktop menu in one boot and worn by the next session. See "Step 3e" |
| 3f The partition shrunk by 610,304 blocks, fence and collections follow | **Proven on the M1 (boot dr24)**: identified in both table copies at the new length, writes admitted only inside `[192633862, 208800773]`, the freed space untouched, earlier boots' files intact, clean restart by `reboot`, Linux `e2fsck -fn` clean. See "The shrunk partition" |
| 4 Boot chain (B1 stage without the RAM root record) | **Booted on the M1 (sfb10, dr25, dr26, dr27)**: the stage, with nothing but itself and the kernel uploaded (14.4 MB, 2.7 s), boots the desktop from the disk root; the session is the disk's own, started by launchd; the boot ends by the system's own `reboot` and the M1 is back in Linux without a controller reset. dr27's verdict is 86 ok and 1 FAIL (a checker anchor; reviewed and accepted, the stop file set aside as `dr27-STOP-set-aside-after-review.txt`). See "Step 4" |
| 5 The second shrink; the network in the disk's own session; a session without the bench | **Proven on the M1 (boots dr28, dr29, dr30, 13 November 2026)**: the partition found in both table copies at 16,035,840 blocks, writes admitted only inside `[192633862, 208669701]`; in the disk's own session Wi-Fi is joined by pd-netd from the remembered network, an address is obtained and the router answers; a session with no `sfbench=` in the boot arguments runs its time and ends in its own restart, the root clean. Open: the install (`b1/INSTALL-PROCEDURE.md`, owner). See "Step 5" |
| 6 The install kit: `boot.bin` as a file, stage 1, the installer route | **Installed by the owner (13 November 2026); the first start stopped safely in stage 1 (`image span overlaps the live heap`); fixed without a new install and proven on the bench with the real boot partition (boots dr35, dr36): `sf/boot.bin` is stage 2 alone, which reads `sf/kernel.bin` itself. The start from the picker with the new files has not been done yet. See "Step 6" and "Step 6b".** Before the install: the file an installed stage 1 would read (stage 2 without proxy fallback + configuration + collection, `pdnvme_ttl=0`) boots the desktop from the disk root and restarts itself with nothing else sent; stage 1 with a boot partition that does not exist stops by itself. Kit and owner's steps: `m1-native-boot/install/INSTALL.md`. See "Step 6" |
| 7 Daily use: SSH, Homebrew, the Claude Code CLI, a session without a time limit | **On the disk and run on the M1 (boots dr38 to dr45, 12 and 13 November 2026)**: SSH with keys only, Homebrew and 26 formulae; an ext4 driver defect found by dr41 (freed inodes, e2fsck -fn rc 4) is fixed, proven by dr42 (rc 0) and on SFBOOT (`sf/kernel.bin` `093b61f1…`). **Claude Code runs since boot dr46** (it was ended by libxpc, not by the kernel; a fixed `libSystem.B.dylib` is on the disk, the old one beside it as `.old`; `SF-X-claude-code.md`). **Open: sftp does not work.** See "Step 7" |

Commands (all from the lab root; the bench is taken by `tools/bench.sh`, `BENCH_WHO=disk`, one boot at a time, names `drNN`):
- Host tests (run by every boot wrapper): `integrations/puredarwin/m1-storage/host-test/run-host-tests.sh` (`GATEHOST`, `PARTHOST` 48 cases, `SRCHOST`).
- One slot with the read-only driver and our partition (Linux baseline, boot, Linux again, verdict):
  `BENCH_WHO=disk NV_VERDICT_MODE=part NV_RANGE_FILE=runs/m1-storage/linux-c/ranges NV_TTL=300 NV_RUNTIME=420 tools/bench.sh run integrations/puredarwin/m1-storage/tools/nv_slot.sh drNN 3 any`
  (use `NV_RANGE_FILE=runs/m1-storage/linux-e/ranges` now: `linux-d`'s 40 plus three ranges of the boot partition SFBOOT, blocks 209,280,006 (its first 256), 209,344,006 and 209,407,750 (its last 256); `linux-d`'s two ranges in the former free space (208,669,702 and 208,735,232) now lie in the stub's APFS container. Linux mounts neither partition; SFBOOT is written from Linux between boots only (`sf-bootbin-install.sh`), so within a slot all of these must be equal, and `dr_plain_ranges.py` counts SFBOOT's with the ranges Linux never writes (`SF_BOOT_LBA`, `SF_BOOT_BLOCKS` in `sf-part.env`). `dr_desk_slot.sh` uses `linux-e` by default. `linux-d/ranges`: 40 ranges for the present layout: the 37 of `linux-c` plus the last 256 blocks of the partition as it is now (208,669,446), the first 256 unallocated blocks behind it (208,669,702) and 256 in the middle of the newly freed part (208,735,232); `linux-c`'s "last 256 of the partition" (208,800,518) and everything behind it now lie in the unallocated space. `linux-c` was `linux-b/ranges`, the 34 ranges of the layout before the first shrink, plus three of the same kind for that shrink. `dr_desk_slot.sh` uses `linux-d` by default; `runs/m1-storage/ref-tree-sw01.txt`: digest of the source image's tree, made with `user/pdnvme-test.c` built on the host over `debugfs rdump` of `artifacts/m1-desktop-010/pcie-enum-candidate-sw01/ramdisk.img`).
- Step 4, the stage without a RAM root (stage image first: `B1_ROOT_UUID=<filesystem UUID from sf-part.env> integrations/puredarwin/m1-native-boot/b1/build.sh`):
  read-back `BENCH_WHO=disk DR_RW=1 DR_HALT=reboot DR_STAGE=disk DR_STAGE_STOP=1 tools/bench.sh run integrations/puredarwin/m1-storage/tools/dr_desk_slot.sh drNN native 240`; the boot itself the same without `DR_STAGE_STOP=1`, with `DR_SECONDS=60 NV_PREV_CONSOLE=<previous console.bin>`; the session, the launchd job and the bench's pieces are put on the disk by `DR_RW=1 DR_HALT=reboot DR_INSTALL=1 DR_COMPOSITOR=<the compositor already on the disk>` (launcher-driven, RAM root). A session without the bench: the boot's command with `DR_PLAIN=1` (no `sfbench=`; only `sfsession_seconds=<DR_SECONDS> sfmark=1`; the slot's own checks instead of the verdict).
- What the network left on the root, read-only from Linux: `BENCH_WHO=disk tools/bench.sh run integrations/puredarwin/m1-storage/tools/dr_linux_root.sh netlog`.
- QEMU rehearsal, no M1: `tools/dr_qemu_disk.py` (a 4096-byte-sector GPT disk around an ext4 image), `tools/dr_qemu.sh <name> <guest script> <disk> ro|rw` (the `qemu-dev` tools unchanged). Rehearse every new guest script there first: three script mistakes that would have cost M1 boots were found this way.
- A verdict failure writes `runs/m1-storage/STOP`; it is set aside only on the reviewer's word (dr01's is kept as `dr01-STOP-checker-and-node.txt`).

Traps: the collection has about 0x2000 bytes of room before the kernel image and members are padded to 16 KiB per segment (the kext is built `-Os` for that reason; `kc_overlap.py` fails the build otherwise); at the tip devfs is not ready until the root is mounted (about 75 s without `pd_quiet_wait=0 pd_rtc_wait=0`); `mount` resolves `/tmp/x` to `/private/tmp/x`; the shell exits on a failed redirection of `:`; `rm` asks on a read-only filesystem unless `-f </dev/null`; `qd_run.py`'s screenshot after `--up` froze the guest's serial output (the wrapper puts `--up` at the end); `pdnvme-test wtest` must never be pointed at a node that could be writable (it writes block 0 back); **on this root `/var` and `/private/var` are two directories, not a link**: launchd's jobs (pd-netd, mdnsd) keep their files in `/var/run` and `/var/log`, the session and `sf-boot` use `/private/var/run`; after the guest's `MACH Reboot` the QEMU of a rehearsal hangs (end that one QEMU by its exact PID after reading its command line).

### Step 1: the read-only driver at the tip with our partition visible (dr01, dr02)
Kext changes: `kernel/pd_nvme_part.c/.h` (plain C, host-tested): the partition is named by three boot arguments, `pdnvme_part=<unique GUID> pdnvme_part_lba=<first block> pdnvme_part_blocks=<length>`, and the driver reads both GPT copies itself (not through the storage stack): each header valid by its checksum and pointing at the other, both tables valid and byte-identical, exactly one entry with that GUID, of type Linux filesystem data, with exactly that first and last block, inside the usable range, overlapped by no other entry. Only then does `PDAppleNVMeMediaGuard` decline that one media (whose position, length and `UUID` property must also match); any doubt and it is guarded like the others. `kern.pd_nvme.part` says which; `kern.pd_nvme.media` gains `ours=N`. Nothing about writing changed: the media is write-protected, the gate is the same. Also: `/dev/pdnvme0` is retried once a second until devfs exists and no longer delays the block device (dr01's verdict failed on its absence at the tip; the rest of dr01 passed), and the kext is built `-Os`.
Host: `PARTHOST` 48 cases (every field, every boundary, either copy altered with checksums made right, duplicates, overlaps, unreadable blocks); the M1's real GPT copies (read from Linux, kept under `runs/`) are accepted with the real GUID and refused with the start or length off by one.
Collection (tip, default + PDAppleNVMe): `/nix/store/3qlsc4jqp9ng8vjnw4xl930nsi241km2-puredarwin-kc-arm64-0.1` (dr02); dr01 ran `dp558sl7…`.

Result of dr02 (hypervisor, RAM root, 8 CPUs, `pdnvme=3`, verdict PASSED 45 of 45; dr01 the same except the private node):
- our partition `verified … found in both GPT copies`; `partitions=8 guarded=7 foreign=0 ours=1` before and after; the stack's GPT equals Linux's 8 starts and sizes; Identify equals Linux's.
- `/dev/disk0s3` mounted `-o rdonly` with the tree's ext4 driver, twice: the whole tree (9,230 files, 764 directories, 106 links, 258,444,027 bytes) hashes to the source image's digest both times, 5.0 s each (about 52 MB/s through ext4; raw sequential 735 MB/s); three named files equal.
- writes: a mount without `rdonly` is refused (`Permission denied`), as is a remount read-write; create, mkdir, unlink and append in the mounted filesystem give `Read-only file system`; opens for writing are refused on `rdisk0`, `disk0`, `rdisk0s1`, `rdisk0s3`, `disk0s3`, `pdnvme0`. **Correction (found in the step-2 rehearsal):** the ext4 driver does try to write on a read-only mount (the console has `disk0s3: privilege violation.` six times in dr02: the superblock at mount and the journal superblock and superblock at unmount, for each of the two mounts); its own statistics (`writes=0`) count only writes that succeeded. Every one of them is refused by IOMedia, which was opened for reading only, before it reaches the driver (`write_refused` is 2, the two deliberate opens of `/dev/pdnvme0`), and the partition's blocks are unchanged (ranges, Linux `e2fsck`); `gate_rejected=0`; SMART inside Darwin: write counters +0.
- ranges: 24 of 24 on the GPTs, `p1`, `p2`, **`p3` (ours)**, `p4`, `p8` and the gaps identical in Linux before, Darwin (private node and block device) and Linux after; 6 of 6 untouched ranges in Linux's filesystems equal; 4 rewritten by Linux itself. The same digest over all ranges after the mounts as before.
- stop by the script after 22 s, clean; Linux: no recovery or replay line, `p5`/`p6`/`p7` mounted as usual, `p3` not mounted, `unsafe_shutdowns` 107 → 107.
- for step 3: a read-WRITE mount sets the journal's needs-recovery flag in the superblock at mount (`ext4_jnl_mount`) and clears it at a clean unmount; a root that is still mounted read-write when the machine is reset will therefore be replayed at the next mount.

### Step 2: root on the partition, read-only (dr03 to dr09)
How it is put together:
- **Root selection:** `rd=uuid boot-uuid=<FILESYSTEM UUID> wdt=-1`. The M1 kernel waits for the `boot-uuid-media` resource, which the tree's `Ext4FileSystemDriver` publishes for the Linux-typed leaf media whose superblock carries that UUID; disk numbers play no part. (QEMU's loader instead hands the kernel a match on the partition GUID: dr03 used that on the M1, found no root, and ended by the driver's time to live, cleanly.) The launcher's built-in `rd=md0` is replaced when the extra arguments carry an `rd=` (`launcher-diskroot.patch`, generated against the launcher base tree; `dr_desk_boot.sh` concatenates it after `m1-native-boot/launcher-native.patch`).
- **The RAM root stays uploaded**: it is `/dev/md0` (writable, memory), it carries each boot's script and test tools, and the same boot can be repeated with `rd=md0`.
- **Process 1** is `user/sf-roinit.c`, installed as `/usr/appleinternal/sbin/launchd.sfro` and chosen by `launchdsuffix=sfro` (DEVELOPMENT kernels; falls back to `/sbin/launchd` where the file is missing): devfs, the memory disk on `/private/ram`, then `execv` of launchd.
- **The root as changed for read-only use** (`tools/sf-root-transform.sh`; applied to the partition from Linux by `tools/dr_linux_root.sh transform <sf-roinit>`, which identifies the partition three ways and touches nothing else; `e2fsck -fn` clean afterwards): `/var`, `/private/var`, `/private/tmp`, `/etc/resolv.conf` are links into `/private/ram` (originals kept as `*.disk`); `/usr/bin/darwin-desktop-test`, which the bench types, runs the boot's own script from the memory disk (original kept as `.staged`). Reference digest of the transformed tree: `runs/m1-storage/ref-tree-diskroot.txt`.
- **No read of an owner's partition** (reviewer's condition): guarded partitions lose their content hint when the guard attaches, and with `pdnvme_readfence=1` the block device serves only the GPT's own blocks and, once identified, our partition; any other read is refused before a command is built. `kern.pd_nvme.fence` counts requests by where they lie. Range hashes go through `/dev/pdnvme0`, which the fence does not concern. **The fence is on in every boot from here.**
- **The end of a boot:** nothing can be read from the root once the controller stops, so `pdnvme-test stopreport` (one process, in memory, its report rehearsed once before the stop) stops the controller, reports and prints the bench's end marker. The driver's time to live remains the backstop.
- Commands: shell profile `BENCH_WHO=disk NV_VERDICT_MODE=root NV_RANGE_FILE=runs/m1-storage/linux-c/ranges NV_TTL=240 NV_RUNTIME=420 tools/bench.sh run integrations/puredarwin/m1-storage/tools/nv_slot.sh drNN 3 any`; desktop `BENCH_WHO=disk tools/bench.sh run integrations/puredarwin/m1-storage/tools/dr_desk_slot.sh drNN hv|native 240` (collection `build-kc-native.nix` = the native collection + PDAppleNVMe, `/nix/store/lpa9g137…` when dr06 to dr09 ran; `DR_BOOTARGS=pdnvme_delay=0` for the timeline). QEMU: `DR_QEMU_SINGLE=1 DR_QEMU_KERNEL=debug DR_QEMU_BOOTARGS="rd=uuid boot-uuid=<partition GUID in capitals> launchdsuffix=sfro wdt=-1 -s" DR_QEMU_DISK2=<state image> tools/dr_qemu.sh <name> guest/qemu-root-ro.sh <disk> ro`.

Results (every verdict 41 of 41 unless said):
| Boot | What | Result |
|---|---|---|
| dr03 | first attempt, `boot-uuid` = partition GUID | no root found; controller stopped cleanly by its time to live at 240 s; Linux and all stable ranges unchanged; STOP set aside on the reviewer's word |
| dr04 | shell, hypervisor | rooted on `disk0s3` read-only; 39 ok, 2 failures of the tree comparison: the `libSystem.B.dylib` in use reads back with 4 bytes changed in the page cache (`0x1f` to `0xff` at four offsets; the same digest in QEMU on a writable root). Comparison corrected (that file's content is compared separately) |
| dr05 | shell, hypervisor | **passed**: root read-only on our partition, memory disk on `/private/ram`, writes to `/var`, `/tmp`, `resolv.conf` succeed, creates in `/etc`, `/usr`, `/`, `/private` and a remount read-write refused; the whole root tree as the running system sees it (9,232 files) equals the transformed source image; fence: before the deliberate test `refused=0 other=0`, the deliberate read of `p2` through `/dev/rdisk0` refused (`Operation not permitted`); about 12,000 requests, 67,366 blocks, all inside our partition; one superblock write tried by ext4 at the root mount and refused by IOMedia; `write_refused=0 gate_rejected=0`, SMART write counters +0; stop from memory, report complete; Linux clean, 24 of 24 stable ranges unchanged |
| dr06 | desktop, hypervisor | **passed**, and the desktop from the disk: GL on KMS 2560x1600, 10 flips, 0 missed, Wi-Fi join and ping, Touch Bar ready, coreaudiod (`hold_probe=1`), keyboard and trackpad, both terminals, clean end |
| dr07 | desktop, native | storage verdict passed; the compositor came up on the software renderer: driver log lines on the 115200-baud UART delayed the display driver (the native stream's `noiolog` word was missing) |
| dr08 | desktop, native, `noiolog` | **passed**: GL on KMS, 10 flips, 0 missed, Wi-Fi, Touch Bar, coreaudiod, input, terminals, no hypervisor |
| dr09 | the same with `pdnvme_delay=0` | **passed** (41 of 41, GL on KMS): the driver's 3 s wait before starting the controller is not needed |

Seconds from the kernel's start (`m1-native-boot/tools/nb_times.py`; typed sessions; the driver waited its default 3 s before starting the controller in these boots): dr06 (hypervisor) launchd 4.97, shell 5.38, script 8.38, first frame 10.18; dr08 (native) launchd 5.17, shell 5.38, script 10.56, first frame 12.82. dr09 (native, `pdnvme_delay=0`): launchd 2.04, shell 2.35, prompt 4.95, command typed 6.11, script 7.33, first frame 9.69, Wi-Fi joined 12.60 (the 5 s between the shell and the script are the bench typing at a 115200-baud console; a launchd-started session does not have them). RAM-root references (SF-T): first frame 7.27 (hypervisor), 8.67 (native, typed), 4.91 (native, launchd session). The RAM root is still uploaded in these boots, so the saving of the upload shows only in step 4.

Known and open: the ext4 driver writes the superblock at every mount, read-only or not (refused here by IOMedia): step 3 must either make a read-only mount truly read-only or have those writes inside the fence and correct. The in-use libSystem's four patched bytes are in the cache: step 3 must show they are not written back to a writable root. A session started by launchd (multi-user) from the disk root is not tried.

### Step 3: the write-fenced driver
**Done, host only**
- `kernel/pd_nvme_wgate.c/.h` (compiled into no kext yet; a source check keeps it out of the read-only one): `pd_nvmew_gate()` = the read-only gate unchanged, plus on the I/O queue Write (namespace 1, at most 256 blocks, every block inside the fence `[first, last]`, no FUA / protection information / directive / hints / fused / SGL / metadata pointer) and Flush (namespace 1, nothing else in the command), both only while the fence is open; `pd_nvmew_inside()` is the second, independently written comparison for the doorbell.
- `host-test/wgate_host.c` (run by `run-host-tests.sh`, `WGATEHOST PASSED`), fence = our partition's real range: first and last block; one before and after; 256 blocks at each end; crossing either end by one and by half; every other partition's first block, both GPT copies, the sliver after ours, past the disk; LBA 2^64-1, a wrap to 0, our first block with the high word set; count field 0, 257 and 65536 blocks; namespaces 0, 2, 3, 4 and broadcast for write and flush; FUA, limited retry, protection information, directive, dataset hints, tags, SGL, fused, metadata pointer, reserved word; flush with a data pointer or a command word; fence closed, absent, inverted, past the namespace, "everything", one block; every opcode on the I/O queue (exactly 0x00, 0x01, 0x02 admitted), the admin queue identical to the read-only gate in 512 of 512 shapes, no other queue; 20 million random commands (half aimed at the fence's edges) against an independent statement of the rule: 1,508,780 writes admitted, all inside by both looks.
- QEMU, the tree's ext4 driver on a writable 4096-byte-sector GPT disk (`guest/qemu-rw.sh`, phases a/b/c through `tools/dr_qemu.sh … rw`; images under `runs/m1-storage/qemu-dr/`):
  - **clean path**: read-write mount, files written (20 MiB in 65 s under TCG), a file deleted, unmount: Linux `e2fsck -fn` clean, every file's hash as written.
  - **unclean stop** (machine stopped with the filesystem mounted read-write, some files synced, some not): the superblock has `needs_recovery`, the journal is dirty; Linux `e2fsck -fy` says only "recovering journal" and is clean; **Darwin's own replay at the next mount gives the identical tree digest as Linux's replay** (9,236 files) and leaves the filesystem clean for `e2fsck -fn`. Synced and unsynced files were all present and intact in both.
  - the in-use `libSystem.B.dylib` (reads back with 4 patched bytes while the system runs) is **not written back**: on a writable root the file on disk keeps its digest.
- Facts the design must use: every mount, read-only too, writes the superblock (`needs_recovery` on) and every unmount writes the journal superblock and the superblock (off); a root filesystem cannot be unmounted, and `ext4_mount`'s update path does nothing on a downgrade to read-only, so "remount read-only, then stop the controller" does not yet leave a clean filesystem: either the driver gets that downgrade (checkpoint, clear the flag, flush: a tree patch to `ext4_vfsops.c`/`ext4_jbd.c`, built as a replacement member of our collection only), or every boot with a read-write root ends in a replay at the next mount (shown correct above, but the reviewer asked for the clean path as well).

**The write build (3a) and the first fenced-write boot (3b, dr11)**
- Sources: the read-only driver's files are not edited. `kernel-rw/rw.patch` is applied to copies of `pd_ans.h`, `pd_ans.cpp`, `PDAppleNVMe.cpp` (`tools/rw_src.sh make <dir>`; edit the copy, then `tools/rw_src.sh refresh <dir>`), plus `kernel-rw/pd_nvme_wring.c/.h` (plain C: fence opening with the compiled-in range 192,633,862 / 16,166,912 (16,777,216 until `18490a9b6`), the fenced device's block arithmetic, the data direction, and everything between a command and the doorbell). Recipes: `build-kext-rw.nix`, `build-kc-rw.nix` (`--arg native true` for native boots). The class names stay `PDAppleNVMe…`; the bundle is `org.puredarwin.driver.PDAppleNVMeRW`.
- A write passes: `pd_wabs_ok` before anything is queued (`pd_write`) and again before the command is built (`submitWrite`); `pd_nvmew_gate` on the command; copy into the slot; `pd_nvmew_gate` on the slot; direction (host-to-device only for opcode 0x01 on the I/O queue); `pd_nvmew_inside` on the slot; the TCB; `pd_nvmew_inside` once more in `ring()` directly before the one doorbell statement. A refusal zeroes the slot and counts (`gate_rejected`, `fence_rejected`).
- The fence opens in one place: after `pd_part_verify` passed, with `pdnvme_write=1`, and only if the three partition arguments equal the compiled-in range. It can be closed (`kern.pd_nvme.seal=1`: Flush, then closed for the rest of the boot; the stop path does the same), never reopened. Without `pdnvme_write=1` the build behaves as the read-only one.
- In write mode: the whole disk stays write-protected and guards all 8 partitions, ours too (census `guarded=8 … ours=1`), its read fence leaves only the GPT; the second device `pdnvme0w` (block 0 = our first block) is the only way to our partition, its media is `disk1` (`kern.pd_nvme.wstats` names it), synchronize = Flush behind a barrier in the engine. `kern.pd_nvme.stop` answers EBUSY while that media is open and the fence not sealed; the stop path (the time to live too) sends Flush before the shutdown notification. `kern.pd_nvme.wprobe="<lba> <blocks>"` asks the range check and has no data path. `kern.pd_nvme.wgate_selftest`: 4/13/0 open, 0/17/0 closed.
- Host tests (`host-test/run-host-tests.sh`): as before plus `WRINGHOST` (98 cases and 4 million random writes at the edges; includes a slot altered after the copy and after both gate calls) and 17 source checks on the write build.
- Collection room: the write kext is 7 KB larger and crosses a 16 KiB boundary. Every boot kext is on the launcher's required-member list (dr10 was refused on the build host for leaving two of them out; nothing booted), so `build-kc-rw.nix` leaves `PDAppleAVD` (inert without `pdavd=1`) out of the write collections only. `/nix/store/kinfsnzg…` (hypervisor), `/nix/store/j12x8mcd…` (native).
- Slot command: `BENCH_WHO=disk NV_VERDICT_MODE=write NV_RANGE_FILE=runs/m1-storage/linux-c/ranges NV_TTL=300 NV_RUNTIME=420 tools/bench.sh run integrations/puredarwin/m1-storage/tools/nv_slot.sh drNN 3 any` (guest `guest/darwin-disk-rw.in`; afterwards `tools/dr_linux_root.sh wcheck` from Linux, `DR_REPLAY=1` where an unclean stop was intended).
- **dr11** (hypervisor, RAM root, 8 CPUs): fence open over exactly our range; 22 range probes (first and last block admitted; one before, one after, across either end, block 0, the GPT header, first and last block of each of the 7 owner partitions refused), nothing built (`writes=0 flushes=0`); opens for writing refused on six whole-disk nodes; reads of `p2` and of ours through the whole disk refused; a write at the block after the device's last refused by the media layer. ext4 read-write on `/dev/disk1`: 34.7 MB in 2 s plus three small files, 8,586 write commands (17,039 blocks), 12 flushes (8 ms average, 18 ms worst), write latency at most 213 us; a stop while mounted refused; unmount; read-only mount: the four files hash as written. `fence_rejected=0 range_refused=0 gate_rejected=0`. After the writes 31 of 31 ranges outside our partition hash as before, inside Darwin. Stop: Flush, fence closed, clean. Linux: 21 of 21 stable ranges and 6 of 6 untouched Linux ranges unchanged, no recovery line, `unsafe_shutdowns` 107 → 107, `e2fsck -fn` of p3 clean, superblock clean without `needs_recovery`, the four files with Darwin's SHA-256. Verdict 68 ok, 1 FAIL: the first `NV_WSTATS` line was cut by a kernel log line on the console (the readings after it are complete and 0); the guest now prints each reading twice. STOP files kept as `dr10-STOP-preboot-member-gate.txt`, `dr11-STOP-checker-console-interleave.txt`.
- The test files stay on p3 under `/private/sf-wtest` (remove them when the root is rearranged for 3d).

**Clean-stop ordering and the unclean case (3c, boots dr12 to dr16)**
- `ext4-downgrade.patch` (tree patch against the tip; `build-ext4.nix`; in `build-kc-rw.nix` it replaces the tree's `ext4`): `mount -t ext4 -o update,rdonly <device node> <mount point>` (this `mount` has no `-u`, and wants the node, not `root_device`) pushes file data, commits and checkpoints the journal, marks it empty, clears RECOVER, flushes and sets `em_frozen`; after that the driver writes nothing (`ext4_meta_bwrite`, `ext4_jnl_log_block`, the write vnop answer EROFS) until an upgrade thaws it. **Trap found by dr14:** an update reaches the filesystem with `MNT_RDONLY` already set and `cluster_pageout()` discards the dirty pages of a read-only mount, so unsynced files kept their size and lost their content; the patch lifts the flag for the push. QEMU rehearsal: `build-qemu-image.nix` (`QD_BASE_IMAGE=<result>/*.img`), `guest/qemu-downgrade.sh` phases `m` (a mount) and `r` (the root); always compare every file's hash on the host (`debugfs cat | sha256sum`), not only `e2fsck`.
- End of a boot with a read-write filesystem: sync; downgrade; `kern.pd_nvme.seal=1` (Flush, fence closed; `sealed_refused` then counts anything that still tries); `kern.pd_nvme.stop=1` (refused before the seal while the media is open).
- Slots: `NV_WMODE=unclean|downgrade` (guest `guest/darwin-disk-rw2.in`), `DR_REPLAY=1` for Linux's replay, `NV_PREV_CONSOLE=<console.bin of the previous boot>`.
- Collections: six boot kexts that match only other boards (`PDSg2002Eth`, `PDSg2002SD`, `PDSun60iDisplay`, `PDQcomGeniI2C`, `PDQcomTLMM`, `PDPowerVR`) are left out and every default member is in (the video decoder too); `launcher-m1-members.patch` (per boot, `PCIE_LAB_PATCH`; `nv_slot.sh` and `dr_desk_boot.sh` add it) removes exactly those names from the launcher's required list. The coordinator will make that the default; the generic kexts (virtio, ECAM, AHCI/XHCI, I2C-HID) stay.

| Boot | What | Result |
|---|---|---|
| dr12 | unclean end (mounted read-write, seal, stop); Linux replays | passed 61/61: `needs_recovery`, journal start 1; `e2fsck -fy` then `-fn` clean; synced files hash-equal; 20 writes after the seal refused; unsynced files absent |
| dr13 | unclean end, journal left for Darwin | passed 60/60 (the slot script stopped after the boot because I had edited it while it ran; the Linux reading, check and verdict were run by hand under the bench lock) |
| dr14 | Darwin mounts after dr13; downgrade, seal, stop while mounted | 69 ok, 1 FAIL: clean on disk, nothing written after the seal, but the file written just before the downgrade without a sync was all zeros (the trap above). STOP kept as `dr14-STOP-downgrade-lost-unsynced-data.txt` |
| dr15 | unclean end again | passed 60/60 |
| dr16 | Darwin mounts (`journal dirty (start=1 seq=17): replaying`), dr15's files hash-equal; write; downgrade with the fix; seal; stop while mounted | **passed 71/71**: superblock clean without `needs_recovery`, journal empty, `e2fsck -fn` clean, all four files (the unsynced one too) hash-equal from Linux, `sealed_refused=0`, no write command after the downgrade |

Every one of them: LINUX_VERIFIED, 21/21 stable ranges and 6/6 untouched Linux ranges unchanged, `unsafe_shutdowns` 107, `gate_rejected=0 fence_rejected=0`, controller stopped cleanly. Never edit a script while a slot that uses it runs (bash reads it as it goes).

**Root read-write on the partition (3d, boots dr17 to dr21)**
- How it is put together: `rd=uuid boot-uuid=<filesystem UUID> wdt=-1 pdnvme=3 <three partition arguments> pdnvme_write=1 pdnvme_readfence=1`, no `launchdsuffix` (process 1 is launchd). The whole-disk view of our partition is guarded in write mode, so the driver itself publishes the fenced media as `boot-uuid-media` when the ext4 superblock in the fence's first block carries the `boot-uuid` (`fenced_root_thread` in `rw.patch`); the root is then `disk1`, read-write from the start.
- The partition was rearranged from Linux with `tools/dr_linux_root.sh untransform` (`tools/sf-root-untransform.sh`): the `*.disk` originals are back in place, the links kept as `*.rolink` (the way back is `transform`), `/private/sf-wtest` removed, `/usr/bin/darwin-desktop-test` now mounts the RAM root image (`/dev/md0`) read-only on `/private/ram` itself and runs the boot's script from there. The RAM root is still uploaded in these boots (script, `pdnvme-test`, range list).
- Boot: `BENCH_WHO=disk DR_RW=1 [NV_PREV_CONSOLE=<previous console.bin>] tools/bench.sh run integrations/puredarwin/m1-storage/tools/dr_desk_slot.sh drNN hv|native 240` (collection `build-kc-native-rw.nix`; guests `guest/diskroot-rw-prelude.sh`, `guest/darwin-diskroot-rw.in`; verdict `NV_WMODE=root`). End of the boot: sync; `mount -t ext4 -o update,rdonly /dev/disk1 /`; `seal`; `pdnvme-test stopreport` from memory.
- **No clock (found by dr17):** this machine has no time source at boot, so the time is seconds since the kernel started (1970). e2fsck reads a deletion time below the inode count as an orphan-list pointer and reports the filesystem damaged (`Inode 29 was part of the orphaned inode list`, rc 4; nothing else was wrong). `ext4-downgrade.patch` now never dates a deletion before the filesystem's own `s_mkfs_time`/`s_wtime` (QEMU: clock set to 1970, files deleted, downgrade, host `e2fsck -fn` clean, five files hash-equal). File timestamps are still 1970 until something sets the clock: **step 4 needs a time source** (NTP once Wi-Fi is up, or an RTC driver).
- The desktop keeps its Theme/Display choices in `/private/var/db/desktop-state` (`XDG_STATE_HOME`); an unattended session writes nothing there and did not take a request left in `appearance-request`. The persistence test therefore writes `appearance=dark` where the Theme menu writes it.
- `/private/var/run/gpu-desktop` is removed by the prelude before the session: a real boot needs that cleanup (stale sockets and locks of the previous boot are on disk now).

| Boot | What | Result |
|---|---|---|
| dr17 | desktop, hypervisor, root read-write | desktop up from the disk (GL on KMS 2560x1600, Wi-Fi, ping, Touch Bar, audio, input); 69 ok, 1 FAIL: `e2fsck -fn` rc 4, the clock trap above. Repaired from Linux (`e2fsck -fy`, one inode's deletion time). STOP kept as `dr17-STOP-e2fsck-low-dtime-no-clock.txt` |
| dr18 | the same with the dated-deletion fix | **passed 72/72**; dr17's file found hash-equal |
| dr19 | native (`noiolog`, `vdm-serial`) | **passed 72/72**; GL on KMS, 10 flips; files of dr17 and dr18 found |
| dr20 | hypervisor; `appearance=dark` stored | **passed 72/72** |
| dr21 | native, `pdnvme_delay=0` | **passed 72/72**; `appearance=dark` and four files of earlier boots found hash-equal |

Every one: root on `disk1` read-write; writes to `/var/run`, `/tmp`, `/private/var/tmp`, `/etc`, `/usr` succeed; downgrade rc 0, `sealed_refused=0`, no write command after the downgrade; Linux: `e2fsck -fn` clean, no `needs_recovery`, journal empty, persistence files and the desktop state hash-equal, the in-use libSystem not written back; 21/21 stable ranges and 6/6 untouched Linux ranges unchanged, `unsafe_shutdowns` 107, `gate_rejected=0 fence_rejected=0`, LINUX_VERIFIED. A desktop boot issues about 1,700 write commands (7 MB) and 290 flushes.

Seconds from the kernel's start (`nb_times.py`; typed sessions; the RAM root is still uploaded, so the saving of the upload shows only in step 4):

| | dr09 native, read-only root, no delay | dr17 hv, read-write | dr19 native, read-write | dr21 native, read-write, no delay |
|---|---|---|---|---|
| launchd | 2.04 | 4.90 | 5.06 | 2.13 |
| shell | 2.35 | 5.61 | 5.37 | 2.54 |
| script | 7.33 | 9.04 | 11.60 | 8.66 |
| first frame | 9.69 | 11.10 | 14.07 | 11.12 |
| Wi-Fi joined | 12.60 | 14.22 | 16.77 | 13.84 |

The read-write root costs about 1.4 s to the first frame against the read-only one (the journal: 290 flushes of about 8 ms are a good part of it; Linux's driver rate-limits flushes on this controller, ours does not yet).

### Step 3e: the clock, the system's own shutdown and restart, settings that last (dr22, dr23)
What a person needs from a root on disk that the bench script had been doing or nobody did. Collection `build-kc-native-rw-halt.nix` (`build-kc-rw.nix` with `halt = true`: the write collection with `m1-native-boot/reboot/build-cpupower.nix` as PDCpuPower; `/nix/store/nilcvnwn…`), native boots only.

**Clock (hardware clock, read only).** `m1-native-boot/reboot/PDAppleReboot.cpp` reads, once the thermal thread's SMC session is open, SMC key `CLKM` (48-bit counter, 32768 counts a second) and the six offset bytes Linux keeps at `0xd100` of the PMU (SPMI extended read), and computes the time as Linux `rtc-macsmc.c` does (`pd_halt_gate.h` `pd_rtc_time`, host-tested: `cc -I.. test/test_halt_gate.c`). It offers it as `kern.pd_rtc_usec` and `kern.pd_rtc`; `pdrtc=0` turns it off. **Nothing is written**: not the offset, not the SMC, not NVRAM; setting the clock under SiliconForest lasts until it stops (`date` works, the hardware clock stays Linux's). `/usr/sbin/sf-clock set` (called by `/sbin/sf-boot`) waits for the value and calls `settimeofday`. `pdthermal_delay=4` is in these boots' arguments: PDCpuPower's first SMC access is otherwise 20 s of machine uptime (which counts from power-on, so in a RAM-loaded boot it had passed before the kernel started: the clock was read at 36.9 s of uptime, about 12 s before the session script ran). In an installed boot chain the kernel starts earlier and the delay decides when the clock is known: `sf-clock` waits up to 30 s for it. Not looked into: whether PDCpuPower's early SMC session can collide with the PCIe bridge's radio-power session (it did not in two boots; Wi-Fi joined at the first attempt in both).

**Shutdown and restart.** `/sbin/sf-halt restart|poweroff` (`system/sf-halt.c`): away from the caller's terminal; SIGTERM to every process (5 s), SIGKILL to the rest (launchd and the console's own shell are left: when the console's session leader dies the kernel revokes the console, and in the QEMU rehearsal the halt then never finished); sync; `mount -t ext4 -o update,rdonly <root node> /`; `kern.pd_nvme.seal=1`; `kern.pd_nvme.stop=1` and a wait for `stopped`; `reboot(2)` with `RB_NOSYNC|RB_QUICK`. Everything it needs after the stop is run once before it. If the seal or the stop fails it does not reset the machine. `/bin/reboot`, `/bin/halt`, `/bin/poweroff`, `/bin/shutdown -r|-h now` are `system/sf-command` (the tree's programs are kept as `*.xnu`: they call `reboot(2)` with the root read-write and the controller running). The kernel's part is `m1-native-boot/reboot` as written by the native-boot stream (Linux `macsmc-reboot` ported), **booted here for the first time**: `PE_halt_restart` writes SMC `MBSE`=`rest`, `NTAP`=0, `MBSE`=`phra`. New: every one of those writes passes `pd_smc_halt_gate()` (`pd_halt_gate.h`; allowlist: key `MBSE` with value `rest` or `phra`, and `offw`/`off1` only with `pdpoweroff=1`; key `NTAP` with one byte 0; anything else refused and counted in `kern.pd_smc_halt_writes`). **PMU: nothing is written** (`pdpmu=1`, the default: the three boot-state bytes are read once at start). `pdpmu=2` (Linux's writes of `0x9f01`, `0x9f02`, `0x9f0f` through `pd_pmu_write_gate`) and a power-off (`pdpoweroff=1`) have never been booted; `sf-halt poweroff` refuses unless `pdpoweroff=1` is among the boot arguments. Linux after both self-restarts: no `PMU logged … boot error(s)` line at all (after a controller reset it logs 1).
In the desktop menu: Session > Restart and Session > Shut Down (compositor commit `5ccff8a`, local): the row writes `restart` or `poweroff` to `$XDG_RUNTIME_DIR/session-end-request` and ends the session as SIGTERM does; whoever started the session runs the command (`guest/darwin-diskroot-halt.in` does; a session script of the system has to: see step 4).

**At boot.** `/sbin/sf-boot` (once a boot, by `kern.bootsessionuuid`; called first by the disk's `/usr/bin/darwin-desktop-test`): `sf-clock set`, then everything in `/private/var/run` and `/private/tmp` is removed (not when something is mounted below them).

**Settings.** The menu's Theme and Display rows keep one file each in `$XDG_STATE_HOME/chonkstep/` (`theme`, `scale`, `wallpaper`), `XDG_STATE_HOME` = `/private/var/db/desktop-state` (the session script's). (dr20's `appearance` file in the directory above is not what the menu writes.) Nothing prevented persistence; it had never been exercised. The test drives the menu with the compositor's autopilot (synthetic pointer records through its own input decoder; `CHONKSTEP_DARWIN_AUTOPILOT_KEEP=1` picks another theme and scale and leaves them, `…_END=restart` picks Session > Restart; both new, same commit).

**Installed on p3 by dr22, through the fenced driver** (`system/sf-install.sh`, run by the boot's script from the RAM root image; nothing was written to p3 from Linux in this step): `/sbin/sf-halt`, `/sbin/sf-boot`, `/usr/sbin/sf-clock`, `/bin/{reboot,halt,poweroff,shutdown}` (originals as `*.xnu`), `/usr/bin/darwin-desktop-test` (`system/darwin-desktop-test.disk`), `/usr/bin/chonkstep-darwin-desktop` (build `wm-darwin-keep-build-003`, sha256 `e5489e51…`). **p3 now holds theme `graphite`, scale 150 %, wallpaper `graphite-fold` as the desktop's stored choice.**

Commands: `BENCH_WHO=disk DR_RW=1 DR_HALT=reboot|"shutdown -r now"|menu [DR_INSTALL=1 DR_COMPOSITOR=<file>] [DR_AUTO_ONLY=settle,settings [DR_AUTO_KEEP=1]] DR_SECONDS=60 NV_PREV_CONSOLE=<previous console.bin> tools/bench.sh run integrations/puredarwin/m1-storage/tools/dr_desk_slot.sh drNN native 240`. The launcher gets `reboot/launcher-self-reboot.diff` on top of the native patch (first use on hardware): the guest's `PD_BENCH_REBOOT` line ends the capture 10 s later and the controller waits for a Linux that comes back by itself instead of resetting. Verdict `NV_WMODE=halt` (`nv_verdict.py`): the write-mode root checks plus the clock, the file time, the halt, the reset count and the look of the next session. QEMU rehearsal of install, boot clean-up and both commands on a read-write root: `tools/dr_qemu_halt.sh <name>` (the virt machine does not reset on `reboot`: end its QEMU by hand once the log says `MACH Reboot`; `debugfs ls` shows removed directory entries with inode 0, a Linux mount does not).

| Boot | What | Result |
|---|---|---|
| dr22 | native; tools installed; clock; autopilot picks theme Graphite and scale 150 % in the menu; ended by `reboot` | **passed 84/84**. `SF_CLOCK set utc=1794531841 waited_ms=0 before=49 source=hardware-clock`; Darwin's `date` against the arrival of its line at the controller: -0.8 s at the start, -0.7 s at the end; `dr22-time.txt` mtime 1794531843 in Darwin and in Linux; `SF_HALT downgrade: mount rc=0, root is now read-only`, `SF_HALT stop errno=0 state=stopped-clean`, `Quick reboot...`, `MACH Reboot`; `reset_attempts` 0; Linux: `e2fsck -fn` clean, state clean, no `needs_recovery`, journal start 0; state files `theme=graphite scale=180` |
| dr23 | native; nothing installed (the disk's own dispatcher ran `sf-boot`); autopilot picks Session > Restart; the script runs `shutdown -r now` | **passed 87/87**. `SF_BOOT clock_rc=0 run_and_tmp=cleaned removed=2`; `WMSCALE initial scale=150% scale120=180 source=persisted … theme=graphite theme_source=persisted`; the settle frame read back from the renderer equals the CPU render (`titlebar=1309x35`; at 200 % in dr22: `1796x46`); `WMSET session_end=restart`, `NV_SESSION_END request=restart`; clock -0.9 s / -0.5 s; `reset_attempts` 0; Linux as for dr22 |

Both: LINUX_VERIFIED, 21/21 stable ranges and 6/6 untouched Linux ranges unchanged, `unsafe_shutdowns` 107, `gate_rejected=0 fence_rejected=0 sealed_refused=0`, no write command after the downgrade, `kern.pd_smc_halt_writes` and `kern.pd_pmu_writes` at `sent=0 refused=0` before the restart (the three SMC writes of the restart itself cannot be read back: the machine is gone).

Not proven, and warts: one process outlives SIGTERM in every halt and costs the 5 s wait (not identified; `sf-halt` does not name what it kills); power-off; `pdpmu=2`; the hypervisor (these boots are native: the self-restart rule for the hypervisor's console was not exercised); a halt from a launchd-started session; `halt -n`-style options (the wrappers take `reboot`, `halt`, `poweroff`, `shutdown -r|-h|-p [now]` only); the files written by boots before dr22 keep their 1970 times; `pdnvme_ttl` still stops the controller after its time (200 s in these boots): a session longer than that needs `pdnvme_ttl=0`, which no boot has had.

### The shrunk partition (dr24, 12 November 2026)
The three write collections were rebuilt with the fence constant 16,166,912: `/nix/store/lrrqbhkg…` (hypervisor, `build-kc-rw.nix`), `/nix/store/kafwjsds…` (native), `/nix/store/zz2hfjnr…` (native with restart and clock, `build-kc-native-rw-halt.nix`; replaces `nilcvnwn…`). Only the last one has booted. The older store paths named in this document (`kinfsnzg…`, `j12x8mcd…`, `nilcvnwn…`) carry the old range: never boot them with the disk root.
New in the tools: `runs/m1-storage/linux-c/ranges` (see "Commands" above); the halt guest (`guest/darwin-diskroot-halt.in`) asks the driver's range check about the fence's ends with the root mounted (`kern.pd_nvme.wprobe`, no data path: first block, last block, the last 256 blocks; one before, one after, across the end, the partition's last block before the shrink, the last unallocated block, block 0) and `nv_verdict.py` checks the answers in halt mode.

**dr24** (native, `DR_RW=1 DR_HALT=reboot DR_SECONDS=60`, collection `zz2hfjnr…`, `NV_PREV_CONSOLE` = dr23's): **passed 87/87.**
- `NV_PART verified first=192633862 blocks=16166912 exempted=0: found in both GPT copies`; `NV_MEDIA published=1 partitions=8 guarded=8 foreign=0 ours=1`.
- fence `first=192633862 last=208800773`; probes: `in-first lba=192633862 rc=0`, `in-last lba=208800773 rc=0`, `in-last-256 lba=208800518 blocks=256 rc=0`; `out-after lba=208800774 rc=1`, `out-across-end lba=208800773 blocks=2 rc=1`, `out-old-last lba=209411077 rc=1`, `out-freed-last lba=209411118 rc=1`, `out-before lba=192633861 rc=1`, `out-block0 rc=1`. 1,902 write commands (7.8 MB), 309 flushes, `fence_rejected=0 range_refused=0 sealed_refused=0 gate_rejected=0`.
- the four ranges in the unallocated space (208,800,774; 209,105,920; 209,410,822; 209,411,078) hash the same in Linux before, in Darwin before and after its writes, and in Linux after; 24 of 24 ranges Linux never writes unchanged; 34 of 34 ranges outside the partition unchanged inside Darwin after its writes; 6 of 6 untouched Linux ranges equal.
- the nine files of dr17 to dr23 under `/private/var/sf-persist` and the desktop's four state files found hash-equal: the shrink lost nothing. The session came up with the stored look (`WMSCALE initial scale=150% … source=persisted … theme=graphite theme_source=persisted`), GL on KMS, Wi-Fi joined, ping answered, Touch Bar ready.
- end by the system's own `reboot`: `SF_HALT downgrade: mount rc=0, root is now read-only`, `SF_HALT stop errno=0 state=stopped-clean`, `Quick reboot...`, `MACH Reboot`; `reset_attempts` 0, `DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED`; clock -0.9 s / -0.5 s.
- Linux afterwards: `e2fsck -fn` rc 0 (`10192/4046848 files, 465248/16166912 blocks`), state clean, no `needs_recovery`, journal start 0; `unsafe_shutdowns` 107 → 107.
Not booted on the new layout: the hypervisor and the plain native write collections, the read-only collections with the three partition arguments (they take the length from `sf-part.env`; nothing is compiled in), an unclean end.

### Step 4: the stage boots the desktop from the disk root, nothing else uploaded (sfb10, dr25 to dr27)
**What is new**
- `system/sf-session` (on the disk: `/usr/libexec/sf-session`): the desktop session of a boot without a RAM root. `m1-boot-speed/guest/darwin-gpu-desktop-fast.in` without the typed command and without credentials (the network is left to pd-netd's own launchd job), honouring the menu's `session-end-request`, ending in `/bin/reboot` (`/bin/shutdown -h now` for a power-off request, a restart if that is refused). The launchd job is `m1-native-boot/guest/org.siliconforest.desktop.plist` (on the disk in `/System/Library/LaunchDaemons`; it runs the dispatcher `/usr/bin/darwin-desktop-test`, which runs `sf-boot` and then, with no RAM root, the session). Boot arguments it reads: `sfsession_seconds=<11..14400>` (default 14400: the compositor's own bound), and for the bench `sfbench=<name>` and `sfbench_auto=<phases>`: only then, and only when `/private/var/db/sf-bench` holds them, the bench's prelude and end script run (templates `prelude.in`, `end.in` = `guest/diskroot-halt-prelude.sh` and `guest/darwin-diskroot-halt.in` reading from that directory instead of the memory disk, `tools/dr_bench_templates.sh`; filled in at boot from the boot arguments), with `pdnvme-test`, the range list and the untouched libSystem beside them. A boot without `sfbench=` runs nothing of the bench.
- `system/sf-install.sh` installs the session, the job and (when the boot carries them) the bench's pieces. `tools/dr_desk_boot.sh DR_INSTALL=1` carries them.
- The disk-root variant of `b1/tools/sf_stage_boot.sh` is `tools/dr_desk_boot.sh` with `DR_STAGE=1` (stage image with the RAM root record, typed session: written, **not booted**) or `DR_STAGE=disk` (no RAM root record, multi-user, the disk's own session), and `DR_STAGE_STOP=1` for a read-back boot; `tools/dr_desk_slot.sh` is the slot around all of them (a read-back boot gets the checks named in its header instead of the verdict). `sf_stage_boot.sh` itself is unchanged and still refuses `pdnvme` arguments. Launcher: word `noramdisk` (`native_boot.py`, `b1/native_stage.py`: the image is stage + configuration + collection; refused unless the command line has `rd=uuid`), and an untyped session that restarts the machine itself ends the capture ten seconds after its `PD_BENCH_REBOOT` line. Host tests: `b1/test_stage_launcher.py` (8).
- The stage image in `b1/out/` is now built with `B1_ROOT_UUID=<the filesystem UUID>` (sha256 `edaaab16…`; never committed). `dr_desk_boot.sh` refuses a stage image that does not carry this root's UUID.
- QEMU rehearsal: `tools/dr_qemu_session.sh <name>` (install typed, then a multi-user boot whose launchd job runs the session and ends by `reboot`; host: `e2fsck -fn` clean, state clean, journal start 0 after both). **Trap:** after the guest's `MACH Reboot` QEMU hangs and ignores SIGTERM; `qd_run.py` ends it at its timeout, or end that one QEMU by its exact PID with SIGKILL after checking its command line.

**Boots**

| Boot | What | Result |
|---|---|---|
| sfb10 | stage with the root rule of `b6558ea92`, read-back, RAM root (`SF_WORDS="vdm-serial hostseed stagestop"`, tip native collection) | `PDSF: root: the RAM root of this image (rd=md0)`; `STAGE_READBACK … "equal_to_the_launchers": true`, eight `"equal": true`, `addresses_as_the_launcher_predicts` all true; `PDSF: stop requested: not entering XNU`; no kernel banner; `NO_COMMAND_LINE_OBSERVED_LINUX_VERIFIED`, 1 reset |
| dr25 | launcher-driven as dr24, `DR_INSTALL=1` (compositor `wm-darwin-keep-build-003`, the one already there) | **passed 87/87**; `SF_INSTALL bench: end.in:6552 libSystem.B.dylib.ref:4338830 pdnvme-ranges:487 pdnvme-test:35720 prelude.in:5032`, `SF_INSTALL done: … usr/libexec/sf-session … System/Library/LaunchDaemons/org.siliconforest.desktop.plist`, `NV_SFINSTALL rc=0`; restart by `reboot`, `reset_attempts` 0; Linux `e2fsck -fn` clean |
| dr26 | `DR_STAGE=disk DR_STAGE_STOP=1`: read-back of the stage image without a RAM root | **passed** (`DR_STOP PASSED`): records `config`, `kernel` only (14,423,732 bytes); `PDSF: root: the disk filesystem this stage was built for (rd=uuid)`; `PDSF: boot seed from the SEP`; boot-args page equal to the launcher's, kernel image, trust cache and SEP firmware copies equal, `"no_ramdisk": {"memory_map_names_a_ramdisk": false}`; kernel not entered; Linux: `e2fsck -fn` rc 0, state clean, journal start 0, 7 of 7 ranges in our partition and the free space behind it unchanged; 1 reset |
| dr27 | `DR_STAGE=disk DR_SECONDS=60`: **the stage boots the desktop from the disk root** | **86 ok, 1 FAIL** (below). `SF_SESSION begin seconds=60 bench=dr27 started_by=launchd`; `NV_EARLY_ROOT disk read-write: root_device on / (ext4, local, root file system) on disk1`; `WMSTAT selected renderer=gl output=kms width=2560 height=1600`; Touch Bar ready, keyboard and trackpad; `GPUDESK_DESKTOP_RC=0`; `SF_SESSION end request=none desktop_rc=0`; `SF_HALT processes: SIGTERM to 11`, `4 left after 5000 ms`, `SIGKILL sent, 1 left`; `SF_HALT downgrade: mount rc=0, root is now read-only`; `SF_HALT stop errno=0 state=stopped-clean`; `PD_BENCH_REBOOT`, `MACH Reboot`; `NO_COMMAND_LINE_OBSERVED_LINUX_VERIFIED` (nothing is typed in such a boot), `reset_attempts` 0. Verdict: partition identified in both copies, fence `[192633862, 208800773]`, the nine fence-end probes as in dr24, 24/24 and 34/34 and 6/6 ranges, the 13 files of dr17 to dr25 and the desktop's state hash-equal, clock -1.0 s / -0.4 s, Linux `e2fsck -fn` clean (`10221/4046848 files, 466409/16166912 blocks`), no `needs_recovery`, journal start 0, `unsafe_shutdowns` 107; 2,866 write commands (12.3 MB), 403 flushes, `fence_rejected=0 range_refused=0` |

**dr27's one FAIL, and the STOP file.** `NV_VERDICT FAIL sf-boot ran once:` The line was there (`SF_BOOT clock_rc=0 run_and_tmp=cleaned removed=3 date=2026-11-13T02:06:35Z`), but in a multi-user boot the console's login shell shares the console and its prompt's escape sequence stood in front of it on the same line (`ESC[?2004hSF_BOOT …`); the check was anchored at the line's start. The checker now also accepts the line after a CSI sequence; the same captures graded again offline give 87/87 (`runs/m1-storage/dr27-verdict-rechecked.txt`; the boot's own verdict, `dr27-verdict.txt`, is kept as it was: 86 ok, 1 FAIL). **`runs/m1-storage/STOP` was written by that verdict and is still in place** (copy: `dr27-STOP-checker-console-login-prefix.txt`); it is set aside only on the reviewer's word, so no storage boot starts until then.

**Seconds** (dr24: launcher-driven with the RAM root uploaded; dr27: the stage, nothing else):

| after the controller's worker began | dr24 | dr27 |
|---|---|---|
| upload finished, m1n1 vectored | 19.6 (kernel 51.7 MB raw, RAM root 84 MB compressed) | 2.7 (one image, 14.4 MB) |
| kernel banner | 19.7 | 6.3 (the stage inflates the collection, asks the SEP for the seed, rewrites the tree: 3.6 s) |
| session script begins | 32.5 | 15.7 |
| first frame | 35.1 | 18.2 |

The upload's saving is 16.9 s to the first frame. From the kernel's banner the two are alike (first frame 15.4 s and 11.9 s after it; in dr24 4 s of that are the bench typing): both carry the bench's prelude in front of the desktop, and `pdnvme_delay` was at its default.

**Not proven, and warts**
- **The network in the disk's own session.** dr27 has no `GPUDESK_NET_UP_RC` line (the session waited 60 s for pd-netd's `state=online`). Solved in step 5; the note that stood here (no log on the root, so the job did not run) was wrong: the log is in `/var/log`, and the job ran.
- `DR_STAGE=1` (stage with a RAM root and a typed session on the disk root): written, not booted.
- A session ended from the desktop menu in a boot without a RAM root (`sfbench_auto=…` reaches the autopilot's variables, not its end request: `@AUTO_END@` is empty there); a power-off; a session without `sfbench=` (the plain `exec /bin/reboot` at the end of `sf-session`: rehearsed in QEMU only through the bench branch); `pdnvme_ttl=0` (dr27 had 200 s); a session longer than the bench's 60 s.
- What `sf-halt` leaves: in dr27 four processes outlived SIGTERM and one outlived SIGKILL (launchd restarts its KeepAlive jobs while the halt runs; in QEMU three were left). The root was clean all the same (`sealed_refused=0`, no write command after the downgrade). `sf-halt` still does not name them.
- In a multi-user boot the console's login shell (`org.puredarwin.console-login`) writes to the same console as the session; any check anchored at a line's start can be hit the way `SF_BOOT` was.
- The stage image is the bench's: it falls back to the USB proxy when it refuses, and the launcher still collects the proxy, paints the logo and uploads. The installed chain (stage 1 or `kmutil`'s boot object, section 4 of the install procedure) does not exist.
- `cmp` seems to be missing on the root: `DR_INSTALL=1` copies the compositor again even when it is the same file (dr25: `NV_SFINSTALL compositor rc=0 e5489e51…`, the digest already there).

**To do next**: see the end of "Step 5". The first three items of this list as it stood after dr27 are done there (the stop file, the network, a session without `sfbench=`).

### Step 5: the second shrink, the network in the disk's own session, a session without the bench (dr28 to dr30, 13 November 2026)
**The second shrink.** The coordinator took 131,072 more blocks from the end of the partition (ext4 first, then the entry; start and identifier unchanged) and moved the fence constant (`92c390cb0`: `PD_SF_FENCE_BLOCKS 16035840`). Rebuilt for it: the three write collections, `/nix/store/6ld8dmbx…` (native with restart and clock, the only one booted), `/nix/store/ipzdj9bk…` (native), `/nix/store/43x6pbw2…` (hypervisor); the stage image was rebuilt and is byte for byte the same (sha256 `edaaab16…`: it carries the filesystem's UUID, which the shrink kept, and nothing about the length). Earlier store paths (`zz2hfjnr…`, `kafwjsds…`, `lrrqbhkg…` and older) must not boot the disk root. New: `runs/m1-storage/linux-d/ranges` (see "Commands"), ten fence-end probes instead of nine (`guest/darwin-diskroot-halt.in`, `nv_verdict.py`: `out-old-last` is now the last block before the second shrink, 208,800,773; `out-older-last` the last block before the first, 209,411,077; `out-freed-last` 209,411,118 as before).

**Why the network did not come up in dr27.** Not what the note after dr27 said. pd-netd's job did run; its log is `/var/log/pd-netd.log`, and the reader looked in `/private/var/log` (on this root `/var` is a directory of its own). The log said, every few seconds, `wifiutil: The network couldn't be joined.` / `pd-netd: en0 autojoin ended with 1`: pd-netd joins **remembered** networks only (`wifiutil autojoin`), and the root remembered none. `wifiutil join` keeps a network in `/Library/Preferences/SystemConfiguration/org.puredarwin.wifi.known`; the image this root was copied from has no `/Library/Preferences/SystemConfiguration`, wifiutil does not create it and says nothing when it cannot write the file. So every typed boot (dr22 to dr25) joined with the bench's credentials and remembered nothing, and the first boot in which nothing was typed had nothing to join. (On the RAM root nobody noticed: nothing there outlives a boot.) Read from the root, read-only, before anything was changed (`runs/m1-storage/dr27-netlog2.txt`): `SFL_NETDIR /Library/Preferences/SystemConfiguration: … No such file or directory`, `SFL_NETKNOWN no file of remembered networks`, `pd-netd.en0.status: state=joining`.

**The fix.**
- `system/sf-install.sh` creates `/Library/Preferences/SystemConfiguration`; `system/sf-boot` creates it at boot if it is missing. A network joined once (`wifiutil join <name> <passphrase>`, or as here the bench's typed session) is then remembered **on the disk**, as a name and the key derived from the passphrase (file mode 0600, never the passphrase itself); pd-netd joins it in every later boot.
- `system/sf-boot` also removes, at once and before the wait for the clock, two leftovers of the last boot in `/var/run` that a RAM root never had: the mark of a network left on purpose (`org.puredarwin.wifi.left`, which upstream means to last "until the next join or restart") and a `pd-netd.*.status` that still says `online`. Nothing else in `/var/run` is touched (launchd's other jobs are already using it).
- `system/sf-session` reports what pd-netd reached: `GPUDESK_NET_UP_RC=<rc> by=pd-netd remembered_networks=<n> <status>`, then `GPUDESK_LAN_PING_RC` (the router the lease names) and `GPUDESK_PING_RC` (1.1.1.1). New boot argument `sfmark=1`: without `sfbench=`, sf-halt prints its last line `PD_BENCH_REBOOT` so that a capture ends when the machine restarts itself; nothing else changes.
- `system/sf-halt.c` names what it could not stop (below).
- `tools/dr_linux_root.sh netlog` reads the right places and says how many networks are remembered (never which). `tools/dr_desk_boot.sh DR_PLAIN=1`, `tools/dr_desk_slot.sh` (its checks for such a boot), `tools/dr_plain_ranges.py` (the Linux side of them), `tools/dr_qemu_session.sh` (a third boot `p`: the plain session).
- QEMU rehearsal `qs2` (install, bench session, plain session): `e2fsck -fn` rc 0, state clean, journal start 0 after each; the plain boot printed no `NV_` line, ended through `exec /bin/reboot` with the marker, and the directory was there (mode 0755). The virt machine has no Wi-Fi: the join itself could not be rehearsed.

**Boots** (all native, collection `6ld8dmbx…`, `reset_attempts` 0, no stop file)

| Boot | What | Result |
|---|---|---|
| dr28 | launcher-driven, RAM root, typed session, `DR_INSTALL=1` (compositor `wm-darwin-keep-build-003` again), `DR_SECONDS=60`: installs the fix; first boot on the new layout | **passed 87/87**. `NV_PART verified first=192633862 blocks=16035840 exempted=0: found in both GPT copies`; fence `first=192633862 last=208669701`; probes `in-first lba=192633862 rc=0`, `in-last lba=208669701 rc=0`, `in-last-256 lba=208669446 blocks=256 rc=0`, `out-before lba=192633861 rc=1`, `out-after lba=208669702 rc=1`, `out-across-end lba=208669701 blocks=2 rc=1`, `out-old-last lba=208800773 rc=1`, `out-older-last lba=209411077 rc=1`, `out-freed-last lba=209411118 rc=1`, `out-block0 rc=1` (`NV_WPROBE_TOTAL admitted=3 refused=7`); `NV_SFINSTALL rc=0`; `GPUDESK_WIFI_JOIN_RC=0 attempts=1`, then `NV_NETKNOWN remembered_networks=1 directory=there`; 27 of 27 ranges Linux never writes unchanged; `SF_HALT processes: 1 left after 5000 ms: 3(zsh)`; Linux `e2fsck -fn` clean (`10219/4014080 files, 464352/16035840 blocks`); `DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED` |
| dr29 | `DR_STAGE=disk DR_SECONDS=90`: stage and kernel only, the disk's own session, `sfbench=dr29` | **passed 87/87**. `SF_SESSION begin seconds=90 bench=dr29 started_by=launchd`; `PDBwfm: state SCAN -> AUTH`, `AUTH -> ASSOC`, `ASSOC -> RUN` 13.9 s after the session began; `GPUDESK_NET_UP_RC=0 by=pd-netd remembered_networks=1 state=online ssid=(not printed) addr=10.1.1.211 mask=255.255.255.0 router=10.1.1.1 dns=10.1.1.1 server=10.1.1.1 lease=86400` (17.4 s); `GPUDESK_LAN_PING_RC=0 host=10.1.1.1 (the router of the lease)`; `GPUDESK_PING_RC=0`; `WMSTAT selected renderer=gl output=kms width=2560 height=1600`; `GPUDESK_DESKTOP_RC=0`; `NV_PART verified first=192633862 blocks=16035840 … found in both GPT copies`, the ten probes as in dr28; `SF_HALT downgrade: mount rc=0, root is now read-only`, `SF_HALT stop errno=0 state=stopped-clean`, `PD_BENCH_REBOOT`, `MACH Reboot`; Linux `e2fsck -fn` clean (`10228/4014080 files, 464369/16035840 blocks`); `NO_COMMAND_LINE_OBSERVED_LINUX_VERIFIED` |
| dr30 | `DR_STAGE=disk DR_PLAIN=1 DR_SECONDS=150`, upper bound 300 s: **no `sfbench=`** (the session's boot arguments are `sfsession_seconds=150 sfmark=1`) | **passed** (`DR_PLAIN PASSED`, 14 of 14 session lines, the slot's storage checks). `SF_SESSION begin seconds=150 bench=no started_by=launchd`; no `NV_` line on the console; `WMSCALE initial scale=150% … source=persisted … theme=graphite theme_source=persisted wallpaper=graphite-fold`; GL on KMS 1.7 s after the session began; `GPUDESK_INPUT kbd=yes mouse=yes`, Touch Bar ready; `GPUDESK_NET_UP_RC=0 by=pd-netd remembered_networks=1 state=online …` (associated at 13.9 s, reported at 20.4 s), `GPUDESK_LAN_PING_RC=0 host=10.1.1.1`, `GPUDESK_PING_RC=0`; `GPUDESK_DESKTOP_RC=0` at 151.1 s; `SF_SESSION end request=none desktop_rc=0`; `SF_HALT downgrade: mount rc=0, root is now read-only`; `SF_HALT stop errno=0 state=stopped-clean level=3 ttl=260`; `SF_HALT restarting now`, `PD_BENCH_REBOOT` at 159.8 s, `MACH Reboot` at 160.6 s. Linux: `e2fsck -fn` rc 0 (`10226/4014080 files, 464372/16035840 blocks`), state clean, journal start 0; 27 of 27 ranges Linux never writes unchanged (table copies, APFS partitions, the unallocated space); `unsafe_shutdowns` 107 → 107, media errors and error log 0 → 0 |

**What a person sees in dr30's kind of boot** (from the console's lines and their arrival times; nobody watched the panel): the boot picture, then about nine seconds after the kernel starts the desktop in the look chosen earlier (Graphite, 150 %, its wallpaper) with two terminal windows, keyboard, trackpad and Touch Bar working; Wi-Fi is up about a quarter of a minute later without anything being typed or clicked. The desktop stays until the session's time is over (150 s here because the boot arguments said so; four hours without `sfsession_seconds=`), or until Session > Restart is chosen. Then the desktop ends, about nine seconds pass (five of them spent waiting for processes that will not go, below; what the panel shows meanwhile was not looked at), and the machine restarts into its default entry, Linux.

**What `sf-halt` cannot stop, named.** In the multi-user boots: `… 4 left after 5000 ms: 414(pd-dhcp) 413(pd-netd) 412(mdnsd) 12(zsh)`, then `SIGKILL sent, 2 left: 417(notifyd) 416(zsh)` (dr30; dr29 alike). The process numbers are new ones: these are launchd's `KeepAlive` jobs (pd-netd with its children, the console's login shell, notifyd), which launchd starts again as fast as sf-halt ends them, so the five seconds are always spent. In a single-user boot the one left is `3(zsh)`, the console's own shell, which is spared on purpose. Not changed: the halt path was left as proven. The cure is to tell launchd to stop its jobs first (or not to count children of launchd born after the SIGTERM); the root was clean every time (`sealed_refused=0`, no write command after the downgrade).

**Not proven, and warts**
- dr30 has `sfmark=1` and `sfsession_seconds=150` among its boot arguments: it is the plain path (no prelude, no end script, `exec /bin/reboot`) but not the argument-free one. A session that is never ended (four hours), and a session ended by a controller reset with the root mounted read-write, were not booted: the second would leave the journal to be replayed, which only Darwin may do on this partition.
- Without `sfbench=` nothing inside the boot checks the fence, the partition or the SMART page: dr30's storage evidence is Linux's (before and after) and sf-halt's own lines.
- The remembered network is on the disk now (one name, one derived key, mode 0600, root only). Nothing in SiliconForest forgets it unless someone asks wifiutil to.
- Joining a network for the first time from the desktop (no typed command) was not exercised: the network was remembered by the bench's typed `wifiutil join` in dr28.
- `pd-netd` and upstream's `org.puredarwin.wifi-autojoin` job both run `wifiutil autojoin`; they did not collide in two boots (a marker file keeps them apart), but one of them is superfluous.
- `sf-boot` cleans `/private/var/run` and `/private/tmp` only; `/var/run` keeps the last boot's sockets and process-id files of launchd's jobs (they are overwritten when the jobs start). `/var/log` grows without bound (`mdnsd.log` was 59 kB after one minute in dr27).
- The clock is set about four seconds into the boot; files pd-netd writes before that carry 1970 times.
- Still open from step 4: `DR_STAGE=1`; a session ended from the desktop menu in a boot without a RAM root; a power-off; `pdnvme_ttl=0` (dr30 had 260 s and used 160); the hypervisor and plain native write collections on this layout; `cmp` missing on the root.

**To do next**
1. **The install is the owner's** (the kit and its steps are now `m1-native-boot/install/INSTALL.md`, see "Step 6"; what follows is the state before it): `m1-native-boot/b1/INSTALL-PROCEDURE.md` now has the present numbers and the decided design (two stages; the stub, 610,304 blocks, and a FAT boot partition, 131,072 blocks, both in the unallocated blocks 208,669,702 to 209,411,118, both created by him in macOS). Proposed there, for his word: the stub first with its exact size (208,669,702 to 209,280,005), the boot partition second taking the rest (209,280,006 to 209,411,077), 41 blocks left free; and a look, before that step, that nothing of the Linux install names a partition by number. **Nothing of it is executed, no script for it exists, and nothing outside the partition is written by us.** After his `diskutil` step the range list needs a successor to `linux-d` and one disk-root boot must show the driver still finds our entry in both table copies.
2. `pdnvme_ttl=0` (exists since step 6; 150 s in dr31) with a session of some minutes; flush rate-limiting if the 400 flushes matter.
3. `sf-halt` and launchd's jobs (above); one autojoin instead of two; a bound on `/var/log`.

### Step 6: the install kit (dr31, dr32, 13 November 2026)
`integrations/puredarwin/m1-native-boot/install/` (`INSTALL.md`: the route, what an unattended start does on every failure, the owner's numbered steps, the kernel-update path, uninstall). **Nothing of it has been executed on the M1's disk**; no partition exists yet.

**What depended on the bench, and what replaced it**
- The image was packed on the controller for each boot (configuration with the launcher's own `xnu-base`, the proxy's USB port, the bench's boot arguments). Now `install/build.sh` packs it on the workstation: `out/boot.bin` = stage 2 + configuration (`display=1 usb-nodes=remove0 sprr=1 amx=1 xnu-base=0x850000000`, dr30's command line with `pdnvme_ttl=0` and without `sfsession_seconds`/`sfmark`) + the collection, 14,423,700 bytes. `xnu-base` is fixed (the launcher's varied from boot to boot between 0x851678000 and 0x852c6c000): the kernel gets the memory above 1.25 GiB, as in every boot so far; the stage's own choice (`lowmem`, about 1 GB more) is still not booted.
- The stage fell back to m1n1's USB proxy when it refused, and m1n1 restarts the machine on a panic. `install/patches/0001` (build flag `SF_INSTALLED`, both stages): in both places it turns the screen log on, prints `SiliconForest: stopped (<reason>)` with what to do, and stops (`sf_stop`); no USB, no restart, nothing written. The bench's stage in `b1/out/` is unchanged (it keeps the proxy for read-back boots).
- **`pdnvme_ttl=0` did not exist.** The driver took 10..570 and otherwise kept its default of 90 s: an installed system would have lost its disk after a minute and a half, and the documents (this one included) had assumed 0 meant "no limit". `kernel/PDAppleNVMe.cpp` now takes 0 as no limit (two lines; `kernel-rw/rw.patch` regenerated, offsets only; host tests pass). Collection `/nix/store/916z475d…` (`build-kc-native-rw-halt.nix`); the fence, the gate and the halt gate are untouched.
- The logo: both stages are built `RELEASE=1 BUILTIN_LOGO=siliconforest` from `branding/siliconforest-boot.svg` (the mark, off-white on black); a normal start shows nothing else. It is not the wallpaper (the stored wallpaper is `graphite-fold`; `chonkstep-darwin/backgrounds/siliconforest.png`, which the bench launcher paints as its boot picture, is a forest scene with the word mark and is not used by the installed chain). The icon is `branding/siliconforest-icon.svg` as an `.icns` (made by `build.sh`).
- Launcher: words `bootbin` (the file `sf-boot.bin` is uploaded byte for byte, followed by a variable line as stage 1's chainload appends one; its own configuration is the boot's) and `stage1` (`native_boot.py`, `b1/native_stage.py`); `tools/dr_desk_boot.sh DR_BOOTBIN=<file>` / `DR_STAGE1=<file>`, and the slot's checks for the second.

**Boots** (both native, no stop file)

| Boot | What | Result |
|---|---|---|
| dr31 | `DR_STAGE=disk DR_PLAIN=1 DR_BOOTBIN=install/out/boot-bench.bin` (sha256 `5e536ba7…`; = `boot.bin` plus `sfsession_seconds=150 sfmark=1` in the configuration record, nothing else: `install/test_kit.py` compares them) | **passed** (`DR_PLAIN PASSED`, 14 of 14). `m1n1 121fcbf…-siliconforest-boot-v1`; `PDSF: config 643 bytes … crc d955ef1b ok`, `PDSF: kernel 10163133 bytes gzip, 33810078 in memory, crc 7462afda ok`; `PDSF: XNU memory 0x850000000..0xbd2d5c000`; `PDSF: boot seed from the SEP`; `PDSF: 7 secondary cores held in the EL2 page`; `PDSF: entering XNU at 0x852662490`; `SF_SESSION begin seconds=150 bench=no started_by=launchd`; `WMSTAT selected renderer=gl output=kms width=2560 height=1600`; `WMSCALE initial scale=150% … theme=graphite theme_source=persisted wallpaper=graphite-fold`; `GPUDESK_NET_UP_RC=0 by=pd-netd remembered_networks=1 state=online`; `GPUDESK_LAN_PING_RC=0`, `GPUDESK_PING_RC=0`; `GPUDESK_DESKTOP_RC=0`; `SF_HALT rehearsal: controller ready level=3 ttl=0`; `SF_HALT downgrade: mount rc=0, root is now read-only`; `SF_HALT stop errno=0 state=stopped-clean level=3 ttl=0 : requested: clean`; `PD_BENCH_REBOOT`, `MACH Reboot`; `reset_attempts` 0, `NO_COMMAND_LINE_OBSERVED_LINUX_VERIFIED`. The desktop started 11 s after the stage was entered. Linux: `e2fsck -fn` rc 0 (`10226/4014080 files, 464377/16035840 blocks`), state clean, journal start 0; 27 of 27 ranges Linux never writes unchanged; `unsafe_shutdowns` 107 → 107 |
| dr32 | `DR_STAGE=disk DR_STAGE1=install/out/sf-stage1-test.bin` (stage 1 as the installer writes the boot object, with a PARTUUID no partition has) | **passed** (`DR_STAGE1 PASSED`). `m1n1 121fcbf…-siliconforest-stage1-v1`; `fb: display logo`; `Found variable chainload`; `nvme: initialized at 0x27bcc0000`; `Chainloading from the ESP`; `nvme: 64-block reads verified`; `Searching for the requested partition`; `Chainload failed: PartitionNotFound`; `nvme: shutdown done`; `No valid payload found`; `SiliconForest: stopped (nothing could be started).` … `SF_STOPPED`; no `Running proxy`, no kernel banner; one controller reset (as planned), `…LINUX_VERIFIED`. Linux: `e2fsck -fn` rc 0, 27 of 27 ranges Linux never writes unchanged, 0 of ours written, `unsafe_shutdowns` 107 → 107 |
| dr33 | rehearsal: the same stage 1 from RAM with `chainload=<Linux's EFI partition>;m1n1/boot.bin` (read-only: m1n1's storage has no write, `rust/src/nvme.rs`) | read the file, **did not chainload it**: `Partition offset: 210021423`, `File size: 10238674`, `chainload: image span overlaps the live heap`, then `SiliconForest: stopped`; one controller reset. **This was the installed machine's failure, seen in advance and explained away** as a bench artefact ("iBoot leaves the 64 MiB of `m1n1.ld`'s `_max_payload_size` there": it does not, for a raw boot object; see "Step 6b"). (The slot printed its fail-safe wording, `DR_STAGE1 PASSED`, which does not describe this run.) |
| dr34 | the same, the launcher giving stage 1 those 64 MiB (`STAGE1_PAYLOAD_ROOM`, `b1/native_stage.py`; **a layout the installed machine does not have**, now only behind `DR_STAGE1_ROOM=1`) | **the chainload completed and Linux came up by itself**: `Searching for the requested partition`, `Partition offset: 210021423`, `File size: 10238674`, `chainload: image 0xfa0000 bytes; firmware block at 0x812804000; high-water 0x818150000`, `nvme: shutdown done`, `Valid payload found`, `Vectoring to next stage...`, then `m1n1 121fcbf…-darwin-lab-j293-window-v1` (Linux's stage 2) and its own `Valid payload found`; `reset_attempts` 0, `…LINUX_VERIFIED`, a new Linux boot ID, no PMU boot-error line. `/boot/efi/m1n1/boot.bin` has the same sha256, size and modification time before and after; 27 of 27 ranges Linux never writes unchanged; `p3` `e2fsck -fn` rc 0, none of our ranges written; `unsafe_shutdowns` 107 → 107. (The slot ends with rc 1: its three fail-safe lines, `SF_STOPPED` and the like, do not apply to a chainload that succeeds; its storage checks passed.) |

**Host**: `install/test_kit.py` (9 tests) runs the installer's own `osinstall.py`/`m1n1.py`/`util.py` (the copy in the kit) against our data and package with macOS's tools replaced by recorders: one `addPartition(<stub>, %EFI%, %noformat%, 524288000)`, `newfs_msdos -F 32 -v "SFBOOT - Si"`, `sf/boot.bin` on the FAT mount byte-equal to `out/boot.bin`, the boot object = stage 1 + `chosen.asahi,efi-system-partition=<partuuid>` + `chainload=<partuuid>;sf/boot.bin` + four zero bytes, the space needed (2,884 MiB) inside the free 2,896.16 MiB. `install/linux/sf-gpt-check.py` was run on the M1's real table copies (the read of 12 November) and on altered copies of them (two entries added: `GPT ALL OK`; one of them overlapping `p4`: 4 failures).

**Not proven before the install (first exercised by it)**
- Stage 1 chainloading **our** file from **our** partition: dr34 proves, with our stage 1 on this machine, the partition found by PARTUUID, the FAT directory and file read, `chainload_image` (the next image copied to stage 1's base, SEPFW and boot arguments behind it) and the jump, with Linux's 10.2 MB file from Linux's EFI partition. Left: a FAT made by `newfs_msdos` on macOS, the path `sf/boot.bin`, 14.4 MB instead of 10.2, and stage 2's records behind the image instead of device trees (dr31 entered that file in place).
- iBoot starting our stage 1 as a boot object, the picker's icon and name, the pairing, `bputil`/`kmutil` with it: Apple's side, by the installer's code.
- The installer run itself (the kit's Python on macOS, `sf-install.sh`, `sf-preflight.sh`, `sf-uninstall.sh`: written, syntax-checked, never run on macOS); what `diskutil` does to the table.
- `linux/sf-bootbin-install.sh` (no SFBOOT to write to).
- A start with no USB cable, no bench boot picture, and the kernel entered seconds after power-on (the clock is read by `pdthermal_delay=4`; `sf-clock` waits up to 30 s).
- A session longer than 150 s with `pdnvme_ttl=0`; the four-hour session; what the panel shows (nobody looked).

### Step 6b: the install, the first start's refusal, two files (dr35, dr36, 13 November 2026)
The owner ran the installer and the recovery step. From Linux afterwards (`linux/sf-gpt-check.py` against the read of before the install): `GPT ALL OK`, both copies valid and identical, all 8 earlier entries byte for byte (5 at another position: Linux now numbers `p3` SiliconForest, `p4` our stub, `p5` SFBOOT, `p6` Linux's stub, `p7` its EFI partition, `p8` `/boot`, `p9` root, `p10` recovery), exactly two new entries in the former free space: APFS 208,669,702..209,280,005 and EFI-typed 209,280,006..209,408,005 (128,000 blocks); the read is `private/disk-root/gpt-backup/sf-gpt-after.*`.

**The first start from the picker** showed the logo, then stage 1's log: `Partition offset: 209280006`, `File size: 14423700`, `File read successfully`, `chainload: Preparing image...`, `chainload: image span overlaps the live heap`, `No valid payload found`, `SiliconForest: stopped (nothing could be started).` Nothing was damaged; the fail-safe did what it is for.

**Cause** (`sources/m1n1-baseline/src/chainload.c`, `chainload_image`): the next image is copied to stage 1's own base, and it is refused when `live_heap > inherited_top && image_end > inherited_top && new_base < live_heap`, with `image_end = base + ALIGN_UP(file + variable lines + 4, 16 KiB)` and `inherited_top` the `top_of_kernel_data` stage 1 was started with (its heap begins there, and holds the file by then). iBoot starts the stub's boot object raw: stage 1 at `0x805494000` (the photo's `No more payloads at 0x8058b4082` is its 4,325,376 bytes plus 130 bytes of variable lines), top somewhere behind that, and below `0x805494000 + 0xdc4000 = 0x806258000` (or the 14,423,700-byte file, span `0xdc4000`, would have been taken). Certain room: stage 1's own 4,325,376 bytes. dr33 had shown this refusal; dr34's 64 MiB made the bench unlike the machine.

**Fix (no recovery trip; stage 1 in the stub is unchanged, sha256 `44c1ef0b…`, and `build.sh` stops if it would build another)**
- `sf/boot.bin` = stage 2 alone, 4,128,768 bytes (sha256 `81d58f1b…`), span 4,145,152 with 512 bytes allowed for the lines stage 1 passes on (it passes two `chosen.` lines, 169 bytes; it does not pass `chainload=`), 180,224 bytes to spare. `build.sh` fails when span + 32 KiB > stage 1's size; `test_kit.py` checks it, and that the first kit's file does not fit.
- Stage 2 (`…-siliconforest-boot-v2`): the stage of `b1/` with `CHAINLOADING=1` (m1n1's FAT reader) and `SF_KERNEL_FILE=1` (`install/patches/stage2/0001`, `install/src/pdsf_file.c`). With no records behind its image it takes the partition from `chosen.asahi,efi-system-partition=`, refuses `chainload=`, `boot=` and any other payload, starts the storage controller, reads `sf/kernel.bin` into its heap (`rust_load_image`, read-only), stops the controller, and hands the bytes to the same code as appended records (`pdsf_boot_records`; checksums, root rule, `sf_stop`). With the FAT reader the image grew by one 64 KiB step past stage 1's size; `patches/stage2/0002` (one EL3 stack instead of four: T8103 has no EL3) takes 192 KiB off. `b1/`'s bench stage is unchanged in behaviour and still has no storage code (`test_b1.py`).
- `sf/kernel.bin` = configuration + collection as a record list (10,163,856 bytes, sha256 `a035b539…`; `pdsf_pack.py pack` without `--m1n1`). `kernel-bench.bin` = the same with `sfsession_seconds=150 sfmark=1`.
- `linux/sf-bootbin-install.sh <PARTUUID> <boot.bin> <kernel.bin>`: both files, each replaced file kept as `.old`, an identical file left alone, a bench kernel only with `SF_BENCH=1` and never kept as `.old`; the old refusals, plus: `boot.bin` must be stage 2 alone and fit.
- The installer package carries both files (`esp/sf/`), so a fresh install is right too.
- Launcher: `stage1` enters stage 1 as iBoot does (boot arguments page directly behind the image, top behind that); `stage1room` gives the 64 MiB; `stage1go` expects stage 1 to start the system from the boot partition and captures until the session's own restart (`DR_STAGE1_ROOM=1`, `DR_STAGE1_GO=<copy of the kernel file on SFBOOT>`; the slot then checks the chain and the session as `DR_PLAIN`'s).

**Boots** (both native, both with `install/out/sf-stage1-installed.bin`: the kit's stage 1 + the two variable lines with SFBOOT's PARTUUID, as the installer wrote the boot object)

| Boot | SFBOOT held | Result |
|---|---|---|
| dr35 | the first kit's `sf/boot.bin` (14,423,700 bytes, `8f300c87…`) | **the owner's failure reproduced**: `m1n1 121fcbf…-siliconforest-stage1-v1`, `No more payloads at 0x812cd0082`, `nvme: initialized`, `Partition offset: 209280006`, `chainload: Preparing image...`, `chainload: image span overlaps the live heap`, `nvme: shutdown done`, `No valid payload found`, `SiliconForest: stopped (nothing could be started).`, `SF_STOPPED`; one controller reset (as planned), `…LINUX_VERIFIED`; `e2fsck -fn` rc 0, 30 of 30 ranges Linux never writes unchanged, `unsafe_shutdowns` 107 → 107 |
| dr36 | `sf/boot.bin` = stage 2 (`81d58f1b…`), `sf/kernel.bin` = `kernel-bench.bin` (`75cf16e9…`), `boot.bin.old` = the first kit's | **passed** (`DR_PLAIN PASSED`, every line ok). Stage 1: `top_of_kdata: 0x813328000`, `fb: display logo`, `Partition offset: 209280006`, `File size: 4128768`, `chainload: packed span would overlap protected/live memory; parking firmware block`, `chainload: image 0x3f4000 bytes; firmware block at 0x814b3c000; high-water 0x815118000`, `nvme: shutdown done`, `Valid payload found`. Stage 2: `m1n1 121fcbf…-siliconforest-boot-v2`, `fb: display logo`, `No more payloads at 0x8132f00a9`, `PDSF: reading sf/kernel.bin from the boot partition`, `nvme: initialized`, `Partition offset: 209280006`, `File size: 10163888`, `nvme: shutdown done`, `PDSF: SiliconForest payload at 0x816539040 (read from the boot partition)`, `PDSF: config 643 bytes … crc d955ef1b ok`, `PDSF: kernel 10163133 bytes gzip, 33810078 in memory, crc 7462afda ok`, `PDSF: XNU memory 0x850000000..0xbd2d5c000`, `PDSF: boot seed from the SEP`, `PDSF: 7 secondary cores held in the EL2 page`, `PDSF: entering XNU at 0x852662490`. Then `SF_SESSION begin seconds=150 bench=no started_by=launchd`, `WMSTAT selected renderer=gl output=kms width=2560 height=1600`, `WMSCALE initial scale=150% … theme=graphite … wallpaper=graphite-fold`, `GPUDESK_NET_UP_RC=0 by=pd-netd remembered_networks=1 state=online`, `GPUDESK_LAN_PING_RC=0`, `GPUDESK_PING_RC=0`, `GPUDESK_DESKTOP_RC=0`, `SF_HALT downgrade: mount rc=0, root is now read-only`, `SF_HALT stop errno=0 state=stopped-clean level=3 ttl=0`, `SF_HALT restarting now`; `reset_attempts` 0, `…LINUX_VERIFIED`. Linux: `e2fsck -fn` rc 0 (`10226/4014080 files, 464384/16035840 blocks`), state clean, journal start 0; 30 of 30 ranges Linux never writes unchanged (SFBOOT's three among them); `unsafe_shutdowns` 107 → 107. The kernel's driver started the storage controller after m1n1 had started and stopped it twice |

**On SFBOOT now** (written from Linux by `sf-bootbin-install.sh`, read back through a read-only mount): `sf/boot.bin` 4,128,768 bytes `81d58f1b…` (= `out/boot.bin`), `sf/kernel.bin` 10,163,856 bytes `a035b539…` (= `out/kernel.bin`), `sf/boot.bin.old` 14,423,700 bytes `8f300c87…` (the first kit's; stage 1 refuses it). The default startup volume is Linux again.

**Not proven**
- The start from the picker with these files: iBoot's own `top_of_kernel_data` is imitated on the bench (boot arguments page and top directly behind the image), not known. The fit rule does not depend on it; whether stage 1 parks the firmware block (as in dr36) or not (as in dr34) does, and both paths have run.
- `sf/kernel.bin` as installed (dr36 booted `kernel-bench.bin`: the same collection record, two more words in the configuration record).
- No USB cable, a session longer than 150 s, `--rollback`, uninstall: as before.

### Step 7: daily use (dr37 to dr41)
What the owner asked for after the first start from the picker: SSH (server and client), Homebrew, the Claude Code CLI, the forest picture, and a session that does not end by itself.

**How it gets onto p3.** Nothing is written to p3 from Linux. (1) One bench boot with a RAM root (`DR_INSTALL=1 DR_USERLAND=1`, `DR_COMPOSITOR=`, `DR_TERMINAL=`) installs, through `system/sf-install.sh` and `userland/sf-userland-install.sh`: the session, the compositor and terminal builds below, upstream's OpenSSH server (`userland/build-openssh.nix`: the tree's `openssh-arm64`, 10.4p1; Homebrew's openssh bottle cannot load here: `ssh` links Kerberos.framework, `sshd` libpam and libsandbox), `sshd_config`, the launchd job `org.siliconforest.sshd` (`/usr/libexec/sf-sshd`), the account `sf` (uid 501, group staff, `/Users/sf`; Homebrew refuses root), `/usr/local/bin/brew` (runs the client as `sf` through `hb-runas`), `/etc/sf-environment`, `/usr/bin/shasum`, and ONE public key in `/var/root/.ssh/authorized_keys` (this workstation's `~/.ssh/id_rsa.pub`). The host key (ed25519) is made on the M1 at that moment and printed; it never leaves the disk. (2) Everything large goes over SSH into the RUNNING system during a bench boot of the installed chain (`tools/sf_userland_push.sh`: parts `gtar`, `sys`, `brew`, `claude`; each a tar stream, then the SHA-256 of every file computed on the M1 and compared here). The bench copy of `sf/kernel.bin` for such a boot is made by `m1-native-boot/install/sf_bench_kernel.py <kernel.bin> <seconds>` (470 s here; with 150 it reproduces `kernel-bench.bin` byte for byte; `test_kit.py`).

**SSH.** `sshd_config`: port 22, IPv4, all addresses (the Wi-Fi is the only interface besides the loopback), public keys only (`AuthenticationMethods publickey`, no password, no keyboard-interactive, no empty passwords), `AllowUsers root`, `PermitRootLogin prohibit-password` (root is the only account a person uses; it has no password). Host key fingerprint `SHA256:6jGrgzthy11YSazJr9illn78px6yTPvEnPMlAcGr9SU` (ED25519); on this workstation in `~/.ssh/known_hosts_m1_siliconforest` under the name `m1-siliconforest` (the M1's Linux, macOS and SiliconForest share one address and have three host keys): `tools/sf_userland_push.sh ssh` uses that file with strict checking and nothing else.

**The session's length.** `sf-session` without `sfsession_seconds=` now asks for the longest session the desktop accepts: 3,153,600,000 s (a hundred years) with the builds `wm-darwin-daily-build-001` (compositor, sha256 `721574af…`) and `terminal-application-daily-build-001` (`0e12ef81…`), whose only bound was the argument check (`Instant + Duration` stays in range; the u32 millisecond stamps are wrapping casts, as the Wayland protocol defines them); it falls back to four hours when either program on the disk is an older build (it reads the bound out of the binaries). Bench boots give `sfsession_seconds=` as before. The terminal build also: no longer ends itself after 8 MiB of output or 1 MiB of input (the transcripts in the records directory keep the first 8 MiB / 1 MiB), and its shell gets `PATH=/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/bin:/usr/bin:/sbin:/usr/sbin`, `HOME=/var/root` (was `/private/tmp`, emptied at every boot) and `LANG=en_US.UTF-8`. The two source changes are in `runs/m1-desktop-010/<build>/source` (copies of `wm-darwin-keep-build-003` and `terminal-application-build-008`); **they are not yet in `chonkstep-source.patch.gz` nor in the wm-darwin branch.**

**The wallpaper.** Nothing stored is changed (the owner's own picks stay). With nothing stored the standalone desk wears the picture in `/usr/share/backgrounds/siliconforest` (the forest, `siliconforest.png`: on p3 since the root was copied); a stored theme brings its own wallpaper unless one was picked. The forest is the row **Siliconforest** in the desktop menu: click the desktop, **Wallpaper > Siliconforest**.

**For the person at the machine.** The desktop's terminal is root's shell (zsh as `sh`) with `/usr/local/bin` and `/opt/homebrew/bin` in front; home is `/var/root` and stays. `brew install <formula>`, `brew list` (the wrapper runs Homebrew as the account `sf`; no `brew update`). Installed: bash, git, less, nano, GNU grep (`ggrep`), sed (`gsed`), awk (`gawk`), tar (`gtar`), coreutils (`gls` …), findutils (`gfind`), make, tmux, python3/pip3, node/npm, jq, rg, tree, htop, wget, fzf, bat, fd, xz, zstd, sqlite3, gh, lua; the system's `curl`, `ssh`, `scp -O`, `ssh-keygen`. From another machine: `ssh root@<the M1's address>` with a key whose public half is a line of `/var/root/.ssh/authorized_keys` (one is there: this workstation's). `claude` is on the PATH and starts (since boot dr46; nobody is signed in). Restart: the desktop menu's Session > Restart, or `reboot`.

**Boots**
| Boot | What | Result |
|---|---|---|
| dr37 | install boot | never ran: the controller was off the network (its address changed to 10.1.1.55; `tools/pcie_iterate.sh` and `tools/m1_return_linux.sh` now try it first). The slot's stop file was set aside by the coordinator |
| dr38 | install boot (RAM root, `DR_INSTALL=1 DR_USERLAND=1`) | installed: `NV_SFINSTALL rc=0`, compositor `721574af…`, terminal `0e12ef81…`, `SF_USERLAND host key made on this machine`, `authorized keys for root: 1`, `desktop state left as it is: theme=beos scale=180 wallpaper=beos-blue`; the desktop ran 40 s on the new builds (`GPUDESK_DESKTOP_RC=0`, both terminals); `SF_HALT … stopped-clean`; `e2fsck -fn` rc 0. Verdict 84 ok, 2 FAIL: `nv_verdict.py` counted SFBOOT among Linux's filesystems (corrected: left out by `SF_BOOT_LBA`, and it must NOT be mounted; re-run on dr38's data: PASSED). Stop file set aside by the coordinator |
| dr39 | installed chain, session 470 s; `sys` and `claude` sent | SSH from the workstation with strict host-key check; `sys` (37 files) and `claude` (309 MB in 17 s) verified equal; **`brew` did not arrive**: the system's tar (toybox 0.8.13) gives up after about eighty files of one archive (`bad symlink`; 81 of 21,770 files; the shell's limit is 256 open files); hence the part `gtar` (GNU tar's one binary from its bottle, used for the unpacking from then on) and member lists that carry no system directory (toybox had given `/opt` the archive's owner; set back in dr41). `e2fsck -fn` rc 0; one session line FAIL (`GPUDESK_DESKTOP_RC=1`: the second terminal ended with status 1 at the session's own end; not seen in dr40 or dr41) |
| dr40 | the same; `gtar`, `brew` | **`DR_PLAIN PASSED`**, `e2fsck -fn` rc 0 (`64138/4014080 files, 770010/16035840 blocks`). `brew` sent and unpacked in 332 s, **21,784 files equal on the M1**. Every program once (`userland/sf-userland-check.sh`), all rc 0: `brew --version` (`Homebrew >=4.3.0`), `brew list --versions` (63), `brew config` (`macOS: 26.5-arm64`), **`brew install lua`** (fetched, poured, `Lua 5.5.1`), bash 5.3.20, git 2.56.1 (init, commit, log), curl 8.21.0 and an HTTPS fetch (200), less, nano 9.3, GNU grep/sed/awk, coreutils, findutils, tmux 3.8 (detached session), python 3.14 (ssl, sqlite3, hashlib), node (crypto), npm 11.20.0, jq, ripgrep, tree, htop, wget, fzf, bat, fd, xz, zstd, gh 2.103.3 (version only), shasum, **`claude --version`: `2.1.322 (Claude Code)`**, `ssh -V`: `OpenSSH_10.4p1`. `make` and `sqlite3` were not found under those names (Homebrew: `gmake`; sqlite is keg-only): links in `/usr/local/bin` since dr41 |
| dr41 | the same; ownership of `/opt`, the two links, checks | session and restart as dr40 (`SF_HALT … stopped-clean`, reset_attempts 0, 30 of 30 ranges Linux never writes unchanged), **but `e2fsck -fn` rc 4** (below); the slot wrote the stop file (set aside by the coordinator once the cause was shown). The rest of the driver's list did not run (a link-by-link check of mine used the time) |
| dr42 | the same with **the fixed driver** (collection `j0wkyjql…`, bench copy of the new kernel file); a mass deletion on the M1 (402 entries made in `/private/var/sf-massdelete`, 352 removed), the open proofs | **`DR_PLAIN PASSED`, `e2fsck -fn` rc 0** (`64196/4014080 files, 770110/16035840 blocks`), no line about garbage; freed inodes of the old kind (scan from Linux, read-only): 577 before, **6 now** (the test's new files took the others and the fixed driver freed them properly), none new, none in a block that is more than half such. `/opt/homebrew`: 21,784 files and 26,516 links equal. A login without a key: `Permission denied (publickey)`. `scp -O`: works. scp's default mode and sftp: **do not work** (`/libexec/sftp-server` ends with SIGBUS). `make`, `sqlite3`: answer. **`claude` on a terminal (tmux): killed at once (`EXIT=137`, no screen; kernel: `EXC_CORPSE_NOTIFY … subcode=0x700000002`)**; `claude --version` works |
| dr43 | a short diagnostic of that | never ran: the slot's Linux baseline was refused on port 22. Cause: the M1's Linux has `ufw limit 22/tcp` (six new connections in 30 s), and this stream's host-side driver polled SSH every 3 s while the M1 was still in Linux (`[UFW LIMIT BLOCK] SRC=<the workstation>`, 23:59:50 to 00:03); sshd never stopped. The driver now waits 75 s and polls every 9 s |
| dr44 | the diagnostic (session 230 s) | `DR_PLAIN PASSED`, `e2fsck -fn` rc 0. `claude --version`, `--help`: run; `doctor`, `-p`, the terminal start: killed (137); with `BUN_JSC_useJIT=0`: not killed, stops on `SharedArrayBuffer is not defined`; node's JIT runs. `SF-X-claude-code.md` |
| dr45 | a copy of the binary without the hardened-runtime bit (`userland/sf_cs_runtime.py`) | `DR_PLAIN PASSED`, `e2fsck -fn` rc 0 (`63629/4014080 files, 848470/16035840 blocks`). **The copy is killed the same way**; `/usr/local/bin/claude` still names the vendor's file; the useless copy `claude.sf` (309 MB) is on the disk |
| dr46 | bench session 330 s; **`/usr/lib/libSystem.B.dylib` replaced over SSH** by the build with `xpc-copy-description-never-aborts.patch` (`tools/sf_userland_push.sh libsystem`: checked by SHA-256 there, proven to load through `DYLD_LIBRARY_PATH`, the old file kept as `libSystem.B.dylib.old`, then renamed); the Claude Code check; `claude.sf` removed | `DR_PLAIN PASSED`, `e2fsck -fn` rc 0 (`63737/4014080 files, 771042/16035840 blocks`). **The unmodified `claude`: `doctor` rc 0, `-p` "Not logged in", first screen and login-method screen on tmux.** `SF-X-claude-code.md` |
| dr47 | a fresh boot from the disk with that libSystem (session 230 s), the same check | `DR_PLAIN PASSED`, `e2fsck -fn` rc 0 (`63739/4014080 files, 771077/16035840 blocks`); claude as in dr46; no `libxpc misuse` and no `EXC_CORPSE_NOTIFY` line on the console |

### Reaching the M1's Linux: SSH lock-outs removed (15 November 2026, 07:45Z)

Owner-authorized CODEX-TB change, during the normal CIO04CM Linux install
visit; no extra reboot. This supersedes dr43's 9-second polling workaround
and the later 60-second quiet-period advice for Linux SSH. Eight rapid fresh
key-authenticated sessions from the workstation all succeeded after the change.
Normal bounded boot/network waits still apply. Firewall remains active; port
22 and every non-SSH UFW rule remain unchanged.

Observed OpenSSH 10.6p1. UFW had the following rules (original indices 5/8):

```text
22/tcp       LIMIT IN  Anywhere       # ssh
22/tcp (v6)  LIMIT IN  Anywhere (v6)  # ssh
```

Deleted IPv6 rule 8, then IPv4 rule 5, and added `ufw allow 22/tcp comment
'ssh'`. Both now say `ALLOW IN`, with the same sources/comment. The original
iptables/ip6tables implementation used `recent --seconds 30 --hitcount 6`
and jumped to UFW's REJECT chain. No SSH references to recent/hashlimit or
limit chains remain; generic UFW logging-limit chains were retained. No
separate hashlimit rule was present. sshguard/fail2ban units were absent.

Created `/etc/ssh/sshd_config.d/00-siliconforest-no-lockout.conf`:

```text
PerSourcePenalties no
PerSourceMaxStartups none
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
```

`PerSourcePenalties` before was exactly:

```text
crash:90.000000 authfail:5.000000 noauth:1.000000 invaliduser:5.000000 grace-exceeded:10.000000 refuseconnection:10.000000 max:600.000000 min:15.000000 max-sources4:65536 max-sources6:65536 overflow:permissive overflow6:permissive
```

The initial audit unexpectedly found `PasswordAuthentication yes`; it was
changed to `no` to meet the owner's explicit key-only requirement. Public-key
authentication remains enabled and keyboard-interactive authentication remains
disabled. `Port 22`, `MaxStartups 10:30:100`, `MaxAuthTries 6`, and
`PerSourceMaxStartups none` are unchanged. The first two startup/authentication
caps bound concurrent unauthenticated connections/tries within one connection;
they are not a timed source-IP ban. The two effective changes were checked
against the full before/after `sshd -T` output, `sshd -t` passed, sshd was
reloaded, and the service remained active. No authentication bypass was used.

Revert evidence on Linux: `/root/sf-ssh-lockout-20261115T074536Z/` contains `report.json`, original
`user.rules`, `user6.rules`, `ufw`, `sshd_config`, `sshd_config.d/`, and complete
before/after UFW, sshd, iptables, ip6tables and nftables outputs. Workstation
receipts are private `runs/m1-usbc/cio04cm-linux.txt`,
`linux-ssh-change-report.json` and `cio04cm-ssh-fresh-1..8.txt`.
To restore the old rate limits, delete the `ALLOW 22/tcp` rule and run
`ufw limit 22/tcp comment 'ssh'` (both families), remove the new drop-in,
validate with `/usr/bin/sshd -t`, and reload sshd. Removing the entire drop-in
also restores the old password-enabled baseline; retain its last three lines
when re-enabling penalties if key-only authentication should remain in force.
Other SSH configuration files and `/usr/share/omarchy` were not changed.

**The e2fsck finding of dr41, and its fix.** `Inode N seems to contain garbage` for 24 inodes, all valid files and directories (their checksums recomputed from Linux: correct). They shared five inode-table blocks with inodes freed in dr41, when `sf-boot` removed what dr40 had left in `/private/tmp` and `/private/var/run` (several hundred files). The driver's delete paths wrote the freed inode after `ext4_inode_free_extents` had zeroed `i_block`, with `EXT4_EXTENTS_FL` still on: no extent header, which e2fsck's inode scan calls insane, and when more than half of a block's 16 inodes are so, every inode of the block is reported as garbage; `e2fsck -fy` would have cleared the valid ones (**never run `-fy` on this partition without reading what it says first**). Superblock state clean, journal start 0 throughout; both kernels read everything. No boot before had deleted more than a handful of files.
- Fix (`ext4-downgrade.patch`; the text of before is `ext4-downgrade.before-dr41.patch`, for the reproduction only): the three places that write a freed inode give it a valid empty extent root (`ext4_inode_init_extent_header`: magic, no entries, room for four, depth 0), which is what Linux's `ext4_ext_remove_space` leaves in a freed inode; the flag stays.
- QEMU (`tools/dr_qemu_massdelete.sh <name> tree|old [reuse <image>]`, `guest/qemu-massdelete.sh`, a scratch filesystem with p3's features): old driver rc 4 with 29 such lines (run md1); fixed driver, the same script, rc 0 (md2); fixed driver on the filesystem the old one left, new files and a second deletion: 250 old freed inodes become 190, none new (md3).
- The inodes the old driver left: `tools/sf_freed_inode_repair.py scan|repair <device> <backup file>|restore|compare`. It touches an inode only if the bitmap says free, links 0, extents flag, `i_block` all zero and the checksum correct; saves the originals first; rehearsed on copies of md1 and md3 (`e2fsck -fn` rc 0 afterwards; `compare`: only the listed inodes changed, to exactly the expected content; `restore` gives the image back bit for bit). **Not run on the M1** (a write to p3 from Linux is the owner's decision) and no longer needed for a clean check: 6 such inodes are left (`runs/m1-storage/dr42-freed-inode-scan.txt`), they heal when reused.
- `dr_desk_slot.sh DR_FSCK_OLD_SCAN=<an earlier scan>`: a plain boot whose e2fsck returns 4 is accepted only if a new scan lists no inode the earlier one does not, every inode called garbage lies in a block that is more than half old freed inodes, and e2fsck says nothing else.
- The kit was rebuilt from the fixed collection: `sf/kernel.bin` sha256 `093b61f1…` (was `a035b539…`), `sf/boot.bin` `81d58f1b…` and stage 1 `44c1ef0b…` unchanged; `test_kit.py` passes. **On SFBOOT since 13 November** (read back with `--show` after dr45): `sf/kernel.bin` `093b61f1…`, `sf/kernel.bin.old` the same file (the bench copies in between displaced `a035b539…`, which must not be booted with this disk root any more and is kept in `runs/sf-userland/`), `sf/boot.bin` `81d58f1b…`, `sf/boot.bin.old` the first kit's.
- **14 November (agent DESK, `SF-Z-desktop-night.md`):** `sf/kernel.bin` is `821cb932…`, the same kit with one change in the collection: PDM1SPIHID hands three- and four-finger swipes to userspace (`kern.pd_trackpad_swipe`); the kit built from the tree without that change gives `093b61f1…` byte for byte. Installed from Linux with `sf-bootbin-install.sh` (`sf/kernel.bin.old` = `093b61f1…`; `--rollback` brings it back), `sf/boot.bin` unchanged; `e2fsck -fn` of the partition clean before and after its first boot, two boots so far. `install/out/` holds the new kit; the one of before is `runs/m1-desktop-010/install-out-before-desk-swipes/`.
- **14 November, evening (agent VK, `SF-AC-vulkan.md`):** `sf/kernel.bin` is `56759b9f…` (10,165,568 bytes): DESK's kit `821cb932…` plus one change, LinuxKPI layer patch 0041 (`m1-rust-kext/lkpi/patches-layer/`: the taskqueue no longer reads a task after its function ran; a use after free that panicked the M1 at 21:06Z and 22:24Z when a GPU client died with work in flight). Only PDAgx and PDDcp change in the collection. Control: the kit rebuilt without the patch gives `821cb932…` byte for byte. Installed from Linux with `sf-bootbin-install.sh` (`sf/kernel.bin.old` = `821cb932…`; `--rollback` brings it back), `sf/boot.bin` unchanged. `e2fsck -fn` before: rc 4 with the findings left by the 21:06Z crash (inode 65144, blocks 636291-636295, group 17's free count; not repaired, the owner has the repair script). First boot 22:43Z: up, GPU-teardown tests passed with the console captured on the M5 (`SF-AC-vulkan.md`). `install/out/` holds the new kit; the one of before is `runs/m1-desktop-010/install-out-before-vk-taskqueue/`.
- **14-15 November, night (agent GFX2, `SF-AC-vulkan.md`, "Kernel round of agent GFX2"):** two kits on top of VK's, each booted once before the next: k1 `35044741…` = `56759b9f…` with `pdavd=1` (configuration record only: hardware video decode, `/dev/pdavd`); k2 `7ad3b4a9…` = k1 with `if_attach_nx=46` (no Skywalk compat netif on legacy interfaces: en0 counts received traffic) and IO80211Family counting packets out (the collection changes in IO80211Family and PDBwfm only). Control: the tree rebuilt without the changes gives `56759b9f…` byte for byte. `sf/kernel.bin` is `7ad3b4a9…` since 23:54Z, `sf/kernel.bin.old` = `35044741…` (`--rollback`); `56759b9f…` is in `runs/gfx2/kit-base/` and `runs/m1-desktop-010/install-out-before-gfx2-avd-wifi/`. `e2fsck -fn` before each: rc 4, the 21:06Z findings unchanged (not repaired). **Current kernel and how to rebuild it (for whoever stacks on it):** `sf/kernel.bin` = `7ad3b4a9e6e7e3e15ab038c9a61d824c6d1710f92b072ea418b207db911449d6` (10,165,696 bytes), `sf/boot.bin` = `81d58f1b…` (unchanged), `kernel.bin.old` = `35044741…`. Rebuild: `integrations/puredarwin/m1-native-boot/install/build.sh [outdir]` with `SF_PD` unset, at any commit from `616578780` on (the command line ends `… pdapciebridge=1 pdavd=1 if_attach_nx=46`; the collection is `m1-storage/build-kc-native-rw-halt.nix`, store path `2n7jr1yz8g9ksgcyscn9zagl7dw7sbhv-puredarwin-kc-arm64-0.1` here). `install/out/` holds exactly this kit (`build.json`). Built from this workstation's tree, whose uncommitted older-stream edits to `rebase/build-kc.nix`/`rebase/kernel.nix` only add arguments with the old values as defaults; a clean checkout was not tried.

**Open**
- Claude Code: runs (dr46, dr47) up to its login screen; nothing after a sign-in is tested, and there is no keychain (`SF-X-claude-code.md`). The cause was `xpc_copy_description()` in our libxpc ending the process when Security.framework described a connection; other `xpc_api_misuse` sites remain (listed there).
- **The SHA-256 of a program or library that has been run is not the file's**: the kernel rewrites pointer-authentication instructions in the page cache and `read` returns them (`SF-X-claude-code.md`, "A finding on the way"). Hash pushed files before running them; do not `cp` a library in use to keep it (hard link).
- **sftp** (and scp's default mode, and anything that uses the sftp subsystem): the tree's `sftp-server` ends with SIGBUS at start, in QEMU and on the M1, with or without libobjc inserted. `scp -O` works.
- The toybox `tar` of the base system fails after about eighty files of one archive; `gtar` (GNU tar) is in `/opt/homebrew/bin`.
- The second terminal's exit status at a bounded session's end (dr39 once).
- Not tried: `brew update`/`upgrade` (the client is a copy without history, `HOMEBREW_NO_AUTO_UPDATE=1`), anything after a sign-in, Claude Code in the desktop's own terminal. Known gaps of the Homebrew stream stand (`/dev/fd`, vim, neovim, the curl bottle; `SF-S-homebrew.md`). `/var/log` still grows; `sf-halt`'s 5 s are as before. The terminal still writes what is typed (first 1 MiB) to its records under `/private/var/run`, removed at the next boot.

**QEMU** (`tools/dr_qemu_userland.sh <name> quick`, `guest/qemu-userland.sh`; the M1's root image read-write, `/opt/homebrew` a second disk): run u2 passed the install script, sshd and the logins above, brew, bash, git, curl, tmux, node, `claude --version`.

## Goal
SiliconForest reads, and later boots from, the M1's internal NVMe storage instead of a RAM disk uploaded over USB. The internal disk holds the owner's macOS and Linux installs: **nothing in this ticket may modify it** until the owner authorises a specific write, and the code is built so that it cannot.

## Status (2026-11-11)
| Phase | State |
|---|---|
| 1 Facts | Sources, saved Apple device tree, hypervisor: done. Controller state as left by the boot chain: observed on the M1 (boot nv01, nothing written). Linux' view (read-only): partition layout, Identify, SMART baseline, kernel log, and reference SHA-256 of 30 ranges (1 MiB each: start incl. GPT, seven points across the disk, start/middle/end of every partition, end incl. backup GPT) in `runs/m1-storage/linux-a/`. |
| 2 Read-only driver `PDAppleNVMe` | **Proven on the M1** at level 2 (boot nv02) and level 3 (boot nv03): controller start, Identify equal to Linux', all comparable ranges equal through the driver's own device and through the block device, GPT parsed by the stack, every partition guarded, no write, clean stop (twice in nv03), Linux clean afterwards. **No further boots** until the reviewer says so. |
| 3 Filesystem groundwork | Source survey done; ext4 read-write on a 4096-byte-sector GPT disk and the behaviour of write-protected media shown in QEMU. |
| 4 Install design | Written below. Nothing of it is implemented or executed. |

Proven on the M1: what nv01 observed and what nv02 measured ("Result of nv02"). The block-device path is compile evidence.

## Phase 1: facts

### Hardware map (T8103 / J293)
From the Omacom tree (`arch/arm64/boot/dts/apple/t8103.dtsi`, `t8103-pmgr.dtsi`, `drivers/nvme/host/apple.c`, `drivers/soc/apple/sart.c`, `rtkit.c`), m1n1 `src/nvme.c`, and this machine's Apple device tree as saved by an earlier boot (`runs/m1-bringup-009/observed-original-adt.bin`; both agree):

| Item | Value |
|---|---|
| Coprocessor (ASC) registers | `/arm-io/ans` reg 0: `0x2_7740_0000`, CPU control at `+0x44` (RUN = bit 4) |
| Mailbox | ASC `+0x8000` = `0x2_7740_8000` (Linux `ans_mbox`, asc-mailbox-v4); AIC 583–586 |
| NVMe + linear SQ + NVMMU registers | `/arm-io/ans` reg 3: `0x2_7BCC_0000` (Linux maps 0x40000; ADT says 0x60000); AIC 590 |
| SART | `/arm-io/sart-ans`: `0x2_7BC5_0000`, version 2, 16 entries (`CONFIG` at `4*i`, `PADDR` at `0x40+4*i`, 4 KiB units, allow flags `0xff`) |
| Power | ADT `clock-gates = [103]` = `APCIE_ST` (PMGR `0x2_3B70_0418`), parents `ANS2` (`0x2_3B70_03F0`) and `APCIE` (`0x2_3B70_0348`, shared with Wi-Fi). Linux: `power-domains = <&ps_ans2>, <&ps_apcie_st>`, `resets = <&ps_ans2>` |
| RTKit | system endpoints only (management, crashlog, syslog, debug, ioreport, oslog); no application endpoint is used for NVMe. Shared buffers the firmware requests are host memory, allowed in the SART, named by physical address |
| DMA | no DART. Data and queue memory are physical addresses; each command needs a TCB in the NVMMU table (indexed by tag) carrying the same PRPs and a direction flag |
| Queues | one admin queue (Linux depth 2, tag 0) and one I/O queue (depth 64); tags are shared between them and limited to 64; "linear" submission: the command is written to slot `tag` and the tag to a doorbell (`0x2490c` admin, `0x24910` I/O); completion queues are ordinary |
| Register writes at bring-up | `LINEAR_SQ_CTRL=1`, `MAX_PEND_CMDS_CTRL=64|64<<16`, `NVMMU_NUM_TCBS=63`, `AQA`, `ASQ`, `ACQ`, NVMMU TCB bases, `CC` |
| Namespaces | ADT `namespaces` lists ids 1, 2, 3; Linux shows `nvme0n1` (the disk), `n2`, `n3` (small, firmware-owned). Only namespace 1 is read by this driver |
| LBA size | 4096, no metadata (Linux' Identify Namespace: `lbads=12`, `nsze=ncap=nuse=244276265`); the driver refuses anything else |
| Identify Controller as Linux reads it | `vid=0x106b`, model `APPLE SSD AP1024Q`, firmware `590`, `mdts=8` (1 MiB per command: the driver's limit of 256 blocks is exactly that), `nn=3`, `oacs=0x4`, `oncs=0x4`, `vwc=1`, `aerl=1`, `sqes=0x76`, `cqes=0x44`, `rtd3e=0` (shutdown timeout 5 s); Linux runs it with 2 queues, depth 64. Full field list for the comparison with Darwin: `runs/m1-storage/linux-a/nvme-id.txt` |
| Partition table as Linux sees it (read live, `lsblk`/sysfs, read-only) | 1 TB (244,276,265 blocks of 4096 bytes), GPT, 7 partitions: `p1` Apple Silicon boot (APFS, 500 MiB, starts at block 6), `p2` macOS APFS (~798 GiB), `p3` APFS (2.3 GiB, the Linux stub), `p4` EFI System (vfat, 500 MiB, mounted `/boot/efi`), `p5` ext4 2 GiB `/boot`, `p6` btrfs ~123 GiB (`/`, `/home`, …), `p7` Apple Silicon recovery (5 GiB, last). `nvme0n2` 3 MiB and `nvme0n3` 128 MiB are the firmware's namespaces. Linux' request limit: 1024 KiB per command |

### What the boot chain leaves behind
iBoot → m1n1 stage 1 (in the Linux stub volume) → stage 2 `boot.bin` on the EFI partition (this lab's "boot window" build) → RAM-loaded m1n1 → hypervisor → XNU.
- Stage 1 uses the NVMe to read stage 2 and then runs m1n1 `nvme_shutdown()`: delete I/O queues, `CC.SHN` normal, `CC.EN=0`, RTKit sleep, PMGR reset of `ANS2`. Linux' comment says the same: "an ANS NVMe controller is handed off with firmware shut down, in an NVMe disabled state, after a clean shutdown".
- The RAM-loaded m1n1 does not initialise the NVMe (its `nvme_shutdown()` is a no-op when not initialised).
- **Observed (boot nv01, `pdnvme=1`, nothing written, 8 CPUs, M1 back in Linux):** `ANS2=0x0f0020ff`, `APCIE_ST=0x0f0000ff` (both on), `APCIE=0x000000ff`; `CPU_CONTROL=0` (coprocessor stopped); both mailbox directions empty; `CC=0x00474000` (`EN=0`, `SHN=01`: the normal-shutdown notification m1n1 left, I/O SQ entry size 7), `CSTS=0`, `AQA=0x003f003f`, boot status 0, linear SQ still enabled, `NVMMU_NUM=0x3f`, `CAP=0x20f0010040` (timeout field `0xf0`: 120 s), `VS=1.1.0`. This is the stopped, cleanly shut down state Linux expects to find. The driver's rule stands: a coprocessor found running is left alone.
- SART as left by the boot chain: entries 0–3 with flags `0xff` (three in the `0x2_3Dxx_xxxx`/`0x2_3Exx_xxxx` range, one of `0x34000` bytes near the top of RAM) and entry 5 with flags `0xea` (`0x1a0000` bytes at `0x8_0003_4000`). As in Linux, every entry found with flags is treated as the boot loader's and never written; 11 entries are free, the driver needs about 4.
- iBoot's view in the ADT (`iop-ans-nub`: `pre-loaded`, `running`) describes its own hand-off to m1n1, not what m1n1 leaves.

### The hypervisor
m1n1's hypervisor maps all MMIO straight through except what it hooks (`proxyclient/m1n1/hv/__init__.py`; the lab launcher adds no ANS, SART or NVMe hook):
- hooked: the virtual UART; PMGR registers of `UART0`, the hypervisor's USB port (`ATCx_USB`, `ATCx_USB_AON`) and their parents (`UART_P`, `SIO`, `SIO_BUSIF`, `ATCx_COMMON`); CPU start registers; two power-gate overrides.
- **not** hooked: `ANS2`, `APCIE_ST`, `APCIE` (PMGR), the ASC, the mailbox, the NVMe/NVMMU registers, the SART. Nothing of the storage path is trapped or emulated; DMA is physical (stage 2 maps guest RAM 1:1), so PRPs are real addresses.
- Consequence: under the hypervisor the controller sees exactly what the driver writes. Nothing is filtered for us, and nothing is added. The one hypervisor-specific risk is the end of a boot: the bench resets the M1 from outside (`macvdmtool reboot`), so a controller still enabled at that moment is cut off like a power loss (see "Shutdown").

### What bring-up changes on the device, compared with Linux
| Step | Linux (every boot) | m1n1 stage 1 (every boot) | PDAppleNVMe |
|---|---|---|---|
| PMGR reset of `ANS2`, coprocessor start, RTKit boot | yes | yes | yes (same order) |
| SART entries for RTKit buffers | yes | yes | yes; cleared at stop |
| `CC.EN=1` | yes | yes | yes |
| Identify Controller / Namespace | yes | no | yes (controller, namespace 1) |
| Create I/O CQ + SQ | yes | yes | yes |
| Set Features (number of queues `0x07`; plus whatever `nvme_init_ctrl_finish`/`nvme_start_ctrl` decide: timestamp, power state, async events) | yes | no | **no** |
| Get Log Page | yes (SMART through hwmon, command effects) | no | SMART only |
| Read | yes | yes | yes |
| Write / Flush / Dataset Management (the root is mounted `discard=async`) | yes | no | **no: not in the binary** |
| Delete queues, `CC.SHN` normal shutdown, `CC.EN=0`, RTKit AP quiesced + IOP sleep, `CPU_CONTROL=0` | yes (`apple_nvme_shutdown`) | yes | yes (same sequence) |

SMART, read from Linux (read-only passthrough) around ordinary bench cycles and around nv02: `power_cycles` grows by **1 per machine reset** (two per bench cycle: Linux' reboot and the external reset), whoever starts the controller afterwards; `unsafe_shutdowns` stays at 107 (the external reset of a stopped controller does not count); `host_write_commands` grows by roughly 600–1400 at each Linux shutdown and boot (Linux' own writes); `media_errors` 0, `percent_used` 4. Linux' kernel log of an ordinary boot shows no recovery, replay or orphan line; `p4` (EFI), `p5` (ext4 `/boot`) and `p6` (btrfs) are mounted read-write on every boot.

## Phase 2: the read-only driver

Directory `integrations/puredarwin/m1-storage/` (GPL-2.0 kext; port of the Linux driver's T8103 path, polled RTKit client taken from `m1-sound/aop`).

| File | What |
|---|---|
| `kernel/pd_nvme_gate.c/.h` | the allowlist: plain C, linked into the kext and into the host test |
| `kernel/pd_ans.cpp/.h` | SART, RTKit boot/shutdown, controller enable, Identify, queue pair, read, SMART, shutdown. One thread, polled, no interrupt handler |
| `kernel/PDAppleNVMe.cpp` | boot arguments, engine thread, `/dev/pdnvme0`, the `IOBlockStorageDevice`, sysctls |
| `host-test/` | gate test and source checks |
| `user/pdnvme-test.c` | guest tool: SHA-256 of LBA ranges, sequential and random read timing, write attempt |
| `guest/`, `tools/nv_boot.sh`, `tools/nv_summary.sh` | bench boots (`BENCH_WHO=storage`, names `nv01`…) |
| `tools/linux_m1.sh` | read-only fact gathering and reference hashes on the M1's Linux |
| `build-kext.nix`, `build-kc.nix`, `build-pdnvme-test.nix` | recipes; the collection is the current default (`m1-touchbar/build-kc.nix`) plus this kext |

Collection: `/nix/store/a3xw8ld5pqg4vvhdgcshlyqcarxn9k2c-puredarwin-kc-arm64-0.1`, 0 overlapping segments, room left after `__PRELINK_INFO` `0xba000` (default: `0xe3000`; the kext costs `0x29000`).

### Read-only enforcement
1. **No write path exists.** The driver has names for seven commands only: admin Delete SQ, Create SQ, Get Log Page, Delete CQ, Create CQ, Identify; I/O Read. There is no code that builds Write, Write Zeroes, Flush, Dataset Management, Format, Sanitize, Firmware, Namespace Management, Set Features, Security or vendor commands, and no build option that adds one.
2. **One doorbell, behind a gate.** `PDAns::ring()` is the only statement that writes a submission doorbell. Before it, `pd_nvme_gate()` checks the command twice: as built, and again as the 64 bytes sitting in the queue slot the controller will read. The gate admits per queue: admin = the six opcodes above with their exact fields (Identify CNS 0/1 only, log page 2 only, queue id 1 only); I/O = opcode `0x02` on namespace 1, at most 256 blocks, inside the namespace, no FUA or other flags. Anything else is refused, the slot is zeroed, and `kern.pd_nvme.gate_rejected` is incremented. Because admin `0x00`/`0x01` share numbers with I/O Flush/Write, the check is per queue and on fields, not on the opcode alone.
3. **Direction.** The NVMMU's host-to-device flag is set only as Linux sets it for Create I/O CQ/SQ (the "data" is the queue's address); an I/O command without the device-to-host flag is refused.
4. **Bounce buffers.** Reads land in eight 1 MiB buffers the driver owns and are copied out; device DMA can never target other kernel memory, and a timed-out command's buffer is never reused.
5. **Above the driver.** `reportWriteProtection` is always true, so IOBlockStorageDriver publishes media that is not writable and no filesystem can mount it read-write; `doAsyncReadWrite` refuses any buffer that is not device-to-host before building anything; format and unmap are refused; synchronize sends nothing. `/dev/pdnvme0` refuses opens for writing. These refusals count in `kern.pd_nvme.write_refused`.
6. **Off by default, staged.** Without `pdnvme=` nothing is touched (no power domain, register or allocation). `pdnvme=1` observes; `2` starts the controller and offers only `/dev/pdnvme0` (no IOMedia, so no partition scheme or filesystem driver sees the disk); `3` also publishes the block device.
7. **Proof by test.**
   - Host (`host-test/run-host-tests.sh`, run by `nv_boot.sh` before every boot): every opcode on both queues (exactly 7 pairs allowed), 34 named destructive commands in realistic shapes, read bounds, 20 million random entries compared with an independently written statement of the rule; source checks (one doorbell write, after both gate calls; only allowlisted opcode names; write protection constant). Result: `GATEHOST PASSED`, `SRCHOST PASSED`.
   - Machine (guest script): `gate_selftest` = 7 allowed / 505 refused / 0 wrong; `gate_rejected` = 0 at the end; opening `/dev/pdnvme0`, `/dev/rdiskN`, `/dev/diskN`, a partition for writing must fail (if an open were granted, the tool writes block 0 back with its own contents, so even a carried-out write would change nothing); hashes equal Linux' before and after.

### What else could touch the disk once it is published (`pdnvme=3`)
- The RAM root has no diskarbitrationd, autodiskmount, fsck, Spotlight, swap or automount; `/etc/fstab` is empty; the lab boots single-user, so no daemon runs. Nothing mounts anything by itself.
- In the kernel: the partition schemes read the GPT; `ApfsFileSystemDriver` (PureDarwin's own) attaches to APFS-typed partitions and **reads** the owner's macOS containers (it writes only through an explicit user-client call, which needs a writable open and cannot get one); `Ext4FileSystemDriver`/`AppleFileSystemDriver` stay idle with `rd=md0`. A parser bug there could panic the kernel on real macOS data, which would end the boot without a controller shutdown. That is why level 2 exists and comes first, and why the first level-3 boot is its own step.

### Shutdown
The bench ends every boot with an external reset. The controller must already be stopped then:
- the guest script ends with `kern.pd_nvme.stop=1` and waits for `stopped-clean` before `PD_BENCH_DONE`;
- independently the driver stops the controller itself `pdnvme_ttl` seconds after it became ready (the wrapper sets 120; a boot reaches the guest script about 12 s after the kernel starts, and the bench's reset comes at the runtime bound of 300 s only if the script never finishes);
- `systemWillShutdown` does the same.
Not covered: a kernel panic or hang before the stop. The controller is then reset while enabled, as when the power button is held under Linux; with no write in flight there is nothing to lose, but `unsafe_shutdowns` would grow by one. It is checked after every boot.

### Boot plan (each step only after the previous one is clean; after each: M1 back in Linux, Linux root mounted without recovery, SMART compared)
| Boot | Level | Purpose |
|---|---|---|
| nv01 | 1 | observe: PMGR states, `CPU_CONTROL`, `CC`/`CSTS`, SART entries as left by m1n1. Writes nothing; does not start the controller |
| nv02 | 2 | **first controller start (needs the go).** RTKit, Identify vs Linux, hashes of the ranges through `/dev/pdnvme0`, throughput, latency, write refusal, SMART start/end, clean stop |
| nv03 | 3 | done: block device with the partition guard, GPT through the stack, same hashes through `/dev/rdisk0`, write attempts through the block layer, nothing mounted, nothing attached, second start |
Before nv02 and after each: `tools/linux_m1.sh all` and `hash` (ranges: start, GPT, seven points across the disk, start/middle/end of every partition, end). Ranges Linux itself changes between its two boots (inside its mounted filesystems) are reported separately: for those, equality with Darwin cannot be attributed.

### Result of nv02 (`pdnvme=2`, 8 CPUs, runtime bound 300 s, `pdnvme_ttl=120`; collection `/nix/store/a3xw8ld5pqg4vvhdgcshlyqcarxn9k2c-puredarwin-kc-arm64-0.1`)
One bench slot, nothing else in between: Linux baseline → Darwin boot → Linux again → `tools/nv_verdict.py` (`runs/m1-storage/nv02-*`).
- **Bring-up:** found stopped as in nv01; RTKit protocol 12 in 142 ms; controller ready 150 ms after the driver started; firmware asked for three buffers (crashlog, ioreport, syslog), placed in SART entries 4, 6, 7 and cleared at stop.
- **Identify:** every field of Identify Controller and Identify Namespace 1 that the two sides print is equal to Linux' (model, firmware, `mdts`, `nn`, sizes, LBA format).
- **Hashes (30 ranges of 1 MiB):** the 20 ranges on the GPTs and the APFS partitions (`p1`, `p2`, `p3`, `p7`) are identical in Linux before, under Darwin, and in Linux after. Of the 10 ranges inside Linux' mounted filesystems, 6 were not touched by Linux between its two readings and are equal under Darwin too; 4 (`p4` start, `p5` start and end, one in `p6`) differ between Linux-before and Linux-after, i.e. Linux rewrote them at its shutdown or boot, and nothing can be attributed there.
- **Writes:** `gate_rejected` 0; `write_refused` 2 = the two deliberate opens for writing of `/dev/pdnvme0`, both refused with "Read-only file system"; SMART read inside Darwin at start and end: `host_write_commands` +0, `data_units_written` +0, `host_read_commands` +6591 (= the driver's own count of reads), `unsafe_shutdowns` +0, `power_cycles` +0. Commands sent in the whole session: Identify ×2, Create CQ, Create SQ, Get Log Page ×3, Delete SQ, Delete CQ, Read ×6591. No error, timeout, stray completion or NVMMU invalidation failure.
- **Speed (polled, every read copied once, at most 8 commands of 1 MiB in flight):** sequential 512 MiB at 8 MiB per call: **710 MB/s**; 1 MiB per call: 608 MB/s; 128 KiB per call: 282 MB/s. Random 4 KiB reads over the whole disk, one at a time: **98 µs average, 99 µs median, 235 µs 99th percentile**, 832 µs worst, about 10,000 per second. Linux on the same disk reaches several times the sequential figure; the copy and the single polling thread are the limit here, not the disk.
- **Stop:** by the guest script 14 s after it began (the time-to-live of 120 s was not needed): queues deleted, normal shutdown notification completed (the firmware logged `NVMe shutdown start … saveCtx`), `CC.EN=0`, RTKit AP quiesced / IOP sleep, coprocessor stopped, state `stopped-clean`. The capture ended there; the external reset came with the controller off.
- **Linux afterwards:** verified by the bench; kernel log without recovery, replay or error lines for NVMe, btrfs, ext4 or vfat; `p4`, `p5`, `p6` mounted read-write as usual; controller `live`; `unsafe_shutdowns` 107 → 107; `media_errors` 0.
- **Open observation:** at start the firmware's own log said `gc_search_bm.c: Restarting rebuild from lba …` and, 150 ms later, `Finished rebuilding GC Search BM`. Linux' kernel log (three boots checked, including the one after nv02) shows only `boot mode normal` from the firmware. It is firmware-internal (no host write was counted) and whether Linux simply does not get this message, or something in our start provokes the rebuild, is not known.
- **Checker mistake, for the record:** the first run of the verdict reported one failure and wrote the STOP file: its "driver anomaly" pattern matched the script's own `NV_WRITE_REFUSED` marker. No condition was off; the pattern was corrected and the verdict re-run on the same data (`NV_VERDICT PASSED`); the stale file is kept as `runs/m1-storage/nv02-STOP-checker-false-positive.txt`.

### The partition guard (`pdnvme=3`)
Partition schemes, the tree's APFS container scheme and any other content driver for IOMedia compete in one match category (`IOStorage`), one winner per media. The kext now carries `PDAppleNVMeMediaGuard`: it enters that category with the highest score for every media that is a partition below `PDAppleNVMeDisk`, wins, and does nothing (it never opens the media). Effect: the whole disk still goes to the GPT scheme (the GPT is parsed by the stack), every partition gets its `/dev` nodes for raw read-only access (IOMediaBSDClient is in another category), and **no APFS container, filesystem or other content driver attaches to the owner's partitions**. Media of other disks is declined. The filesystem kexts themselves only act on a `mount`, which nothing issues. A future SiliconForest partition would be exempted by its type GUID. Proven in nv03.

### Result of nv03 (`pdnvme=3`, partition guard, 8 CPUs, runtime bound 300 s, `pdnvme_ttl=120`; collection `/nix/store/w73qb4lhzb9m6nkvm0npp39b4r2dzf90-puredarwin-kc-arm64-0.1`)
Same slot procedure as nv02 (`runs/m1-storage/nv03-*`); the verdict passed on its first run.
- **Guard first:** before any read through the block layer the guest script required `published=1 partitions=7 guarded=7 foreign=0` from `kern.pd_nvme.media` (a walk of the registry below the block device: anything that is not IOBlockStorageDriver, IOMedia, IOGUIDPartitionScheme, IOMediaBSDClient or the guard counts as foreign) and no mount from a disk; the same line was read again after the tests. The console names the seven guarded partitions and has no line from the APFS or any filesystem driver. `mount` showed the RAM root and devfs only.
- **GPT through the stack:** `gpt -r show /dev/rdisk0` lists 7 partitions whose starts and sizes equal Linux' (4096-byte blocks), plus primary and backup headers and tables.
- **Reads through the block device:** the 30 ranges through `/dev/rdisk0` hash exactly as through `/dev/pdnvme0` in the same boot; against Linux the result is the same as in nv02 (20 of 20 on the GPTs and APFS partitions identical before, under Darwin and after; 6 of 6 untouched ranges in Linux' filesystems equal; 4 rewritten by Linux itself). Sequential 256 MiB through `/dev/rdisk0`: 765 MB/s; through `/dev/pdnvme0` 751 / 609 / 282 MB/s (8 MiB / 1 MiB / 128 KiB per call); random 4 KiB: 87 µs average, 94 µs median, 140 µs 99th percentile.
- **Write attempts through the block layer:** opening `/dev/rdisk0`, `/dev/disk0` and `/dev/rdisk0s1` for writing (read-write and write-only) fails with "Permission denied" in the storage family, above the driver; `/dev/pdnvme0` as before. `write_refused` = 2 (the two opens of `/dev/pdnvme0`; the block-layer attempts never reach the driver), `gate_rejected` = 0.
- **SMART, whole 512-byte page, compared byte by byte** around each start and stop inside Darwin: only the temperature, `data_units_read` and `host_read_commands` change; nothing in the write counters, `power_cycles`, `unsafe_shutdowns`, error fields or the vendor area. From Linux: `unsafe_shutdowns` 107 → 107.
- **Second start in the same boot** (stop cleanly, start again as Linux does on a driver rebind, hash all ranges, stop cleanly again): ready in 138 ms, same digest over all ranges, stopped cleanly.
- **Linux afterwards:** verified, no recovery/replay/error line, `p4`/`p5`/`p6` mounted as usual.

### The firmware's "GC Search BM" message
- nv02, first start: `Restarting rebuild from lba 121836800`, then `Finished rebuilding GC Search BM`. nv03, first start: the same with `lba 131446574`. nv03, second start in the same boot: **no rebuild message**, only `boot mode normal`.
- Linux prints every firmware log line (`dev_info`), and its journal of 49 boots has `boot mode normal` 49 times and never a `gc_search_bm` line. m1n1 prints such lines only on its own console; Linux' journal does not hold the boot loaders' output.
- Reading: the rebuild is something the firmware does once after a machine reset, in the first session that gets that far, and it is finished within about 150 ms. On the way to Linux that session is U-Boot's (it reads the kernel from the disk before Linux starts the controller again), so Linux never sees it; on our way there is no U-Boot, so we do. The second-start test supports this (not repeated within one power cycle). **Not shown directly:** U-Boot's own firmware log was not captured, and stage-1 m1n1 runs a short session before both paths without, apparently, completing it. The start sequence itself was compared with Linux' again (reset pulse, RTKit boot order, registers, Identify, queue creation): no difference found other than the commands we leave out.
- Also seen, at the second stop only: `Util_Reset_FailGeom Info` and `Comressing DM.version: 178` between the usual `NVMe shutdown start` and `saveCtx … took 171 ms`. The firmware saves its context at every clean shutdown (m1n1's, U-Boot's, Linux' and ours alike); what these two extra lines mean is not known, and Linux' journal ends before its own shutdown, so there is nothing to compare with. No SMART field moved.

### Not done in this driver
Next, host-side only until a new go: interrupts and zero-copy reads. Not done: interrupts (AIC 590; the engine polls, spinning only while a read is out), zero-copy DMA (everything is copied once), more than 8 reads in flight, namespaces 2 and 3, error recovery beyond "stop using the controller and shut it down", sleep.

## Phase 3: filesystem
Survey of the pinned PureDarwin tree (`e7aa00a6`) and the booted collection:
| Filesystem | In our collection | Read-write | Root on a real disk | Notes |
|---|---|---|---|---|
| ext4 (PureDarwin `ext4.kext`, JBD2 journal with replay) | yes | yes | **yes** | answers the kernel's root-authentication ioctl; every arm64 image of the tree roots on it; QEMU's `virt` image boots it from a GPT disk today. `mkfs`/`fsck` exist on the Linux side |
| APFS (PureDarwin's own `apfs.kext` on `libapfsrw`, MIT) | yes | yes, for 4096-byte-block, unencrypted, single-volume containers | possible | young; no `fsck`; macOS would see and might touch it |
| HFS+ (Apple sources, journaled) | yes | yes | **no**: returns `ENOTTY` for the root-authentication ioctl, the kernel panics (`bsd_init.c:934`); no `newfs_hfs`/`fsck_hfs` in the tree | |
| FAT (`msdosfs`) | yes | yes | no | for the EFI partition |
| exFAT, UDF, NTFS, ZFS, UFS, NFS client, `vn` file-backed disks | no | | | |
Partition schemes built: GPT, MBR, Apple Partition Map, plus the APFS container scheme. `IOStorageFamily-260.100.1` is in the collection. An open `IONVMEFamily` exists in the tree but is for PCI NVMe controllers; it cannot drive the ANS.

**Choice: ext4** in a GPT partition with 4096-byte blocks.

Root selection facts that matter for the design:
- today: `rd=md0`, a memory device (not IOMedia). Our whole disk therefore becomes `disk0`.
- without `rd=md*`, the root must come from `rd=diskNsM`, or a `root-matching` dictionary / `boot-uuid` in `/chosen` published by the loader.
- if the root device does not appear within 60 s, this kernel calls `IOSetRecoveryBoot` (**an NVRAM write**: `boot-command=recover-system`) and panics, unless `wdt=-1` is in the boot arguments. No NVRAM controller was found in our platform code, so the write probably goes nowhere, but that must be shown, or the path patched out, before any boot waits for a disk root.

Demonstration without the M1's disk (QEMU `virt` under TCG, the same arm64 kernel binaries, through the other stream's `qemu-dev` tools unmodified; files in `runs/m1-storage/qemu/`):
- the system itself roots on ext4 from a GPT virtio disk, read-write (`root_device on / (ext4, local, root file system)`);
- a second disk with **4096-byte sectors**, a GPT written for that sector size and one ext4 partition (4096-byte blocks, made on the host): the GPT is parsed (`256 60000 1 GPT part - Linux data`), the partition mounts read-write, a text file and a 20 MiB file are written, unmount, remount, both read back; on the host afterwards `e2fsck -fn` is clean and the text file is there;
- the same image attached **read-only** (the stack sees write-protected media, as it will with PDAppleNVMe): a read-write mount fails with "Permission denied", `-o rdonly` mounts and lists, writes to `/dev/rdiskN` and `/dev/diskNs1` fail with "Permission denied", and the image's SHA-256 is unchanged afterwards.
- two things seen on the way: (1) **`diskN` numbers are not stable**: the system disk was `disk0` in some boots and `disk1` in others, so a root must be named by partition UUID (`boot-uuid`/`root-matching`), never `rd=disk0sN`; (2) the 20 MiB write took 100 s (about 0.2 MB/s) under TCG. Whether that is the emulator or the ext4 driver's write path is not known and must be measured on real hardware before ext4 is called fast.
- not shown: a root on the 4096-byte-sector disk (the root in these runs is the 512-byte system disk); journal replay after an unclean stop; anything about APFS.

## Phase 4: design for the real install (nothing here is executed)

### Partition
A dedicated GPT partition, 32–64 GiB, ext4, with **its own type GUID** (not the generic Linux one, so that no rule can ever confuse it with the Linux root) and the name `SiliconForest`.
| Option | Who, from where, with what | Risk | Undo |
|---|---|---|---|
| P1 (recommended) shrink macOS' APFS container | the owner, in macOS: `diskutil apfs resizeContainer` (what the Asahi installer does), then add the partition in the free space | APFS shrinks online and is the vendor's supported path; needs free space in macOS | delete the partition, grow the container back |
| P2 shrink the Linux btrfs root | the owner, in Linux: `btrfs filesystem resize`, then shrink `p6` and add a partition (`parted`/`sfdisk`, 4096-byte sectors) | two manual steps that must agree; a wrong order or size destroys the Linux root; the recovery partition must stay last | delete, grow back; **back up first** |
| P3 no partition: an image file | — | our kernel has no file-backed disk driver and no btrfs; not viable | |
`mkfs.ext4 -b 4096` on the new partition is run from Linux (a write to the new partition only). Every step above is a write to the internal disk and needs the owner's explicit go; a mistake in P1/P2 is the only way in this plan to lose existing data, so a backup precedes it.

### Write fence in the driver
A separate kext build (`PDAppleNVMeRW`), never in a default collection:
- the fence is `[first, last]` LBA of exactly one partition, found by the driver itself at start: primary and backup GPT must agree and pass their CRCs, the entry must carry the SiliconForest type GUID, the name, and the partition's unique GUID given in the boot arguments. Any mismatch: the build behaves as the read-only one.
- the gate gains Write and Flush (and optionally deallocate) on the I/O queue; Write/deallocate pass only if `first <= slba` and `slba + n - 1 <= last`, checked on the queue bytes as today; a second, independently coded comparison sits next to the doorbell. A counter `fence_rejected` must stay 0. The host test's random comparison is extended to the fence.
- two block devices: the whole disk stays write-protected (the GPT and every other partition cannot be written through any path); a second device exposes only the fenced range as its own writable disk, so a filesystem bug cannot address anything outside it.
- Linux' deferred-flush behaviour for this controller is ported with the write path.
- acceptance before first use on the M1's disk: the same build against a scratch target the owner chooses (an external disk is not possible with this controller, so: QEMU for the fence logic, then the real partition while it holds nothing).

### Boot chain
Today SiliconForest has only ever run as a guest of the m1n1 hypervisor, started by Python on a second Mac. Whether our kernel runs without the hypervisor (CPU start, SPRR/GXF, the virtual UART) is **not known** and is the largest open item; it belongs to the boot stream.
| Option | What changes on the disk | Needs from the owner | Risk | Undo | Cold boot (estimate) |
|---|---|---|---|---|---|
| B0 root on NVMe, still launched from the M5 | the SiliconForest partition only | P-step, go for the write build | none to the boot chain | stop using it | as today minus the RAM-disk upload |
| B1 (recommended next) stage-2 m1n1 on the EFI partition gains a native loader: default Linux unchanged, SiliconForest on a held key or a file flag | one file on the EFI partition (`boot.bin`), as was already done once for the boot window; kernel collection and device-tree overlay as files on the EFI partition | go for one file replacement; a loader in m1n1 (C; today the XNU loader exists only in Python) | a bad `boot.bin` stops Linux from booting until the saved original is put back from macOS or recovery; no boot-policy change | restore the saved `boot.bin` | iBoot ~7 s + m1n1 ~3 s + load ~1 s + kernel ~6 s + userland: **about 17–20 s** to a session, if the 10 s USB window is dropped for this path |
| B2 chain from U-Boot/GRUB (an EFI application that loads XNU) | files on the EFI partition, one boot menu entry | go for the files | lowest for Linux (its path is untouched); most engineering: XNU must be entered after U-Boot has taken over the machine, and the Apple device tree must be recovered | delete the files and the entry | B1 + U-Boot ~4–6 s: **about 23–26 s** |
| B3 own boot object: a second stub volume and boot policy, like a second Linux install | new APFS volume, **boot policy / Startup Security change** in recovery (`kmutil configure-boot` from 1TR) | physical presence in recovery, the machine password | touches the Secure Enclave's boot policies; a mistake does not harm the other systems' data but can require recovery to repair | delete the volume and its policy | as B1 (no key hold) |
None is irreversible in the strict sense. The steps that deserve the word in practice: any repartitioning without a backup, and B3's boot-policy change (cannot be tested beforehand, must be done at the machine).

### Open prerequisites (recorded, not started)
- **Does our kernel run without the hypervisor?** Never tried. What a safe test with the existing rig would need, with no disk involved: a launcher on the M5 that, like today's, RAM-loads the collection and the RAM root over USB into the m1n1 proxy but then enters XNU directly (m1n1's `chainload.py` already loads a Mach-O and jumps without the hypervisor) with iBoot's device tree and boot arguments. What the hypervisor does for us today and would have to be replaced or shown unnecessary: the console (virtual UART over the proxy's USB port: natively the guest owns that port, so the console must come from the real UART pins or the screen), secondary CPU start, SPRR/GXF (emulated today unless the registers are unlocked), the PMGR hooks that keep the proxy's USB alive, and the capture of faults. The bench's return path (external reset, Linux check) works unchanged. Risk: a hang with no console; cost: one external reset. Owner: the boot stream.
- **The 60 s root wait.** With a disk root that does not appear, this kernel calls `IOSetRecoveryBoot(BSD_BOOTFAIL_MEDIA_MISSING)`: it would set NVRAM `40A0DDD2-…:boot-picker-bringup-reason=missing-boot-media` and `boot-command=recover-system`, sync, and panic. From the sources: the only NVRAM controllers in the tree belong to the x86 platform experts; PDArmPlatformExpert registers none, nothing in the lab drives the SPI flash that holds the M1's NVRAM, and the hypervisor offers none. So under the hypervisor and natively alike the variables could at most land in the kernel's in-memory copy, and the code would then block forever waiting for the `IONVRAM` resource instead of panicking. That is inferred, not observed. No storage boot can reach it: all of them boot with `rd=md0`. Before any boot waits for a disk root: `wdt=-1` in its boot arguments and the call patched out of the kernel, so that it does not depend on the absence of a driver.

### Order proposed
1. Read-only driver proven (this ticket's phase 2).
2. Partition by the owner (P1), ext4 made from Linux, root file tree copied from Linux (Linux can mount it).
3. Write-fenced build proven on that empty partition; B0: root on NVMe, launched as today. This already gives "a real disk" and removes the upload.
4. Root-wait/NVRAM path settled, native loader (B1) once the kernel is shown to run without the hypervisor.

## After a crash during a GPU test (14 November 2026, ~21:06Z; recorded by COCOA4)
- The M1 stopped during a headless Vulkan test with the root mounted read-write and came up in Linux. Linux
  `e2fsck -fn` then: rc 4. The next SiliconForest mount found the journal dirty and replayed 17 blocks (one
  transaction), mounted fine; after a sync and a clean reboot, Linux `e2fsck -fn` still rc 4, with only this left:
  one deleted inode with a zero dtime that is still set in the inode bitmap, its 5 blocks still set in the block
  bitmap, the free-block counts (one group and the total) off by 3. Passes 2 to 4 clean (no directory entry or
  named file damaged); the rest of the listing is "extent tree could be narrower" (advice, not errors).
- Not repaired (no `-fy`; the owner decides). `tools/sf_freed_inode_repair.py` does not cover this variant: it
  touches an inode only when the bitmap already says free; here the bitmap says used.
- Open question (not investigated): whether the driver journals an unlink's bitmap and dtime updates in the same
  transaction as the inode's release; a crash between them would leave exactly this.

## Decisions for the owner
1. Go for the first controller start (nv02) after reading the observe boot.
2. P1 or P2, and the size.
3. ext4 (recommended) or APFS for the root.
4. B1, B2 or B3 as the target chain; B0 first in any case.

## Rules
Inherits SF-B. In addition: no boot with `pdnvme=2` or `3` without the host tests passing; after every such boot the Linux check (`linux_m1.sh all`: root mounted without recovery, no NVMe error, SMART compared); any anomaly creates `runs/m1-storage/STOP` and stops all storage boots.


### Read-only filesystem check during GPU-wedge recovery, 2026-11-15

The authorized recovery from the23:20 devfs/DRM-open freeze used one M5 reset,
one Linux visit, the normal installer for qualified CIO29/a149e487… and a
one-time SiliconForest boot. Root identity/label/geometry and unmounted state
were checked before e2fsck -fn. It returned4: journal replay skipped, block and
inode bitmap differences, free-block totals off by3, plus many extent-narrowing
suggestions. No repair or journal replay was requested; this is not a clean
post-replay filesystem assessment. Full output remains private at
runs/sftb/gpu-wedge-2320/linux-fsck-and-install.txt.

At23:37:34Z the actual .next process, Wayland socket/inject directory and a
non-input stats request/reply were verified on the qualified kernel. UART
reader and reroute process stopped; CODEX-TB released all locks and returned to
its machine pause. Default boot stayed Linux; only one-time bless was used.

Recovery command correction: CODEX-TB initially requested /sbin/reboot with
background stderr redirected. The supported fenced installed command is
/bin/reboot (sf-command -> /sbin/sf-halt). Thus this attempt does not prove the
supported clean shutdown hung on the wedged processes. Future recovery must
use /bin/reboot and retain status/stderr; no extra reboot was performed to
retest after this recovery. No SSH/firewall/authentication configuration changed.


### Devfs-fix install filesystem check, 2026-11-16

The supported /bin/reboot reached Linux cleanly in the devfs-only install cycle;
no external reset was needed. Root identity, geometry and unmounted state were
checked before e2fsck -fn -E no_optimize_extents. Exit4 includes the older
zero-dtime deleted inode, bitmap/free-count differences, and two beyond-EOF
extent records in Firefox YouTube SQLite databases (one owner profile, one
throwaway QA profile). Full output, debugfs inode/path evidence and dumpe2fs
features are saved privately in runs/sftb/devfs-install-0041/. No filesystem
repair was attempted because this exceeds the current conditional bitmap/count
repair permission. Normal SFBOOT installation, one-time bless and subsequent
SiliconForest root mount succeeded; this is not a clean filesystem qualification.
The kernel rollback remains CIO29.


## Upstream raw-device superblock and4KiB image review (2026-11-16)

Selected ca1fd5ed ports ext4.util's read_superblock to whole logical device
blocks from offset0, copying the superblock at byte1024. It is a userspace
probe change, separate from kernel45's EOF allocation fix. Real arm64 build
and actual-source sanitized controls pass. Dedicated QEMU004 probes the raw
4096-byte scratch partition: old utility returns254/unrecognized, new253/IO
success; the initial cooked-node control accepted both. No installed utility
or owner-root probe result is claimed from that rehearsal.

Upstream aa82116c converts a newly created512-byte GPT image to4096-byte GPT
units without moving its partition bytes; its T8103 image recipe selects this
format. Our installer already checks4096-byte primary/backup GPT and exact
existing partition spans, then installs SFBOOT files without rewriting GPT.
Do not run the converter on the owner's disk. Its source assumes valid input
and a standard layout; it is no general repair tool. A built1.34GB regular virt
image conversion passes both header/table CRCs, preserves both partition byte
spans and full content SHA256 hashes (805306368 and536870912bytes). This is an
offline image-format test, not a T8103 boot or authorization for table writes.
Ports, tests and isolated recipes: integrations/puredarwin/upstream-12377/.

### 2026-11-16 kernel 47 Wi-Fi cycle: filesystem unchanged

The clean SiliconForest shutdown reached Linux on the second strict-key SSH
probe (first timed out); no external reset was used. Before the normal SFBOOT
installer, the unmounted root identity was checked and `e2fsck -fn -E
no_optimize_extents` returned 4. The extent/i_size/bitmap/count classes match
the retained kernel 46 precheck, with one previous QA-file finding absent;
there were no new findings in this comparison. No repair was performed and
the root is not claimed clean. Kernel 47 preserves the exact qualified 45/46
ext4 driver and changes only Wi-Fi PM. The normal installer verified 47 and
qualified 46 rollback; no partition/GPT or default-boot change was made.

The additional failures first recorded after the root ENOSPC episode remain
under offline investigation. The actual frozen writer's partial allocation
and failed extending-write paths are now explicit hypotheses with disposable
VM low-space/failure controls planned in `m1-ext4-eof/README.md`. FXNET's
13–33 ms buffered-append tail is a separate stage-timing task. No owner-root
space exhaustion, storage error injection or barrier reduction is proposed.


### 2026-11-16: kernel 48 metadata read shortcut and latency attribution

Kernel 48 changes only ext4: when the authoritative full metadata image is in
the pending journal transaction, use it before issuing a physical disk read.
The prior reader read disk first and then overlaid that image. A native append
stackshot caught the writer blocked on the mount mutex, owned by a compositor
close/inactive path freeing extents and reading metadata synchronously through
NVMe. Other snapshots caught allocation zeroing. The new reader preserves the
journal, two flushes, write allocation/EOF fix, NVMe and all frozen write-gate/
fence files. It is described and reproduced in `m1-ext4-journal-cache/README.md`.

Source-aware ASan/UBSan controls, full SDK and real QEMU pass: runtime cache
0/1, independent full byte oracles for 64 metadata and 29 write/resize cases,
8 MiB USB read, clean scratch unmount and e2fsck -fn status 0. An initial
missing-sysctl-registration failure was retained and fixed before this pass.
Only ext4's UUID changes from 47. Native cache 0/1/1/0 controls, 4000 appends
and 128 metadata cases pass full data checks; enabled phases avoid about
12000 bread calls each, disabled phases have no hits. Neither controls nor
enabled phases reproduce the original append tail, so there is no demonstrated
append speedup. Metadata operation timings also remain variable.

A bounded native 64-cycle write/fsync/close/unlink trace on 48 finds median
fsync 11.84 ms and unlink 11.30 ms. Of 32 writer stack samples, 25 sleep in
NVMe flushes issued by `ext4_jnl_commit`: 16 at the post-commit-record flush,
nine at the post-checkpoint flush, confirmed against the exact bundle's
symbols and disassembly. This is a sample count, not wall-time attribution.
Unlink's captured chain enters `VNOP_FSYNC` through vnode reclamation (`vclean`),
which commits the mount transaction synchronously. Other samples show a
physical group-descriptor read or mount-lock contention. The reader shortcut
cannot eliminate these two ordered flushes. Earlier 40000 ordinary appends
coincided with only ten global flushes including background activity; there
is no commit-per-ordinary-write conclusion. Queue/device flush latency and
reclaim batching are next attribution questions; durability is unchanged.

The installation used the normal installer, exact qualified 47 rollback,
notice/locks/UART and one-time boot. Clean shutdown reached Linux without an
external reset; the read-only root check retained the previous error classes,
status 4, with no repair or new finding in the comparison. Root is not clean.
The standard desktop, GL/Vulkan and six Firefox paint/natural-quit gates pass;
UART and locks are released. No root-fill or error injection was performed.


### Wind-down hand-over, 2026-11-16: delete stalls remain unfixed

Kernel 48 is still normal. Its capsule SHA256 is
`2ec8d7c41e1395cf092cc24f8f17b8393b2753d8f6910d6f192cb6c7164c904f`,
with runtime UUID `E042DBE8-9934-4A88-A71A-16DCAD877E05` and stage-two
boot SHA256 `81d58f1b40e8139dc797ceac8314bb74a6308ccf18b28e42f1c902c7b4912e3e`.
Its private complete kit is `runs/sftb/firefox-install-0048/kit/`.
Qualified 47 rollback is `runs/sftb/firefox-install-0047/kit/`, capsule
`adad24e98cb17873fb671d97e6c1c0ea4d2f769daf43abc2dacbda6f466802d0`.
The installer receipt confirms `sf/kernel.bin` is 48 and
`sf/kernel.bin.old` is 47. Important: `sf/boot.bin.old` is a retained legacy
14 MB image too large for the current stage-one footprint. Do not blindly
swap both old files. Restore the complete verified 47 pair through the normal
installer; its current boot.bin is the qualified small stage-two image.

In a later recovery/install session, use the private SFBOOT identifier from
the verified inventory, never a guessed partition. The normal command is
`sudo sf-bootbin-install.sh "$SFBOOT_ID" kit/boot.bin kit/kernel.bin`.
`--show` gives a read-only receipt. The script is
`integrations/puredarwin/m1-native-boot/install/linux/sf-bootbin-install.sh`.
Use only `asahi-bless --next --set-boot SiliconForest -y`; default Linux stays.
No install, reboot or filesystem repair was performed during wind-down.
Known extent/i_size/bitmap/count errors remain in the last read-only fn check;
root is not claimed clean. Kernel 48's existing metadata read shortcut can be
switched with `sysctl kern.pd_ext4.journal_cache=0` or `=1` (default 1), with
read counters at `kern.pd_ext4.cache_stats`. It does not fix delete latency.

The owner reported failed page loads and 5–10 second Terminal startup during
about 24 GB of profile cleanup. Our native evidence already identifies a
mechanism: ordinary appends can wait on the mount mutex while another thread
performs synchronous metadata reads; unlink can enter `vclean -> VNOP_FSYNC`
and commit the mount transaction, sleeping in the two NVMe flushes. The
64-cycle trace measured fsync/unlink medians 11.84/11.30 ms, with 25 of 32
writer samples in those flush waits. That sample fraction is not a wall-time
percentage or proof of every owner-visible stall. No simultaneous 2 GiB
rm/1000-append benchmark has been run yet. No asynchronous journal rework
source or install exists; only the following design and qualification plan.

The proposed design follows the running/committing transaction separation
in the pinned `sources/gpu-plan/linux-aurora/fs/jbd2` and `fs/ext4`, without
pretending the current small port already has JBD2's transaction machinery:

1. Introduce explicit transaction handles, monotonically ordered generations,
   metadata credits and separate running and immutable committing images.
   Reserve credits before a VNOP starts changing metadata; a full 128-block
   array must never force a commit halfway through a multi-block operation.
   Preserve running-over-committing-over-disk read precedence while an older
   checkpoint is in flight. Simply releasing the mount mutex around the
   existing mutable `j_tx` array would race readers/writers and corrupt data.
2. A per-mount sleeping worker requests a commit on a bounded slow deadline
   or credit pressure. Snapshot at a completed operation boundary under the
   metadata lock, then release it before journal/device I/O. Serialize commit
   and checkpoint order with their own state/lock. New writers may update a
   different running transaction while the immutable snapshot is flushed.
   Do not create a fast polling loop, hold an inode/allocator lock across the
   wait, or merely move global blocking into another lock that every writer
   still needs. Physical metadata cache misses and allocation zeroing also
   need a versioned cache/per-buffer protocol; the installed shortcut alone
   does not remove every synchronous read from the mount lock.
3. Explicit fsync must push that file's dirty UBC data, capture the dependency
   generation and wait for its durable completion, outside the mount lock.
   Preserve both ordered flushes, propagate errors and keep failed committed
   images available for replay. Current helpers ignore some flush/commit
   errors; an asynchronous design must make error state sticky and visible.
   Distinguish an internal reclaim flush from a real application fsync using
   the actual XNU call contract. Do not simply ignore all fsync calls on
   unlinked files: an application can still own and fsync an open unlinked
   vnode. Reclaim should enqueue its metadata generation without forcing a
   whole-mount commit on each removed vnode.
4. Defer reuse of freed blocks until the relevant transaction is durable,
   and implement the required revoke/forget behavior across both images.
   Otherwise a new allocation could overwrite data or metadata still named
   by the prior committed transaction or its checkpoint. Preserve orphan
   list recovery and operation atomicity during unlink/rename/reallocation.
   Data-before-metadata ordering must cover the new concurrent transaction.
5. Freeze, unmount and shutdown must stop new handles, drain worker/data and
   wait for all durable generations, then release the mount. Worker lifetime
   must hold a mount reference; failure/cancel paths must not leave dangling
   timers or lose dirty images. The frozen NVMe write-gate/fence files and
   halt constants remain untouched. All changes belong to the filesystem.

Qualification before any install must include actual-function host concurrency
and fault controls, then disposable real QEMU power-cut/replay tests. Start
from fresh scratch images and cut at descriptor/data/commit writes, each
ordered flush, checkpoint and journal-superblock advancement. Reboot the VM,
allow its real replay path, verify acknowledged-fsync data and namespace
oracles, then unmount and run e2fsck -fn. Test rename/unlink with open files,
reallocation after free, metadata overflow, ENOSPC, short/error I/O, failed
flush, freeze/unmount races and repeated mounts. Unsynced operations may be
lost; acknowledged durable ones may not. Preserve failed images and markers.
Host fake disks and clean-unmount fsck alone cannot establish crash consistency.

The requested performance test is still pending. Create a fresh, bounded 2 GiB
owned tree with a documented fixed file-count/size distribution; prepare and
fsync it before timing. Run its rm concurrently with exactly 1000 100-byte
appends and file creations in another process. Save raw per-operation wall
and thread-CPU latency, p50/p99/max, deletion duration, flush counts and actual
cohort/boot identity. Run the same fixed workload on qualified 48 and the
candidate, with unrelated agent cleanup stopped. Report create and append
latencies separately; do not include fixture preparation or cleanup in timing.
Use only disposable VM scratch until crash gates pass; any later native run
must stop for active owner input and never approach root ENOSPC.

The next five steps are: implement and review the transaction/handle model;
qualify real snapshot/error/reuse behavior in host controls; run QEMU crash
cuts and replay oracles; compare the fixed concurrent-delete benchmark; only
then prepare a later noticed install with exact 48 rollback and full desktop,
GL/Vulkan/Firefox/data gates. Build/check commands for installed 48 remain
`nix-build --extra-experimental-features 'nix-command flakes'
integrations/puredarwin/m1-ext4-journal-cache/build-kc.nix --no-out-link`,
`python3 integrations/puredarwin/m1-ext4-journal-cache/test-source.py`, and the
six required AGENTS.md commands. QEMU reproduction uses
`python3 integrations/puredarwin/upstream-12377/run-qemu.py --output <new-run>
--image-recipe integrations/puredarwin/m1-ext4-journal-cache/build-qemu-image.nix
--guest-script integrations/puredarwin/m1-ext4-journal-cache/qemu-guest.sh`,
followed by `m1-ext4-journal-cache/verify-qemu.py`. Those are the existing clean
rehearsals, not the still-unwritten crash-consistency harness.
