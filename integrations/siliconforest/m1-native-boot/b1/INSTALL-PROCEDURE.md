# Installing SiliconForest as its own entry in Apple's startup picker (route B3)

> **NOT AUTHORISED. NOT EXECUTED. NOTHING HERE HAS BEEN RUN.** This is a procedure on paper for
> the owner's decision (his instruction of 12 November 2026: SiliconForest gets its own entry in
> the startup picker, its own stub volume and its own first-stage m1n1 with our logo; the
> recovery-mode steps are done by him; the Linux/Omarchy install stays untouched). No script in
> this repository performs any step of it, and the scripts it names in section 4 are not written.
> It replaces the earlier procedure for route B1 (a selector in the shared `m1n1/boot.bin` on
> Linux's EFI partition), which is dropped: nothing is written to Linux's EFI partition, to
> Linux's stub, or to any file of the Linux install.
>
> **Decided since (12 and 13 November 2026):** the room for the stub volume AND for a boot
> partition comes from the END of the SiliconForest partition, and the design is **two stages**
> (the owner will not go to recovery for a kernel update). That part is done (section 2): the
> partition was shrunk from Linux by the coordinator, twice, and blocks 208,669,702 to
> 209,411,118 are unallocated. The questions "which source" and "where the kernel lives" (the
> former D2) are closed and are no longer among the decisions of section 11. Everything from
> section 5 step B on is still not authorised and not executed.

> **Since 13 November 2026 the kit exists: `../install/INSTALL.md` has the scripts and the owner's numbered
> steps and replaces sections 4 to 8 below** (the installer itself runs with our data instead of a script of our
> own; the boot partition is 500 MiB and EFI-typed; `sfnvme_ttl=0` had to be added to the driver). Still not executed.

The model is the Asahi installer, which made the Linux entry on this machine. Its source was
read for this (`runs/m1-storage/ref/asahi-installer/src/stub.py`, `osinstall.py`, `main.py`,
`step2/step2.sh`, `build.sh`); file and function names below refer to it. Stage and kernel
packing: `DESIGN.md`. State of the disk root: `docs/roadmap/issues/SF-R-storage.md`.

## 1. What exists afterwards

| Thing | Where | Made by | Size |
|---|---|---|---|
| APFS container "SiliconForest" (the stub): volumes System (`SiliconForest`), Data, Preboot, Recovery | a new GPT partition **in the unallocated space after `p3`** (proposed: blocks 208,669,702 to 209,280,005; section 2) | the owner, in macOS, with `diskutil` | 610,304 blocks = 2,499,805,184 bytes (the installer's `STUB_SIZE`, 2.5 GB rounded down to a MiB; the same size as Linux's stub `p4`; the paired recoveryOS image alone is about 1 GB) |
| the stub's boot object, built with our logo: **our stage-1 m1n1** carrying one configuration line (`chainload=<PARTUUID of the boot partition>;sf/boot.bin`) | inside the stub (Preboot, placed by `kmutil configure-boot`) | the owner, in the stub's recoveryOS | about 1 MB |
| the picker's icon | `.VolumeIcon.icns` on the stub's System volume | the owner (the stub script) | under 1 MB |
| the SiliconForest boot partition (SFBOOT): FAT32, one file `sf/boot.bin` = our stage-2 m1n1 (`m1n1-sf-stage.bin`) + configuration + the kernel collection (`pdsf_pack.py`), 14.4 MB today (stage 4.3 MB, the collection 10.2 MB compressed; 33.8 MB in memory), limit 60 MiB (`DESIGN.md` section 1) | a new GPT partition **in the same unallocated space, after the stub** (proposed: blocks 209,280,006 to 209,411,077; section 2) | the owner creates it in macOS; the file is copied by the owner from Linux or macOS | 131,072 blocks = 512 MiB |
| the root: the SiliconForest partition as it is (ext4, label `SiliconForest`) | `p3`, blocks 192,633,862 to 208,669,701 | unchanged in content | 16,035,840 blocks (61.17 GiB) |

The Linux install's four pieces (its stub container, its EFI partition, `/boot`, the btrfs root)
and macOS's containers are not opened for writing by anything in this procedure.

### Why two stages and a boot partition
The boot object in the stub can only be replaced from the stub's own recoveryOS
(`kmutil configure-boot`, physical presence, password). If it were the whole image (stage +
kernel), every new kernel would need a trip to recovery. As with Linux, stage 1 therefore
stays fixed and loads the changing part from a FAT partition. m1n1 reads FAT only, so that
part cannot live on the ext4 root; and it must not live on Linux's EFI partition. Hence a
small FAT partition of our own. The owner decided for it on 13 November 2026 (he will not go
to recovery for a kernel update) and gave it 512 MiB from the end of `p3` (section 2).

## 2. Where the space is (decided and done), and what goes where in it (proposed)

The owner chose the end of the SiliconForest partition, twice. The coordinator did both shrinks
from the M1's Linux, with `p3` unmounted (backups of both table copies and of the listings are
in `private/disk-root/gpt-backup/`, never in git):

