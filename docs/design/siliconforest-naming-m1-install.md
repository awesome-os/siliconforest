# Installing the renamed system on the M1: plan (not executed)

State: 17 November 2026. Written without access to the M1. Nothing here was run. What is
inferred from the tree and not read from the machine is marked *(inferred)*.

The old names in this note are meant: it describes the installed system as it is now.

## Why this is one switch and not a file-by-file update

Kernel and user space find each other by name: IOKit class names (`IOServiceMatching("PDDRMDri")`
in Mesa's and the compositor's device glue), device nodes (`/dev/pdavd`), sysctls
(`kern.pd_avd.*`), boot arguments read back from `kern.bootargs` (`pdm1tpscroll=`), the Wayland
protocol `pd_window_position_v1` between AppKit and the compositor, launchd labels. A renamed
kernel with the installed user space, or the reverse, has no GPU desktop, no video decoder and
no window placement. So the unit of installation is **the kernel collection plus every installed
binary that carries one of those names**, switched together, and the unit of rollback is the
same pair.

## What changes

| Where | Now | Renamed | Source |
|---|---|---|---|
| SFBOOT `sf/kernel.bin` | configuration + collection "48" with `PD…` kexts, `org.puredarwin.*` bundle ids; its command line carries `pdm1kbd=1 pdm1trackpadenum=1 pdm1trackpadchild=1 pdgpudt=2 pdagx=1 pddispdt=2 pddcp=2 pdnvme=3 pdnvme_part=… pdnvme_write=1 pdnvme_readfence=1 pdcpuheld=1 pdapciebridge=1 pdavd=1 pdtextboot=0 …` | the same with `SF…`, `org.siliconforest.*`, `sfm1kbd=1 … sfavd=1` | `m1-native-boot/install/build.sh` (the command line is packed into `kernel.bin` with the collection) |
| SFBOOT `sf/boot.bin` (stage 2) | SHA-256 `81d58f1b…`; it starts the other seven processors only when the command line carries `pdcpuheld=1` and stops otherwise | **replaced, together with `kernel.bin`**: stage 2 from this tree looks for `sfcpuheld=1` (SHA-256 `fc8883a3…`, the same size). The earlier statement here, that stage 2 knows no argument name, was wrong (`b1/src/pdsf_boot.c`). The two builds differ in that word and in where the linker put the strings; the new one has never booted on the machine. `sf-bootbin-install.sh` now refuses a `boot.bin` and a `kernel.bin` of different namings | `m1-native-boot/install/build.sh`; `b1/src/pdsf_boot.c` |
| stage 1 in the stub volume | unchanged | unchanged (a new stage 1 needs recovery mode; none is needed) | |
| `/System/Library/CoreServices/SystemVersion.plist` | ProductName `PureDarwin` | `SiliconForest` | upstream `image.nix` |
| host name | `puredarwin` (`/etc/hosts`, the job `org.puredarwin.hostname`) | `siliconforest` | upstream `image.nix`. The M1's SSH host key does not change; `known_hosts` entries are by address |
| `/System/Library/LaunchDaemons`, `/Library/LaunchDaemons` | `org.puredarwin.hostname`, `.dmesg-save`, `.root-remount`, `.dbus`, `.desktop`, `.wayland-session`, `.cocoa-session`, `.sshd`, `.pd-netd` | `org.siliconforest.*` (and `.sf-netd`). The old plist files are **removed**, not left beside: two jobs would start the same daemon | upstream `image.nix`; `m1-bwfm/netd`; `desktop/system` |
| libSystem, dyld, libobjc, CoreFoundation, Foundation, AppKit, the other frameworks | symbols and strings with `pd_`/`PD` (`pd_cgl_*`, `PDSurface`, `F_PD_SAMEFILE`'s users, `DYLD_PD_MISSING_OK`) | renamed | the fork tree; our Cocoa patches |
| Mesa (GL, Vulkan), libdrm glue, `libPDSurface`, `libPDGOP` | class and library names | `libSFSurface`, `libSFGOP`, … | `m1-mesa-asahi`, `m1-vulkan`, upstream |
| Compositor, dock, terminal (`chonkstep-darwin-desktop` and `.next`, `chonk-terminal-darwin`) | display make `PureDarwin`, `PDGOP`/`PDHID` seats, `pd_window_position_v1`, reads `pdm1tp*=` | renamed | `sources/chonkstep-darwin` + `terminal-app/compositor/*.patch` |
| Daemons and tools | `pd-netd`, `pd-dhcp`, `pd-net-up`, `pd-backlight`, `pd-wifipm`, `pdplay`, `pdtone`, `pdvolume`, `pdvideo`, `pdtbtest`, `pdpowerd`, `pdaccountsd` | `sf-…`, `sf…`. Old binaries are removed after the switch is confirmed | the recipes of `tools/staged.sh` |
| Audio server and its frameworks | the **pin**'s build (upstream `e7aa00a6`), old names: `/dev/pdaudio`, `/dev/pdspeaker`, `kern.pd_sound.*`, `kern.pd_speaker.*`, Mach service `org.puredarwin.audio.coreaudiod`, `/Library/Preferences/org.puredarwin.audio.plist` | the **tip**'s build with our `/dev` backend and the new names only (`/dev/sfaudio`, `/dev/sfspeaker`, `kern.sf_sound.*`, `kern.sf_speaker.*`, `org.siliconforest.audio.coreaudiod`, `org.siliconforest.audio.plist`). This is also a change of server generation (upstream's reworked `coreaudiod`), which the M1 has not run; checked in QEMU as far as a machine without sound hardware can (SF-I, "The renamed audio server"). `coreaudiod`, CoreAudio, AudioUnit and the AudioToolbox family go in together | `m1-sound/sysaudio`, `m1-sound/build-sfplay.nix`; the session script `m1-storage/system/sf-session` (it sets `kern.sf_speaker.hold_probe=1`) |
| Firefox, VLC, the video player | media glue names `/dev/pdavd`, `AV_PIX_FMT_PDAVD` | renamed; a rebuild of Firefox | `firefox/`, `finch/`, `m1-avd` |
| Apple's Terminal and Activity Monitor | not ours, no PD name | unchanged; the libraries they load are ours and are replaced | |
| `/usr/share/doc/siliconforest/NOTICE` | absent | new | `NOTICE` |
| neofetch | patched for `PureDarwin` | patched for `SiliconForest` | `desktop/neofetch` |

State under `/private/var/db`: the names in the tree are already `sf…` or neutral
(`desktop-state/chonkstep/…`, `sffx`, `sfvk`, `sfmetal`, `sft12`, `sfavd`, `sf-bench`,
`sf-apps-stage`); none is keyed by a `pd` name *(inferred from the tree; to be confirmed on the
machine with the scan below)*. launchd keeps no database by label on this system *(inferred)*;
disabled-job overrides, if any exist under `/private/var/db/launchd.db`, are keyed by the old
labels and would have to be carried over by hand.

Exact file list: not known without the machine. It is produced, on the M1 or on a copy of its
root, by

    # every installed file that carries an old name outside attribution text
    find / -xdev -type f -size -200M 2>/dev/null | while read -r f; do
      strings -a "$f" 2>/dev/null | grep -v -i -E 'copyright|license|all rights reserved' |
        grep -q -E 'puredarwin|PureDarwin|org\.puredarwin|PD[A-Z][a-z]+[A-Z]|(^|[^a-z])pd_[a-z]' && echo "$f"; done

and the same scan after the switch must print only files that are not ours (Apple's two
applications, Firefox's bundled third-party libraries, recorded evidence under
`/private/var/db`).

## State on 17 November, evening (REBUILD; workstation only, nothing run on the M1)

Four files were already changed on the M1 by hand on 17 November (the files of before kept as
`.pre-sf`): the host name (`/usr/libexec/pd-set-hostname`, `/etc/hosts`), `SystemVersion.plist`
ProductName, neofetch accepting both names, a draft `NOTICE` in `/usr/share/doc/siliconforest/`.
The manifest accounts for them (rows with no recorded old hash, `keep` rows for the `.pre-sf` files).

**The kernel: built and compared, not installed (KERNEL49, 17 November).** Kernel 48 was the end of a chain
of recipes that each replaced one module of a previously qualified collection and kept the others
as built binaries; from `sf-rename` that chain does not evaluate. `m1-kernel48/build-kc.nix` links
the same 57 members once, each compiled from the renamed tree. Five member groups (the display
drivers, the GPU driver's RTKit port, the video decoder, the Wi-Fi pair, the Thunderbolt member)
were compiled for kernel 48 from an earlier revision of their lab directory than the tree has now;
`m1-kernel48/back-to-48/` holds those differences, and the recipe applies them. Member by member
against kernel 48's binaries (`m1-kernel48/compare-all.py`, table `m1-kernel48/member-proof.tsv`):
56 of 57 identical apart from names; `apfs` differs in one instruction, the length of a string
that contains the product name. The gate, fence, halt gate and speaker protection units: the same
source text with the rules applied, and the code section of the three bundles that hold them has
the same SHA-256 as kernel 48's. In QEMU the `virt` system starts from its ext4 root with this
recipe's ext4, PCI and USB members (`m1-kernel48/qemu-image.nix`); the collection itself is a
T8103 kernel and does not start there (the loader hands over, nothing follows). On the machine it
is unproven until installed. The from-source default collection (`rebase/build-kc.nix`) is still
not a substitute and must not be installed (no writable-root driver).

What kernel 48 is made of, member by member, with the recipe of each and every frozen reference of the chain's
recipes: `runs/rebuild/kernel48/README.md` (workstation, outside git). In short: all 57 members have their recipes
in the tree; the collection glue of the chain's base (`runs/m1-usbc/cio*-kc.nix`, the recorded build phase) is outside
it; seven recipes take OLD-NAME source snapshots as built store paths and would compile old-name source from this
branch; the chain proves itself against old-name collections by module UUID, which has no counterpart after a rename.
The work is one recipe that links the 57 members from their derivations, and an equivalence proof by source.

**Audio pre-test: done on the M1 on 17 November by the coordinator.** The stage, its dry run and the switch worked on
the real system; the hashes on the M1 matched the manifest except `/usr/bin/pcmplay`, which was absent (installed with
`--allow-mismatch`); the old files are in `/private/var/db/sf-audio-pretest/old`. The tip's server (old names) serves
`org.puredarwin.audio.coreaudiod`, opened `/dev/pdaudio` and `/dev/pdspeaker`, kept `hold_probe=1` and `volume_db=30`,
read the stored volume, and a 3 s tone played through the speakers (144000 frames, no trip). Headphones, the volume keys
and Firefox's sound were not yet heard by the owner. So the new server generation is separated from the new names.
`coreaudiod` is not a launchd job: `/usr/libexec/sf-session` starts it (after `/dev/sfaudio` appears, having set
`kern.sf_speaker.hold_probe=1` and `kern.sf_sound.volume_db=30`); the renamed `sf-session` of the main stage does the
same. The main switch restarts nothing: the session's long-lived children (compositor, dock, `coreaudiod`, `sf-netd`)
keep the old files until the machine restarts, which the kernel install needs anyway.

## State on 17 November, night (USERSPACE; workstation only, nothing run on the M1)

The runbook the coordinator executes is `runs/rebuild/stage/RUNBOOK.md` (workstation, outside git; it carries the
counts, the hashes and the test records). What changed in the plan:

- **The core files go in as one step.** libSystem and `/usr/lib/system/libdyld.dylib` name each other's entry points
  (`pd_libdyld_libkernel_init` and two more; `sf_…` after the rename): with one new and one old no program starts. The
  switch puts every core file beside its path first and renames them all by one process that starts nothing in between
  (zsh's own `mv`, else `python3` or `perl`; plain `mv` on a root that is not the running system); the restore does the
  same for the files it brings back.
- **`/sbin/sf-halt` has its own class, `kernel`.** It finds the write fence by a sysctl name; under the other kernel it
  would take the root for an unfenced one and restart without sealing the disk. No run touches it unless `--classes
  kernel` is given, and that is refused on the running system: it is replaced from Linux with the kernel. After a
  restore or a switch on a running system the scripts say which halt program fits the running kernel.
- **`tools/sf_rename_scan.sh`**: every file of a root whose bytes carry an old lookup, against the manifest (`row`, or
  `OPEN` when the manifest does not know the file). The manifest is made from the lab's records; this scan, run on the
  M1 before the switch, is what shows whether it is complete.
- **Rename gap found by building:** upstream's `gen-darwin-modulemap.sh` keeps kernel-private headers out of Swift's
  Darwin module with the pattern `pd_`, alone in a regular expression; `pd_pthread_sched.h` became
  `sf_pthread_sched.h`, the pattern stayed, the header (it defines `XNU_KERNEL_PRIVATE`) entered the module and the
  Swift runtime did not build. Corrected in the lab's recipes (`rebase/swift-darwin-module.nix`, used for the tip's and
  the pin's runtime); the forks are not regenerated for it. A rule for the tool is still to be written; the next fork
  takes it.
- **What is installed, by recipe** (each found through the store: the derivation of the file whose hash a record
  names): CGL `firefox/perf/tools/term12/cgl.nix`; the Finch providers `CoreMedia/build.nix`,
  `media/bottle/{videotoolbox,avfoundation,coreimage}.nix`, `AVFAudio/engine/realtime.nix`, `ColorSync/build.nix`,
  `Accelerate/installed-graphics.nix`, `AudioToolbox/install.nix`, `Metal/build.nix`; Firefox's families
  `finch/firefox-media/{clocked-providers,clock-family}.nix` and `firefox/sc-network.nix`; its launcher is
  `firefox/daily-launch.py`. Five of the Finch providers build byte for byte the same from the renamed tree: the
  rename does not touch them and they are not switched.
- **Firefox's two maintenance directories** get renamed twins (`maintenance-damage-path-v4-sf`,
  `maintenance-render-thread-t13-v1-sf`): the installer with the renamed builds' hashes, the previous launchers with
  the names changed, the receipts carried over with the hashes mapped. The old directories stay for the old system.

### The M1's dry run of 17 November (run by the coordinator)

Eight live files were not what the records said; each was found in the store by its hash. The system-wide CoreVideo is
`cocoa/corevideo-displaylink.nix` without 0172 (0172 is only in Firefox's private family): the renamed system file is
built by `cocoa/corevideo-displaylink-0149.nix`. The system VideoToolbox is `finch/VideoToolbox/encoder/build.nix`, not
the bottle variant; AudioToolbox is main's `finch/AudioToolbox/install.nix` of today; `AudioToolbox.pd` is the pin's
AudioToolbox, left over from the pin's audio server; libdrm is the same source built with an earlier toolchain. The
stage expects those files now and carries the renamed build of the same recipe for each. The live dyld's hash is not
explained yet (a dyld in use reads with lowered pointer-authentication instructions; `runs/rebuild/stage/pac-lower.py`
checks a copy page by page). The scan did not work with toybox's `find` (`-size` with a bare number) and was one
`grep` per file: corrected.

### The M1's scan of 17 November (run by the coordinator)

The scan showed what the manifest did not know: upstream's on-disk kext bundles (`/System/Library/Extensions`, about
50 with old names), kernel headers of the image, a second libdrm at `/lib`, and backups that earlier installs left
beside libraries (`AppKit.pre-t13` and 25 more). The stage's base is now every file that differs between the old-name
tip image and the renamed one, the kext directory included: bundles under an old name go with their directories,
the renamed ones come. Nothing on the M1 loads kernel code from that directory (the kernel is the collection on
SFBOOT); the IOKit user library looks bundles up there by identifier. Two new kinds of row carry this: `rmdir` (a
directory is taken away once the run has emptied it; the restore makes it again) and `sweep` (every regular file
whose path begins with the row's path is taken away like a `remove` row). The switch journals each directory chain it
makes, and the restore takes those away again. Staging directories under `/private/var/db` are not part of the
switch; which of them are still read is in the runbook.

## Tools

- `tools/sf_rename_stage.py SPEC.json DIR`: lays out a staged directory from built trees:
  `DIR/root/<final path>` and `DIR/manifest.tsv` (action, path, SHA-256 expected on the system or
  `absent`/`unknown`, new SHA-256, mode, class, note).
- `tools/sf_rename_switch.sh [--dry-run] [--root DIR] [--name NAME] [--classes "..."] STAGE`: checks
  every row first and changes nothing when a row is bad or a live file is neither the recorded old
  file nor the new one; then, class by class (`state`, `direct`, `launchd`, `core`), puts each new
  file beside its target, checks its hash there, keeps the old file under
  `/private/var/db/<NAME>/old/` and renames the new one over the path (a replaced file is never
  absent from its path). One journal line per change, written first.
- `tools/sf_rename_restore.sh [--dry-run] [--root DIR] [--name NAME]`: undoes a complete or an
  interrupted switch from the journal; the renamed files go to `.../new/`.
- `tools/sf_rename_scan.sh [--root DIR] [--out FILE] [STAGE]`: see above.
- `tools/test_sf_rename_switch.sh [SHELL] [--stage S --mock ROOT]`: 24 cases against a mock root (the 16 below, class
  `kernel`, a restore stopped before its last step, the one-process renames by the tool, `python3` and `perl`, bundle
  directories and swept backups). The 16:
  (dry run changes nothing; switch; second switch does nothing; restore gives the first root byte
  for byte; a switch stopped after each possible number of changes restores and resumes; a
  differing live file stops the switch; classes in two runs; a separate `--name`).

Two things about hashes on the running system. This kernel rewrites pointer-authentication
instructions in the page cache of an executed signed file, and `read(2)` returns those bytes: a
library in use does not hash to the file on the disk. The switch therefore reports `DIFF` for
such files when it runs on the live system (`--allow-mismatch` goes on; the old file is kept by a
hard link, the bytes of the disk). Run against the partition mounted from Linux, the hashes are
the disk's. And the "old" hashes of the manifest are what the lab's recipes at `main` build, or
what a hand-over records; where the M1 carries another revision the dry run says so.

## Order

1. **Audio pre-test** (separate stage `audio-pretest/`, its own journal `--name
   sf-rename-audio-pretest`): the old-name build of the tip's audio server (12 November) on the
   present system, to separate "new server generation" from "new names". It replaces
   `coreaudiod` and `CoreAudio` (the other four files of that build are byte-identical to the
   pin's); the Finch AudioToolbox family, its provider and Firefox's private family stay. Restore
   with the restore script before the main switch, or leave it: the main manifest then shows `DIFF`
   for those two files, which is expected.
2. **Kernel and stage 2, as one pair**: `boot.bin` from `m1-native-boot/install/build.sh` (stage 1
   of that build is byte for byte the installed one and stays), `kernel.bin` from
   `m1-kernel48/pack-kernel-bin.py` (the installed configuration record with its argument names
   mapped, 599 bytes as before; `build.sh`'s own argument list is not the installed one and, with
   the installed arguments added, does not fit a record). From Linux:
   `sudo ./sf-bootbin-install.sh <PARTUUID of SFBOOT> boot.bin kernel.bin`. Both present files
   become `.old`: the previous `boot.bin.old` (the first install's 14 MB image, which stage 1
   refuses) is replaced by the stage 2 that runs today.
3. **Main switch** (`main/`): dry run first, on the M1 or from Linux on the mounted partition. On
   the running system: `--classes "state direct launchd"` with the desktop stopped, then `core`
   (or `core` from Linux: every command the script starts loads the new dyld and libSystem on the
   old kernel once they are in place; that they run there is inferred, not observed).
4. Reboot into the renamed kernel; check the surfaces of the naming note, section 6.
5. After the owner confirms: remove `pdtone`, `pdvolume`, `pdplay` (already taken out by the
   manifest's `remove` rows and kept in `old/`), `AudioToolbox.output-pd`, and `/private/var/db/sf-rename/old`.

State keyed by old names (carried over by `migrate` rows; the old file stays for the rollback):
`/Library/Preferences/SystemConfiguration/org.puredarwin.wifi.known` (the remembered Wi-Fi
networks: without it no network after boot), `/Library/Preferences/org.puredarwin.audio.plist`
(copied with its device keys `PDAudio:` rewritten to `SFAudio:` when it is an XML property list;
levels survive only if the file is in the tip server's layout, otherwise the server starts from
its defaults), `org.puredarwin.loginwindow.plist` and `org.puredarwin.argon.conf` if they exist.
The launchd jobs `org.puredarwin.*` are replaced by their `org.siliconforest.*` files in the same
run (`remove` and `install` rows of class `launchd`).

Not rebuilt, so not in the stage: see the hand-over of 17 November in `runs/m1-coord/` and
issue 60 (the Firefox runtime under `/usr/local/lib/siliconforest/firefox`, the Finch media
frameworks other than AudioToolbox, Activity Monitor's libraries).

## The rollback entry

- `sf/kernel.bin.old` is collection 48 **with its own command line packed inside**: it reads its
  `pd…=` arguments from itself. Nothing has to be translated, and no boot argument is passed from
  outside (stage 1 passes two fixed lines, stage 2 refuses others). `sf-bootbin-install.sh
  <PARTUUID> --rollback` from Linux brings it back **together with `boot.bin.old`, the stage 2
  that runs today**: the two must go back as a pair, because each stage 2 starts only a command
  line of its own naming (`pdcpuheld=1` / `sfcpuheld=1`). The script checks the pair before it
  renames anything. Putting back `kernel.bin.old` alone, by hand, gives a system that stops in
  stage 2.
- Kernel 48 needs the **old user space**. After a kernel rollback the system comes up with the
  renamed user space: the text console, launchd, sshd and Wi-Fi are expected to work
  *(inferred: none of them finds the kernel by a renamed name; `sf-netd` uses BSD ioctls by
  number)*, the desktop is not. Then run the switch script in the reverse direction on the M1
  and reboot.
- If the machine does not come up far enough for that: from Linux, mount the SiliconForest
  partition read-write for this one purpose (allowed with the reason recorded in the log;
  `e2fsck -fn` first), run the reverse switch against the mounted tree, unmount, `e2fsck -fn`.
- Keep `/private/var/db/sf-rename/old/` and `kernel.bin.old` until the renamed system has run for
  as long as the owner wants; then the old set is deleted in one step, and the next kernel
  install makes the renamed kernel the `.old` one.

## Not changed by this plan

Partition table, NVRAM (default boot stays Omarchy), stage 1, the write fence (its constants
are the same numbers under the names `SF_FENCE_FIRST`/`SF_FENCE_BLOCKS`), the halt gate, the
SSH host key, the owner's desktop state files.
