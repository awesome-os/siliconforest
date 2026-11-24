# Making room for SiliconForest: the owner's steps in macOS

> **Later change (12 November 2026):** this page records how the partition was created, at 64 GiB (16,777,216 blocks). The owner has since shrunk it from its end to 16,166,912 blocks (last block 208,800,773) to make room for SiliconForest's startup-picker volume; blocks 208,800,774 to 209,411,118 are unallocated. The numbers below are those of the day it was made. Present state: `SF-R-storage.md`.

Two commands in macOS Terminal: shrink the macOS container by 64 GiB, then create one empty partition in the freed space. This is what the Asahi Linux installer does on these machines; the commands and checks below are taken from its source (`asahi-installer` commit `e93743d`, `src/main.py` `action_resize`, `src/diskutil.py`, `src/osinstall.py`), not from memory. Where something is **not** from that source it says so.

Nothing here touches Linux, the EFI partition, boot policy or NVRAM. Formatting the new partition is **not** part of this: that is done later from Linux, after a check.

## What will change
Sizes in 4096-byte blocks, as Linux reported them on 2026-11-11 (read-only):

| | Now | After |
|---|---|---|
| macOS container (`disk0s2`) | starts at block 128,006, 209,283,113 blocks = 857,223,630,848 bytes | same start, 192,505,856 blocks = **788,503,986,176 bytes** |
| New partition | | starts at block 192,633,862, 16,777,216 blocks = **68,719,476,736 bytes (64 GiB)** |
| Left over | | 41 blocks (168 KiB) of free space before the next partition |
| Everything else (iBoot container, Linux stub, EFI, `/boot`, Linux root, recovery) | | start and size unchanged |

**Partition numbers will shift.** macOS keeps the partition table sorted by position (that is why the recovery partition is last on this disk although it is the oldest). The new partition lies between macOS and the Linux stub, so in Linux it becomes `nvme0n1p3` and today's `p3`…`p7` become `p4`…`p8`; in macOS likewise `disk0s3`. Linux finds its root by filesystem UUID (seen in its kernel command line), and the boot loader finds the EFI partition by its identity, not its number, so this should not matter. **Checked read-only on 2026-11-11:** `/etc/fstab` mounts everything by UUID, the kernel command line uses `root=UUID=…`, there is no swap, and no file under `/etc` or `/boot` (including the EFI partition and the boot loader's configuration) contains a name of the form `nvme0n1pN`. Not checked: how the first-stage loader inside the Linux stub volume locates the EFI partition (it is not readable from Linux); the Asahi design is to store that partition's identifier, not its number.

Also to know: macOS itself has written to the EFI partition before (it holds Spotlight and Trash folders), and may again while it runs. That is macOS' own behaviour, not part of these steps.

## Before you start
1. **Backup.** Anything in macOS you cannot lose should be on another disk. A resize that fails normally leaves everything as it was, but this is the one step in the whole plan that rewrites structures of the macOS volume.
2. Finish any pending macOS update (the installer warns that a pending upgrade blocks shrinking).
3. Mains power connected.

## Boot into macOS
The machine normally starts Linux. Shut it down completely. Press **and keep holding** the power button until "Loading startup options" appears, then release. Choose **Macintosh HD** and click Continue. Do **not** hold Option while choosing and do not use "Always Use": then the default stays Linux. (The default only changes through `bless`/Startup Disk; the installer source sets it explicitly with `bless --setBoot`, nothing else does.)

Log in as an administrator and open Terminal.

## Step 1: look, change nothing
```
diskutil list disk0
```
Expected: a line `Apple_APFS Container disk… 857.2 GB disk0s2`, and after it five more partitions ending with `Apple_APFS_Recovery … disk0s7`. **If `disk0s2` is not the 857.2 GB macOS container, stop.**

