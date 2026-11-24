# Installing SiliconForest on the M1's disk: the kit and the steps

> **Installed on 13 November 2026** (steps 1 to 6 done by the owner). The first start stopped
> safely in stage 1: the first kit's `sf/boot.bin` (stage 2 + configuration + collection in one
> 14.4 MB file) was larger than a stage 1 can start. The fix needed no new install: `sf/boot.bin`
> is now stage 2 alone and reads `sf/kernel.bin` itself; both were put on SFBOOT from Linux
> ("Two files, and why" below; "What is proven" at the end). A fresh install from this kit gets
> the same two files. The steps that write (4 and 6) are the owner's.
> This replaces sections 4 to 8 of `../b1/INSTALL-PROCEDURE.md`; its sections 1 to 3 (what
> exists afterwards, where the space is, what the stub needs from Apple) still describe the result.

## The route

The Asahi Linux installer itself (source at commit `e93743d`, the code that made the Linux
entry on this machine), run on macOS with **our** installer data and package. Its code makes
the APFS container with its four volumes, the stub macOS with its paired recoveryOS, the FAT
partition, and the boot object; `step2.sh` in recovery sets the boot policy. What is ours:

| Piece | File in the kit | What it is |
|---|---|---|
| OS entry | `installer/installer_data.json` | one entry, "SiliconForest": boot object `m1n1.bin`, next object `sf/boot.bin`, one partition `SFBOOT` (type EFI, FAT32, 500 MiB, filled from the package's `esp/` tree) |
| Stage 1 | `installer/boot/m1n1.bin` | m1n1 at the lab's pin, `RELEASE=1 CHAINLOADING=1`, our logo, the installed variant (below). The installer appends `chainload=<PARTUUID of SFBOOT>;sf/boot.bin` and puts the result into the stub as the boot object |
| Package | `installer/os/siliconforest.zip` | `esp/sf/boot.bin` (stage 2 alone, 4.1 MB), `esp/sf/kernel.bin` (configuration + kernel collection as a record list, 10.2 MB) and the icon |
| Icon | `installer/logo.icns` | from `branding/siliconforest-icon.svg` |
| Menu | `installer/main.py` | the installer's, with one hunk (`installer/main-menu.patch`): only "install into free space", "repair an incomplete installation" and "quit" are offered. Resizing a container, replacing the m1n1 of another stub, rebuilding firmware and other disks are not on the menu |

Everything else in `installer/` is the installer's source, byte for byte.

Two things differ from the earlier plan, on purpose:
- **SFBOOT is 500 MiB (128,000 blocks), not 512.** The free space is 2,896.16 MiB and the stub
  takes 2,384; 512 would leave 0.16 MiB for whatever alignment `diskutil` applies to two
  partitions whose space starts at an odd block. 500 MiB is the size the installer has always
  used for its FAT partition; about 12 MiB after it stay unallocated. The two files are 14.3 MB.
- **SFBOOT has the partition type "EFI System"**, because that is the only type for which the
  installer records the partition and writes `chainload=` (`osinstall.py`: `if ptype == "EFI"`).
  A second EFI-typed partition is what every second Asahi install on one disk has. Linux's own
  stage 1 names its EFI partition by PARTUUID and so does `/etc/fstab` (check in step 1).

**The installed variant of m1n1** (`patches/0001`, build flag `SF_INSTALLED`, both stages):
where m1n1 would open its USB proxy ("No valid payload found") or restart the machine (a
panic, an exception), it turns the screen log on, prints why, and stops:

    SiliconForest: stopped (<reason>).
      Nothing on the disk was changed by this.
      Hold the power button until the machine is off. A short press then starts
      the default system; holding the button at start shows the startup picker.

No USB device appears, nothing restarts by itself, nothing is written. (The log above that
message is m1n1's own and includes its copyright line, "The Asahi Linux Contributors"; it is
visible only in this failure case. A normal start shows the mark on black and nothing else.)

## Two files, and why

Stage 1 copies the image it chainloads to its own base. m1n1 (`src/chainload.c`,
`chainload_image`) refuses an image whose end, `ALIGN_UP(file + the variable lines stage 1
passes on + 4, 16 KiB)` from that base, lies above the `top_of_kernel_data` stage 1 was started
with, once its heap (which begins there) is in use; and it always is, because the file was read
into it: `chainload: image span overlaps the live heap`. iBoot starts our stage 1 as a raw
object of 4,325,376 bytes plus its variable lines with that top shortly behind it. On the
installed machine stage 1 ran at `0x805494000` (its variable lines ended at `0x8058b4082`) and
refused the 14,423,700-byte file, so the top was below `0x806258000`; where exactly is not
known. **The only room that is certain is stage 1's own image: 4,325,376 bytes.**

- `sf/boot.bin` = stage 2 alone, 4,128,768 bytes: span 4,145,152 with 512 bytes allowed for
  the lines (stage 1 passes two, 169 bytes), 180,224 bytes to spare. `build.sh` fails when
  span + 32 KiB exceeds stage 1's size, and `linux/sf-bootbin-install.sh` refuses such a file.
  Stage 2 is the stage of `../b1` built with m1n1's FAT reader (`CHAINLOADING=1`) and
  `SF_KERNEL_FILE=1` (`patches/stage2/0001`, `src/pdsf_file.c`), with one EL3 stack instead of
  four (`patches/stage2/0002`; T8103 has no EL3), which is what keeps it below stage 1's size.
- `sf/kernel.bin` = the records (configuration, kernel collection, end) that used to follow
  the image. Stage 2 finds no records behind itself, takes the partition from the line stage 1
  passes on (`chosen.asahi,efi-system-partition=<PARTUUID>`; stage 1 does not pass
  `chainload=`, and stage 2 refuses `chainload=`, `boot=` and any other payload), starts the
  storage controller, reads the file into its heap with m1n1's read-only NVMe and FAT code,
  stops the controller, and handles the records exactly as appended ones: same checksums, same
  root rule, `sf_stop` on any failure.
- Stage 1 in the stub is unchanged (`build.sh` stops if it would build another one,
  `STAGE1_INSTALLED`): a new stage 1 needs recovery mode again.
- Records appended to the image still work (`boot-bench.bin`, the bench's start from RAM).

## What an unattended start does when something fails

| Failure | What happens | What the owner sees | Way out |
|---|---|---|---|
| stage 1 finds no partition with that PARTUUID, no `sf/boot.bin`, cannot read it, or the file is too large for it (`image span overlaps the live heap`: 13 November, the first kit) | `sf_stop` | the m1n1 log and the message above | hold power; start again (default system), or the picker |
| stage 2 refuses (`PDSF: …`: no `sf/kernel.bin` or it cannot be read, a checksum, a root other than the one it was built for, no room, no seed from the SEP) | `sf_stop` | the reason line, then the message | the same; then repair the files from Linux (`linux/sf-bootbin-install.sh`, `--rollback`) |
| m1n1 itself panics | `sf_stop` instead of its restart | the message | the same |
| the kernel starts and finds no root (partition table changed, filesystem gone, the driver's fence refuses) | the kernel waits for the root. Seen once on the bench for four minutes (boot dr03): no restart, nothing written. `IOSetRecoveryBoot` is a no-op in this kernel (`kernel-patches/0001`), and there is no NVRAM driver: boot policy and NVRAM cannot be changed from here. **Not observed beyond four minutes** | the mark, nothing else | hold power |
| the kernel panics | it spins (boot nb10: "machine will reset or spin", it spun); the report goes to the serial port only | a frozen picture | hold power. The root was mounted read-write: its journal is replayed by SiliconForest at its next start (boots dr13 to dr16); Linux never mounts it |
| the desktop never appears, or hangs | nothing by itself | a black or frozen screen | hold power; as above |

None of these restarts the machine, so none can loop. After a forced power-off the PMU holds
one boot error until Linux boots and clears it (SiliconForest only reads it). **Keep Linux the
default system (step 8)**: then every restart and every power cycle after a failure lands in
Linux, the count never passes 1, and the firmware's own reaction to repeated failed starts
(not known to this lab) is never reached.

## What the installer does outside the two new partitions (read in its source)

- `diskutil addPartition` twice: the partition table, both copies, gains two entries. macOS
  may reorder the table's entries by disk position (Linux's `nvme0n1p4…` would become `p6…`; the
  lab finds partitions by identifier).
- `bless --setBoot` on the new volume (`main.py` `bless()`): **the default startup volume
  becomes SiliconForest.** This is not optional: recovery's step only works from the recoveryOS
  paired with the default volume. It is put back in step 8.
- In recovery (`step2.sh`): `bputil -nc -v <volume group>` and `kmutil configure-boot` create
  the boot policy of **this** volume group in the system's `iSCPreboot`. Other systems'
  policies are separate files and are not touched.
- It reads (does not write) macOS's Preboot volume for the list of users who may authorise
  (`AdminUserRecoveryInfo.plist`), and mounts the Preboot/Recovery/System volumes of every APFS
  container to list the installed systems. If macOS's own list of admin users is stale it runs
  `diskutil apfs updatePreboot` on **macOS's** volume (`check_cur_os`); that is Apple's
  maintenance command and the only write to an existing system I found.
- Not done with our data: no firmware collection, no `asahi/` directory, no report to anyone
  (`REPORT` is unset by `sf-install.sh`), no resize, nothing on Linux's stub, EFI partition,
  `/boot` or root, nothing on our root partition.
- Downloads: (a) by `sf-install.sh`, once, the Asahi installer package from
  `cdn.asahilinux.org/installer` (their bootstrap's address), used **only for its bundled
  Python**; (b) by the installer, single files out of Apple's macOS 13.5 IPSW on
  `updates.cdn-apple.com` (the version it picks for this machine's firmware, as for the Linux
  entry): manifests, the boot firmware named in the manifest and the recovery image
  (`arm64eBaseSystem.dmg`). Sizes not measured here: tens of MB for (a), several hundred MB up
  to about 1 GB for (b). Plain HTTPS requests; no account, no identifier of the machine or the owner.

## Steps

Numbers used below: free space = blocks 208,669,702 to 209,411,118 (741,417 blocks of 4096
bytes) directly behind the SiliconForest partition `p3` (192,633,862 to 208,669,701).
"Coordinator" steps are run from the workstation; the others at the machine.

### 1. Before (coordinator, the M1 in Linux; read-only)

    cd integrations/siliconforest/m1-native-boot/install
    ./build.sh && python3 -I -B test_kit.py
    . ../../../../private/disk-root/sf-part.env
    M1="ssh -o HostKeyAlias=10.1.1.127 chrisk@10.1.1.211"
    $M1 'sudo python3 - read /dev/nvme0n1 /tmp/sf-gpt-before' < linux/sf-gpt-check.py
    $M1 'sudo tar -C /tmp -cf - sf-gpt-before.head.bin sf-gpt-before.tail.bin' | tar -C ../../../../private/disk-root/gpt-backup -xf -
    python3 linux/sf-gpt-check.py check ../../../../private/disk-root/gpt-backup/sf-gpt-before \
        --partuuid $SF_PARTUUID --first $SF_PART_LBA --blocks $SF_PART_BLOCKS
    $M1 'grep -n "nvme0n1p\|/dev/disk/by-partlabel" /etc/fstab /etc/m1n1.conf /boot/loader/entries/* /etc/default/grub 2>/dev/null; cat /etc/fstab'

Expect `GPT ALL OK`, and no line of the Linux install that names a partition by number.
A current backup of macOS and of the Linux home exists (the owner's word).
Back out: nothing was changed.

### 2. macOS (owner starts it; coordinator copies the kit)

Owner: shut Linux down, hold the power button until "Loading startup options", choose
macOS, log in **at the screen** (the installer refuses when nobody is logged in there).
Coordinator, over ssh:

    scp out/siliconforest-mac-kit.tar <mac>:   &&   ssh <mac> 'tar -xf siliconforest-mac-kit.tar'

Back out: delete the directory `mac-kit`.

### 3. Look before writing (coordinator over ssh, or the owner; read-only)

    cd mac-kit && ./sf-install.sh --python-only        # fetches the Python bundle; prints "python ok"
    ./sf-preflight.sh 208669702 741417

Expect: `PRE ok   the kit's files are what was built`; the partition list with
`<- OURS` on a free space of 3,036,844,032 bytes after the SiliconForest partition
(macOS shows that partition as a Linux Filesystem with no name); `PRE ok` for "is there
exactly" and "large enough"; and either "the only free space the installer will offer" or
a NOTE naming the one to choose. **Any `PRE FAIL`: stop here.**
Back out: nothing was changed (the Python bundle is a directory inside `mac-kit`).

### 4. The installer (owner, in Terminal on the machine; this step writes)

    cd mac-kit && ./sf-install.sh

| It shows | Answer |
|---|---|
| sudo's password prompt | the macOS password |
| "Welcome to the SiliconForest installer" … "Press enter to continue." | Enter |
| the list of partitions and systems, then "Choose what to do:" with `f: Install an OS into free space` and `q: Quit` | `f` (if `f` is not offered: `q`, and stop) |
| "Choose an OS to install:" `1: SiliconForest` | `1` |
| only if more than one free area is large enough: "Choose a free area to install into:" | the one of 3.04 GB that step 3 named |
| "Enter a name for your OS" `OS name (SiliconForest):` | Enter |
| "Using macOS 13.5 for OS firmware", then downloads and "Preparing target volumes", "Adding partition SFBOOT (524.29 MB)", "Formatting as FAT", "Copying from esp into … partition" | wait |
| "Password for <user>:" then "Setting the new OS as the default boot volume..." | the macOS password |
| "Installation successful!" with `APFS VGID` and **`EFI PARTUUID`** | note the PARTUUID (it is SFBOOT's; put it into `private/disk-root/`, never into git) |
| instructions for the next step, "Press enter to shut down the system." | read step 5 first, then Enter |

After this: the two partitions exist, `sf/boot.bin` and `sf/kernel.bin` are on SFBOOT, the stub has its recovery
and our stage 1 waiting inside `Finish Installation.app`; **the default startup volume is
SiliconForest, which cannot start yet.**
Back out: if it stops before "Installation successful": run it again; with an incomplete
stub it offers `p: Repair an incomplete installation`; or remove everything ("Uninstall").
If `diskutil` refused to add a partition, nothing was changed.

### 5. Check from Linux before recovery (coordinator; read-only, plus one bench boot)

Owner: after the shutdown, **hold** the power button, choose the Linux entry (plain click:
once). Coordinator:

    $M1 'sudo python3 - read /dev/nvme0n1 /tmp/sf-gpt-after' < linux/sf-gpt-check.py
    $M1 'sudo tar -C /tmp -cf - sf-gpt-after.head.bin sf-gpt-after.tail.bin' | tar -C ../../../../private/disk-root/gpt-backup -xf -
    python3 linux/sf-gpt-check.py check ../../../../private/disk-root/gpt-backup/sf-gpt-after \
        --partuuid $SF_PARTUUID --first $SF_PART_LBA --blocks $SF_PART_BLOCKS \
        --free-first 208669702 --free-last 209411118 --before ../../../../private/disk-root/gpt-backup/sf-gpt-before --new 2
    $M1 'sudo bash ./sf-bootbin-install.sh <EFI PARTUUID> --show' # read-only mount: sf/boot.bin, sf/kernel.bin and their sha256 (= out/)
    $M1 'findmnt -rn | grep nvme; systemctl --failed --no-legend'  # Linux mounted what it always mounts

Expect `GPT ALL OK`: both copies valid and identical, our entry unchanged, every earlier
entry still there byte for byte, exactly two new entries inside the free space (APFS,
610,304 blocks; EFI-typed, about 128,000 blocks). The line "N earlier entries changed their
position" may be non-zero; Linux mounts by UUID. Then a new range list (the successor of
`runs/m1-storage/linux-d/ranges`: `linux-e/ranges`, the same plus three ranges of SFBOOT; the
free-space ranges now lie in the two new partitions, which Linux never mounts; put
`SF_BOOT_PARTUUID`, `SF_BOOT_LBA`, `SF_BOOT_BLOCKS` into `private/disk-root/sf-part.env`) and
**one boot of the installed chain from the bench** (the last command below): `NV_PART verified … found in both GPT copies` is the driver's own answer to "does it
still find p3".
If the check fails: do not go on to step 6. Nothing of SiliconForest starts from the disk
(the driver refuses by itself); the saved tables are the reference; what to restore is decided then.

### 6. Recovery (owner, at the machine; this step sets the boot policy)

Shut down. Wait 25 seconds. Press **and hold** the power button until "Loading startup
options". Choose **SiliconForest**. A "macOS Recovery" dialog may ask for a user and password
(if it asks to "select a volume to recover": the normal macOS volume). A Terminal opens:

| It shows | Answer |
|---|---|
| "SiliconForest installer (second step)", VGID, System volume; the explanation; "Press enter to continue." | Enter |
| `bputil`: a warning about lowering this volume's security, user name and password, a confirmation | the macOS user, password, `y` |
| `kmutil`: the same | the same |
| "Installation complete! Press enter to reboot." | Enter |

If it says "did not boot into the correct recoveryOS" or "not in One True RecoveryOS mode":
follow its text (it shuts down; hold the button in one continuous press, choose SiliconForest again).
Nothing is half-applied by a wrong password: it asks again.
Back out after this step: "Uninstall".

### 7. The first start (owner watches)

The restart starts the default volume, which is now SiliconForest: **the mark on black**
(stage 1, then stage 2, about 4 s), it stays while the kernel starts, then about 10 s later
the desktop in the look chosen earlier, with two terminals; Wi-Fi about 15 s after that.
If instead the log and "SiliconForest: stopped" appear, or the mark stays for a minute:
hold the power button until the machine is off; go to step 8 (the picker); see the table above.

To leave: menu **Session > Restart** (about 10 s), and **hold the power button** as the
screen goes dark, for the picker. (A plain restart would start SiliconForest again until step 8.)

### 8. Linux as the default again (owner)

In the picker: select the Linux entry, **hold Option** and click "Always Use". Check: restart
Linux once without touching anything; it comes back to Linux. From then on: SiliconForest =
hold the power button at start and choose it; every restart of SiliconForest lands in Linux.

### 9. Afterwards (coordinator)

The checks of step 5 again (table, mounts), `journalctl -k -b | grep "macsmc-reboot: PMU"`
(no boot-error line after a clean SiliconForest restart), `sudo e2fsck -fn` of `p3` through
`m1-storage/tools/dr_linux_root.sh wcheck` (clean, journal start 0).

The userland comes over SSH once SiliconForest runs (`m1-storage/tools/sf_userland_push.sh push gtar|sys|brew|claude`,
then `push gpu`: the GL Mesa and Vulkan of `m1-vulkan/system.nix` at the system paths, previous files kept as
`.old-<tag>`; relaunch the compositor afterwards). The kit's command line carries `sfavd=1` (hardware video
decode, `/dev/sfavd`; its firmware `avd-fw-v2-t0.bin`, MIT, goes to `/Library/Firmware/apple/` from the M1's
Linux, `m1-avd/tools/avd_disk_test.sh push`) and `if_attach_nx=46` (Wi-Fi receive statistics; SF-AC).

## Updating the kernel later (no recovery mode, no macOS)

    ./build.sh && python3 -I -B test_kit.py                 # workstation: out/boot.bin, out/kernel.bin
    scp out/boot.bin out/kernel.bin linux/sf-bootbin-install.sh chrisk@10.1.1.211:
    ssh … 'sudo bash ./sf-bootbin-install.sh <PARTUUID of SFBOOT> boot.bin kernel.bin'

The script refuses any partition that is not FAT, labelled SFBOOT, EFI-typed, at most
600 MiB, unmounted, absent from `/etc/fstab` and not the EFI partition Linux booted from. It
refuses a `boot.bin` that is not the installed stage 2 alone, built for the root on this disk,
or that would not fit in stage 1's footprint; and a `kernel.bin` that is not configuration +
kernel with good checksums for that root, or whose session ends by itself (the bench's
`kernel-bench.bin`, accepted only with `SF_BENCH=1` and never kept as a previous file). A file
that is already there byte for byte is left alone. It keeps each replaced file as `sf/<name>.old`;
`--rollback` validates the resulting boot/kernel pair on a read-only mount before swapping
back every file that has a previous one; `--show` lists what is there. Stage 1 never
changes. **Prove new files on the bench first** (below): from RAM with `DR_BOOTBIN=`
(`boot-bench.bin`), or the installed chain itself with `SF_BENCH=1` + `DR_STAGE1_GO=`.
`sf/boot.bin.old` on the installed machine is the first kit's 14.4 MB file (confirmed again
by SF-AF's tc01 cycle): stage 1 refuses it. The updated script refuses that rollback before
renaming anything. **Restore a verified kit through the normal two-file install command**
instead: its current-size `boot.bin` is left alone, and its known-good `kernel.bin` is
installed with the usual copy/compare/previous-file handling. Older copies of the script
did not validate rollback and would restore the unbootable first image.

Host regression: `python3 test_rollback.py -v` (scratch files and recorded mount calls;
no real device access). The old script fails four refusal cases, the updated script passes
all seven, including valid-pair and kernel-only rollback. This does not establish a real
hardware rollback; tc01 booted successfully and did not need one.

## Uninstall

1. Make another system the default first (step 8).
2. In macOS: `./sf-uninstall.sh` prints what it found and the commands;
   `sudo ./sf-uninstall.sh --execute` runs them after the container's name is typed:
   `diskutil apfs deleteContainer <the SiliconForest container>` and
   `diskutil eraseVolume free free <SFBOOT>`. If the first leaves a partition where the
   container was, the same `eraseVolume free free` on it.
3. From Linux: `sf-gpt-check.py check … --before <the read of step 1> --new 0` must say
   `GPT ALL OK`: the table is what it was before step 4 (entry positions aside).

The space is unallocated again; it is not given back to `p3` (that would move the write
fence a third time). The entry's boot policy stays behind, unreferenced, in `iSCPreboot`
(the Asahi project documents removal as deleting the partitions and nothing more).
**Not rehearsed:** the commands are from `diskutil`'s manual and the Asahi project's
documentation, and the script's way of finding the container has never met a real disk.

## What is proven, and what is first proven at install

`docs/roadmap/issues/SF-R-storage.md`, "Step 6", has the evidence lines. In short:

- **The installed chain, proven on the bench with the real boot partition** (13 November,
  after the install). dr35: the boot object that is in the stub (stage 1 + the two variable
  lines with SFBOOT's PARTUUID), uploaded and entered as iBoot enters it (top of memory
  directly behind it), with the first kit's 14.4 MB `sf/boot.bin` on SFBOOT: `Partition offset:
  209280006`, `chainload: image span overlaps the live heap`, `SiliconForest: stopped` — the
  owner's failure, line for line. dr36: the same stage 1, the new `sf/boot.bin` and
  `sf/kernel.bin` (bench session) on SFBOOT, nothing else uploaded: stage 1 read 4,128,768
  bytes and started them, stage 2 read 10,163,888 bytes itself, checksums ok, kernel entered,
  desktop from the disk root, Wi-Fi, clean self-restart, no controller reset, Linux finds the
  root clean and the 30 ranges it never writes (SFBOOT's three among them) unchanged.
- **Proven on the M1 from RAM earlier.** dr31: stage 2 with the records behind it, uploaded
  and entered (the first kit's file; the appended path is kept, `boot-bench.bin`). dr32:
  stage 1 with a PARTUUID nobody has stops with `PartitionNotFound`. dr34: stage 1 given 64 MiB
  of room chainloaded Linux's `m1n1/boot.bin` (a layout the installed machine does not have:
  `DR_STAGE1_ROOM=1`).
- **Proven on the workstation.** `test_kit.py` (11 tests): the installer's own code with our
  data asks for exactly one EFI-typed 500 MiB partition after the stub, puts `sf/boot.bin` and
  `sf/kernel.bin` on it and builds the boot object from our stage 1 and the two variable lines;
  `boot.bin` fits stage 1's footprint and the first kit's file does not; the update script's
  checks accept the pair and refuse seven wrong ones. `sf-gpt-check.py` against the real table
  copies (before and after the install) and altered ones.
- **Proven by the install itself.** iBoot starts our stage 1 from the picker entry (logo, then
  its log); the installer's run, `diskutil`'s placement (`GPT ALL OK`), `sf-bootbin-install.sh`
  on the real SFBOOT (install, refusals, `--show`).
- **Not proven yet.** The real start from the picker with the new files (iBoot's own
  `top_of_kernel_data`, which the bench can only imitate; the build's fit check does not depend
  on it); `sf/kernel.bin` as it is on SFBOOT now (dr36 booted `kernel-bench.bin`, which differs
  in the configuration record only: two words that end the session); a start without the
  bench's cable; a session longer than 150 s; `--rollback`; uninstall.

Bench commands (the M1 in Linux, `private/disk-root/sf-part.env` present):

    BENCH_WHO=disk DR_RW=1 DR_HALT=reboot DR_STAGE=disk DR_PLAIN=1 DR_SECONDS=150 DR_BOOTBIN=$PWD/integrations/siliconforest/m1-native-boot/install/out/boot-bench.bin \
        tools/bench.sh run integrations/siliconforest/m1-storage/tools/dr_desk_slot.sh drNN native 300
    BENCH_WHO=disk DR_RW=1 DR_HALT=reboot DR_STAGE=disk DR_STAGE1=$PWD/integrations/siliconforest/m1-native-boot/install/out/sf-stage1-test.bin \
        tools/bench.sh run integrations/siliconforest/m1-storage/tools/dr_desk_slot.sh drNN native 180

The installed chain (stage 1 with SFBOOT's PARTUUID is `out/sf-stage1-installed.bin`, built when
`SF_BOOT_PARTUUID` is in `sf-part.env`). First put the bench session's kernel file on SFBOOT,
afterwards the real one again (the bench one is not kept as `.old`):

    O=$PWD/integrations/siliconforest/m1-native-boot/install/out
    scp $O/boot.bin $O/kernel.bin $O/kernel-bench.bin …/linux/sf-bootbin-install.sh chrisk@10.1.1.211:
    ssh … 'SF_BENCH=1 sudo --preserve-env=SF_BENCH bash ./sf-bootbin-install.sh <PARTUUID of SFBOOT> boot.bin kernel-bench.bin'
    BENCH_WHO=disk DR_RW=1 DR_HALT=reboot DR_STAGE=disk DR_STAGE1=$O/sf-stage1-installed.bin DR_STAGE1_GO=$O/kernel-bench.bin \
        tools/bench.sh run integrations/siliconforest/m1-storage/tools/dr_desk_slot.sh drNN native 330
    ssh … 'sudo bash ./sf-bootbin-install.sh <PARTUUID of SFBOOT> boot.bin kernel.bin'

The bench's return to Linux relies on Linux being the default startup volume
(`sudo asahi-bless --list-volumes`: the star): check it before any bench boot.