| | first block | last block | blocks of 4 KiB |
|---|---|---|---|
| `p3` at first | 192,633,862 | 209,411,077 | 16,777,216 |
| `p3` after 12 November 2026 (610,304 blocks given up for the stub) | 192,633,862 | 208,800,773 | 16,166,912 |
| `p3` now, since 13 November 2026 (131,072 more for the boot partition; ext4 shrunk first, then the entry; identifier and start unchanged both times) | 192,633,862 | 208,669,701 | 16,035,840 |
| **unallocated** | **208,669,702** | **209,411,118** | **741,417** (3,036,844,032 bytes) |
| next entry, unchanged (Linux's stub container `p4`) | 209,411,119 | 210,021,422 | 610,304 |

Of the 741,417 free blocks, 741,376 came from `p3` (610,304 + 131,072); the last 41
(209,411,078 to 209,411,118) were already free between `p3` and `p4` (macOS aligned `p4` when
the installer made it).

What followed in this lab: the write fence's compiled-in length is 16,035,840
(`m1-storage/kernel-rw/sf_nvme_wring.h`, commit `92c390cb0`; host tests against the new range),
the three read-write collections are rebuilt, and the state of the proof boot on this layout
is in `SF-R-storage.md` ("The second shrink"). A collection built before that commit must not
boot the disk root. **The fence does not move again**: nothing below may need another block of `p3`.

**What goes into the free space (proposal; `diskutil` decides the real placement).** Two new
entries, both made by the owner in macOS, 741,376 blocks together, which is what was freed:

| | proposed first block | proposed last block | blocks | bytes |
|---|---|---|---|---|
| the stub: one APFS container "SiliconForest" | 208,669,702 | 209,280,005 | 610,304 | 2,499,805,184 (the installer's `STUB_SIZE`) |
| SFBOOT: FAT32 | 209,280,006 | 209,411,077 | 131,072 | 536,870,912 (512 MiB) |
| left free, as today | 209,411,078 | 209,411,118 | 41 | |

Order proposed: **the stub first, with its exact size; SFBOOT second, taking what is left.**
Reasons: the stub's size is the one that matters (it is the installer's, the same as Linux's
stub `p4`; a smaller stub that still pairs with its recoveryOS is not known here), while SFBOOT
holds one file of 14.4 MB and does not care about a few blocks. The free space has 41 blocks to
spare in all, and it starts at a block that is a multiple of neither 256 nor 2,048 (block
208,669,702 = 256 x 815,116 + 6): if `diskutil` aligns a partition's start or rounds a size,
the second partition is the one that absorbs it, and it should be the one that can. So the
stub is asked for with `diskutil addPartition` and 2,499,805,184 bytes (as the installer's
`action_install_into_free` does: `self.dutil.addPartition(free_part.name, "apfs", name, STUB_SIZE)`),
and SFBOOT with the same command, FAT32, and the size word that means "the rest". The other
order (SFBOOT in blocks 208,669,702 to 208,800,773, the 131,072 blocks freed for it, and the
stub where the first plan had it, 208,800,774 to 209,411,077) is equally acceptable to
everything in this lab; it only leaves the alignment risk with the stub.
Not known here, to be read from the table afterwards and not assumed: where `diskutil` really
puts each partition; whether it aligns; **whether it renumbers the table's entries into disk
order** (new entries between `p3` and `p4` would then make Linux's `nvme0n1p4` and later into
`p6` and later: before step B the owner should look, in Linux, that nothing of the Linux
install names a partition by number (`/etc/fstab`, the boot loader's configuration; m1n1's own
`chainload=` line names a PARTUUID). The lab's tools identify partitions by identifier and,
where they name `nvme0n1p3`, check its identifier first and stop if it is not ours.)

What must hold afterwards, read from Linux (read-only) before anything else is done:
- both new entries lie wholly inside blocks 208,669,702 to 209,411,118 and do not overlap;
- the stub's entry has 610,304 blocks; SFBOOT's has at least 126,976 (496 MiB: the proposal
  minus the most an alignment to 1 MiB at both ends can cost, with room to spare) and at most 131,113;
- the entry of `p3` is byte for byte what it is now (type, identifier, first block
  192,633,862, last block 208,669,701), and so is every other existing entry apart from its
  position in the table, in both copies;
- both table copies are valid and identical (the fenced driver checks exactly this at every
  boot and refuses to open its fence otherwise: one disk-root boot afterwards is the test).
If any of these does not hold: stop, nothing of ours boots from the disk, the saved tables are
the reference. After the step the lab's range list needs a successor to
`runs/m1-storage/linux-d/ranges` (the free space becomes two partitions Linux never mounts:
their ranges stay in the "must not change" class), and SFBOOT's PARTUUID goes into stage 1's
configuration line and into `private/` (never into git).

## 3. What the stub needs from Apple

Taken by the stub script from the macOS IPSW that matches the installed system firmware (the
installer reads single files out of the remote zip; 13.5 or later for this chip,
`CHIP_MIN_VER`), all into the stub, none into macOS:

| Component | Goes to | Why |
|---|---|---|
| `BuildManifest.plist` reduced to this board's identity (J293AP, chip 0x8103, `RestoreBehavior` Erase, variant "macOS Customer"), `RestoreVersion.plist`, `SystemVersion.plist`, `usr/standalone/bootcaches.plist`, `PlatformSupport.plist` | Preboot `/<volume group id>/restore/…`, System `/System/Library/CoreServices` | make the volume group a bootable "macOS" in the eyes of iBoot, the picker and `bputil`/`kmutil` |
| `BootabilityBundle/Restore/Bootability` and its trust cache, `Firmware/Manifests/restore/macOS Customer/` | Preboot restore bundle | personalisation of the boot chain for this volume group |
| every manifest item except `OS`, `BaseSystem`, `SystemVolume*`, `Cryptex*`, `RestoreRamDisk`, `RestoreTrustCache` (iBoot stage 2, device tree, SEP firmware, kernelcache, coprocessor firmware …) | Preboot restore bundle | what iBoot loads before it reaches the boot object; a few hundred MB |
| `arm64eBaseSystem.dmg` | Recovery `/<volume group id>/usr/standalone/firmware/` | **the paired recoveryOS** of this entry: `kmutil configure-boot` for a volume group is only accepted from its own recoveryOS |
| `AdminUserRecoveryInfo.plist`, copied from macOS's own Preboot | Preboot `/<volume group id>/var/db/` | which macOS users may authorise the policy change in recovery |
| `.IAPhysicalMedia`, `Finish Installation.app` with `step2.sh` | System volume | make the first start of the entry open recovery with the second step on screen |
| `.VolumeIcon.icns` + the `com.apple.FinderInfo` attribute, the bootable flag (`fsctl_set_bootable`) | System volume | the icon and the name in the picker |

Not taken: the installer's firmware collection for Linux (`all_firmware.tar.gz`, `vendorfw`),
its `asahi` directory on an EFI partition, its OS packages. The Wi-Fi, Touch Bar and other
firmware SiliconForest loads is already on its root.

## 4. What has to be written before this can be executed (none of it exists)

1. `sf-stub-install` for macOS: the installer's `StubInstaller` (`prepare_volume`,
   `install_files`, `collect_installer_data` without the firmware collection) with our name,
   icon, boot object and `step2.sh`; no distribution, no EFI partition, no questions about
   resizing. To be reviewed line by line against `stub.py` and run by the owner.