```
diskutil apfs resizeContainer disk0s2 limits
```
This only reports. The installer requires two things, and so do we:
- "Minimum size (preferred)" must be **below 788.5 GB**;
- the container's free space must be at least **107 GB** (the 64 GiB we take plus the 38 GB the installer insists on leaving macOS for upgrades, plus its 0.5 GB margin). `diskutil apfs list` shows the container's free space.

If the minimum size is much larger than the used space, the cause is usually Time Machine local snapshots (the installer says so and points to https://alx.sh/tmcleanup). Not from the installer source, standard macOS commands described on that page: `tmutil listlocalsnapshots /` lists them and `tmutil deletelocalsnapshots <date>` removes one. Then look at the limits again. If the numbers still do not fit, stop and report them; nothing has been changed.

## Step 2: shrink the macOS container
```
diskutil apfs resizeContainer disk0s2 788503986176
```
The number is the new size in bytes (old size minus 64 GiB, rounded down to a whole MiB, as the installer aligns it). The system may appear frozen for a while; the installer says this is normal: wait. It can take several minutes.

Expected: it runs through its checks and the resize and ends without an error (the exact wording of diskutil's last line is not in the installer source). If it reports an error, **stop here** and send the output: the installer's advice for this case is "usually caused by pre-existing APFS filesystem corruption … can usually be solved by doing a First Aid repair from Disk Utility in Recovery Mode".

## Step 3: create the empty partition
```
diskutil addPartition disk0s2 %Linux% %noformat% 68719476736
```
This is the installer's exact form for a Linux partition: "after `disk0s2`", type "Linux Filesystem", no filesystem made, size in bytes. (A type of our own would have been nicer; the installer only ever passes `%EFI%` and `%Linux%`, so nothing else is verified. SiliconForest will recognise its partition by the partition's unique identifier, start and size instead.)

Expected: no error, and
```
diskutil list disk0
```
now shows `disk0s2` at about 788.5 GB followed by a new `Linux Filesystem … 68.7 GB disk0s3`, then the former `s3`…`s7` as `s4`…`s8`, recovery still last.

If `addPartition` fails ("not enough space" or similar), nothing is damaged: 64 GiB of the disk are simply unallocated. Send the output; the partition can then be added with a slightly smaller size.

## Back to Linux
Apple menu → Restart (or shut down and press the power button normally, without holding). Linux starts as before because the default boot volume was never changed.

If Linux does **not** start (it should): hold the power button again and pick the Linux entry in the startup options once; report it. The likely cause would be a place in Linux that names a partition by number, which the check above is meant to exclude beforehand.

## What could go wrong, and how it would show
| What | How it shows | State afterwards |
|---|---|---|
| Snapshots or a pending update block the shrink | step 1's minimum size is too large, or step 2 refuses at once | unchanged |
| Existing APFS damage | step 2 stops with an APFS error | the installer treats this as recoverable (First Aid, then try again); send the output before doing anything else |
| Power loss or forced restart during step 2 | — | APFS resizing is journaled by macOS, but this is the case the backup is for |
| Partition numbers shift | Linux partitions are `p4`…`p8` afterwards | expected; harmless if nothing names them by number |
| Typing the wrong identifier | `diskutil` acts on another container | step 1 exists to prevent this: `disk0s2` must be the 857.2 GB container |

## Afterwards (storage stream, from Linux, read-only)
- the new partition entry where expected (start block 192,633,862, 16,777,216 blocks, type Linux filesystem), read from both copies of the GPT, which must agree and pass their checksums;
- every other partition's start and size as before, except the macOS container's size;
- Linux' kernel log clean, its filesystems mounted as usual;
- the reference hashes of the ranges on the iBoot container, the Linux stub, the recovery partition and the GPT's protective MBR unchanged. The GPT header/table blocks themselves, and blocks inside the macOS container, **are expected to change** (the table was rewritten, and APFS moves its own data when it shrinks; the ranges that lay in the part of the container that was given up now belong to the new partition).
- only then, and only after a separate go: `mkfs.ext4` on the new partition, named by its unique identifier and cross-checked by start and size.