2. Stage 1: the pinned m1n1 with `BUILTIN_LOGO=siliconforest` (the Makefile takes
   `data/<name>_128.bin` and `_256.bin`, made from PNGs by `data/makelogo.sh`) and the
   release configuration (no USB proxy wait), and the configuration line appended the way the
   installer's `m1n1.build()` appends `chainload=`. It reads one file from one partition named
   by PARTUUID and writes nothing.
3. The stage for installation (stage 2, the file `sf/boot.bin` on SFBOOT): `m1n1-sf-stage.bin` with our logo, in the variant that restarts the machine when
   it refuses or fails (today's falls back to the USB proxy), packed with the collection
   (`m1-storage/build-kc-native-rw-halt.nix`) and the boot arguments of section 6.
4. Proof from RAM first, as for every stage so far. Done for the stage and the kernel: the
   bench uploads the stage with its configuration and the collection and nothing else, and it
   boots the desktop from `p3` and restarts into Linux by itself (boot dr27, read-back dr26;
   `SF-R-storage.md`, "Step 4" and what follows it). Not done: stage 1, the logo,
   the variant that restarts instead of falling back to the proxy, anything loaded from a disk.

## 5. The steps, and who does them

A. **Linux: done** (section 2). Before step B, once more, read-only: the listing of the table
   (`sgdisk -p`, or the lab's `linux_m1.sh`) shows blocks 208,669,702 to 209,411,118 free and
   `p3` ending at 208,669,701; and nothing of the Linux install names a partition by number
   (section 2).
B. **macOS (owner, admin):** `diskutil addPartition` twice, in the order of section 2:
   first APFS, name "SiliconForest", 2,499,805,184 bytes, into the free space after the
   SiliconForest partition (proposed blocks 208,669,702 to 209,280,005); then FAT32, name
   "SFBOOT", the rest of that free space (proposed blocks 209,280,006 to 209,411,077). Then the
   checks of section 2 from Linux, and one disk-root boot from the bench. Only then: copy
   `sf/boot.bin` to SFBOOT and run `sf-stub-install` (section 3's files, the icon, the boot
   object, stage 1 with SFBOOT's PARTUUID in its configuration line, as `boot.bin` inside
   `Finish Installation.app`, `step2.sh`). Then
   `bless --setBoot --mount <the stub's System volume>`: **from here until step D the default
   boot entry is SiliconForest's stub.**
C. **Recovery (owner, at the machine):** shut down; hold the power button until "Loading
   startup options"; choose SiliconForest; authenticate. The stub's recoveryOS starts and shows
   the second step (`step2.sh`): it checks that this is the paired recoveryOS in 1TR mode
   (`bputil -d`), then runs, each asking for the macOS password:
   - `bputil -nc -v <volume group id>`: Permissive Security **for this entry only**; macOS's and
     Linux's policies are separate objects and are not changed;
   - `kmutil configure-boot -c boot.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v <System volume>`:
     our boot object (stage 1) becomes this entry's boot object.
   It then renames `.IAPhysicalMedia` away and restarts: our logo, then the kernel with the
   root on `p3` (stage 1 loads `sf/boot.bin` from SFBOOT, stage 2 boots the kernel from it).
D. **Linux back as the default (owner):** at the next start hold the power button, select the
   Linux entry while holding Option ("Always Use"); or in macOS `bless --setBoot` on Linux's
   stub. Check: a plain restart lands in Linux.

## 6. Boot arguments in the stage's configuration record

`rd=uuid boot-uuid=<filesystem UUID of p3> sfnvme=3 sfnvme_part=<PARTUUID> sfnvme_part_lba=192633862
sfnvme_part_blocks=16035840 sfnvme_write=1 sfnvme_readfence=1 sfnvme_ttl=0 sfcpuheld=1 cpus=8
sfthermal_delay=4 sf_quiet_wait=0 sf_rtc_wait=0` and the display/GPU words of the desktop
boots (`m1-storage/tools/dr_desk_boot.sh`). The stage refuses any command line whose root is
not exactly that filesystem UUID (`b1/src/pdsf_boot.c`, `PDSF_ROOT_UUID`). Decisions inside:
`sfnvme_ttl=0` (no time limit on the storage controller: an installed system must not stop
its disk after some minutes; the bench boots all had one), `sfpmu=2` or not (D4),
`sfpoweroff=1` or not (D5). The disk's session reads `sfsession_seconds=` (default four hours, the
compositor's bound); `sfbench=` is the bench's and is not given.

## 7. How Linux stays the default, and how SiliconForest is started

- Default: Linux, by step D. Nothing in SiliconForest changes the default: it has no NVRAM
  driver, and its restart (`reboot`, the menu's Session > Restart) is a plain SMC restart,
  which boots the default entry: **every restart of SiliconForest lands in Linux.**
- SiliconForest: hold the power button at start, choose its icon. (From Linux, the owner's
  own `asahi-bless --next <entry>` would do it without the button; that writes an NVRAM
  variable from Linux and is his tool and his choice, not part of this procedure.)
- A SiliconForest that hangs: hold the power button until off, press again: Linux.

## 8. Removal

In macOS: `diskutil apfs deleteContainer <the stub's container>` and
`diskutil eraseVolume free none <SFBOOT>`; blocks 208,669,702 to 209,411,118 are unallocated
again, as they are today. They are not given back to `p3` by this procedure: that would be a
third rewrite of the table and a third change of the write fence, and is a decision of its
own. The entry disappears from the picker with the container. Its local boot
policy is stored in the system's own `iSCPreboot` under the volume group's id and becomes
unreferenced; what cleans it up, and when, is not known to this lab (the Asahi project
documents removal as deleting the partitions and nothing more). Linux's default status is
not affected by removal as long as step D was done.

## 9. Failure modes

| Seen | What it is | Way back |
|---|---|---|
| step B's `diskutil` refuses (no free space found, container busy) | nothing changed | fix and repeat; nothing to undo |
| the stub script stops half-way | an incomplete stub container; the default entry is still Linux (bless is its last step) | delete the container (section 8) and repeat |
| after `bless`, the picker does not show SiliconForest, or selecting it gives "macOS needs to be reinstalled" / a different recovery | stub incomplete or not paired (`step2.sh` says "did not boot into the correct recoveryOS") | hold power, choose Linux; in macOS repeat the stub script; default back to Linux with step D |
| `kmutil configure-boot` or `bputil` fails | wrong password, or not in 1TR (power button released too early) | `step2.sh` repeats the question; nothing is half-applied |
| our logo, then nothing | stage 1 could not read `sf/boot.bin` (missing, damaged, over 60 MiB) | hold power off, power on: the default (Linux after step D; **before step D this entry is the default: use the picker**) |
| stage 2 prints `PDSF: …` and restarts | it refused the image or the command line (wrong root UUID, checksum) | Linux; fix the file on SFBOOT from Linux |
| kernel starts, no desktop | as on the bench | hold power off, on: Linux. The root is then not clean: the journal is replayed at the next mount (proven: dr12 to dr16) |
| after step B the table is not as section 2 demands (an existing entry changed, a new one outside 208,669,702 to 209,411,118 or of the wrong size, the copies differ) | `diskutil` did something else than expected | stop; nothing of SiliconForest boots from the disk (the fenced driver refuses by itself); the tables saved before each shrink and the listing after the second are in `private/disk-root/gpt-backup/` as the reference; what to restore, and with which tool, is decided with the owner then, not in advance |

Not covered by any of this: a mistake inside macOS's own `diskutil` is Apple's code on a
table every system on the disk depends on; a current backup of macOS and of the Linux home is
the first thing to have.

## 10. Artwork (to be supplied; none of it exists)

| File | Used for | Form |
|---|---|---|
| `logo.icns` | `.VolumeIcon.icns`: the icon in the startup picker | Apple icon file with the usual sizes up to 1024 px, transparent background |
| `siliconforest_128.png`, `siliconforest_256.png` | m1n1's built-in boot logo (stage 1 and stage 2), converted by `data/makelogo.sh` to raw BGRA `_128.bin`/`_256.bin` | square, on black: the logo is drawn on the black boot screen, centred |
| the desk picture | already there (`chonkstep-darwin/backgrounds/siliconforest.png`): the kernel's boot picture until the first frame | unchanged |

## 11. Decisions asked of the owner

- **D2** is decided (13 November 2026): two stages, SFBOOT of 512 MiB in the space freed from
  `p3`, beside the stub (section 2). What is left of it for the owner is the order and
  placement proposed there: his word on it, or the other order.
- **D3** when: this needs him at the machine for steps C and D, about half an hour, and a
  backup first.
- **D4** `sfpmu=2`: let SiliconForest write the three PMU boot-state bytes Linux writes
  (`m1-native-boot/reboot/sf_pmu_scratch.h`). Not needed while every restart lands in Linux
  (Linux clears the counters); needed before a day on which only SiliconForest is booted,
  several times in a row. Never booted so far.
- **D5** `sfpoweroff=1`: allow Shut Down to switch the machine off (SMC `MBSE` = `offw`,
  `off1`). Never booted: on the bench a machine that is off needs a person. With D4 unset
  Linux's driver says the machine may restart instead of staying off (the PMU's shutdown flag).
- **D6** the artwork of section 10.

## 12. What this procedure deliberately does not contain

- No command in a form that could be pasted and run as a whole, and no script.
- No write by SiliconForest or by this lab's tools outside `p3`: the GPT, the new partitions,
  the stub and the boot policy are made by the owner's hands with Apple's tools and one
  script he has read.
- No change to Linux's stub, EFI partition, `/boot` or root, to macOS's volumes, to NVRAM by us, or to the security policy of any other entry.
