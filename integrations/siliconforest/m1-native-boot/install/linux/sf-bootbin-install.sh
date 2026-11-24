#!/bin/bash
# SPDX-License-Identifier: MIT
# Put a new sf/boot.bin (stage 2) and sf/kernel.bin (configuration + kernel collection),
# ../build.sh, on the SiliconForest boot partition, from the M1's Linux. This is the whole
# update path: no recovery mode, no macOS; stage 1 in the startup volume stays as it is, reads
# the new sf/boot.bin at the next start of SiliconForest, and that reads sf/kernel.bin.
#   sudo ./sf-bootbin-install.sh <PARTUUID of the boot partition> <boot.bin> <kernel.bin>
#   sudo ./sf-bootbin-install.sh <PARTUUID of the boot partition> --rollback     (the previous files back)
#   sudo ./sf-bootbin-install.sh <PARTUUID of the boot partition> --show         (what is there; read-only mount)
#   SF_BENCH=1 ...  accepts a kernel.bin whose session ends by itself (kernel-bench.bin: the bench proof
#                   on the real partition). Such a file is never kept as a previous one.
# The PARTUUID is the one the installer printed ("EFI PARTUUID") and stage 1 carries in its
# chainload= line; it is not Linux's EFI partition, and the script refuses that one.
# It writes ONLY inside that partition's FAT filesystem: each new file under a temporary name,
# synced and compared, then renames (X -> X.old, X.new -> X). A file that is already there byte
# for byte is left alone, with its previous one. A power cut between the renames leaves .old and
# .new files: SiliconForest then stops with its message (hold the power button; Linux starts as
# before), and running this again repairs it.
# Why two files: stage 1 can only start an image that fits in its own footprint (m1n1 refuses a
# larger one: "chainload: image span overlaps the live heap"). boot.bin is therefore stage 2 alone
# and is refused here when it would not fit; --rollback to a first-install boot.bin of 14 MB gives
# a file stage 1 refuses.
set -euo pipefail
ID=${1:?PARTUUID of the boot partition}; WHAT=${2:?boot.bin, --rollback or --show}; KERNEL=${3:-}
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 2; }
ID=$(echo "$ID" | tr 'A-F' 'a-f')
DEV=$(blkid -t PARTUUID="$ID" -o device | head -1); [ -b "$DEV" ] || { echo "no partition with that PARTUUID" >&2; exit 2; }
refuse() { echo "REFUSED: $*" >&2; exit 3; }
[ "$(blkid -s TYPE -o value "$DEV")" = vfat ] || refuse "$DEV is not FAT"
case "$(blkid -s LABEL -o value "$DEV")" in SFBOOT*) ;; *) refuse "$DEV is not labelled SFBOOT";; esac
[ "$(lsblk -n -o PARTTYPE "$DEV" | tr 'A-F' 'a-f')" = c12a7328-f81f-11d2-ba4b-00a0c93ec93b ] || refuse "$DEV has another partition type than the installer gave it"
[ "$(blockdev --getsize64 "$DEV")" -le $((600 << 20)) ] || refuse "$DEV is larger than our boot partition"
! findmnt -rn -S "$DEV" > /dev/null || refuse "$DEV is mounted"
! grep -i -q "$ID\|$(blkid -s UUID -o value "$DEV")" /etc/fstab || refuse "$DEV is named in /etc/fstab: it belongs to Linux"
[ ! -d "/sys/firmware/devicetree/base/chosen" ] || ! tr -d '\0' < /sys/firmware/devicetree/base/chosen/asahi,efi-system-partition 2>/dev/null | grep -i -q "$ID" || refuse "$DEV is the EFI partition this Linux was booted from"
MNT=$(mktemp -d); trap 'umount "$MNT" 2>/dev/null || true; rmdir "$MNT"' EXIT
NAMES="boot.bin kernel.bin"
if [ "$WHAT" = --show ]; then
  mount -t vfat -o ro "$DEV" "$MNT"; ls -la "$MNT" "$MNT/sf" 2>/dev/null || true; sha256sum "$MNT"/sf/* 2>/dev/null || true; exit 0
fi
ROOT=$(blkid -L SiliconForest || true); [ -b "$ROOT" ] || refuse "no partition labelled SiliconForest on this machine"
check() { python3 -B - "$1" "$2" "$(blkid -s UUID -o value "$ROOT")" "${3:-0}" <<'PY'
import re, struct, sys, zlib
boot, kernel, root, bench = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read(), sys.argv[3], sys.argv[4] == '1'
# Stage 1 (…-siliconforest-stage1-v1, 4,325,376 bytes) refuses an image whose span,
# ALIGN_UP(file + the variable lines it passes on + 4, 16 KiB), reaches past its own footprint
# (m1n1 src/chainload.c; ../build.sh has the reasoning). 512 bytes of lines, 32 KiB of margin.
STAGE1 = 4325376
assert boot[0x808:0x810] == b'AURALOAD', 'boot.bin: no m1n1 load header'
assert struct.unpack_from('<Q', boot, 0x818)[0] == len(boot), 'boot.bin: not stage 2 alone (records or anything else behind the image)'
span = (len(boot) + 512 + 4 + 16383) // 16384 * 16384
assert span + (32 << 10) <= STAGE1, 'boot.bin: %d bytes do not fit in the footprint of the installed stage 1 (it would refuse them)' % len(boot)
assert b'SF_STOPPED' in boot, 'boot.bin: the bench variant of the stage (it would open a USB proxy on failure)'
assert b'sf/kernel.bin' in boot and b'siliconforest-boot-v' in boot, 'boot.bin: not a stage 2 that reads sf/kernel.bin'
assert root.encode() in boot, 'boot.bin: built for another root'
assert len(kernel) <= 200 << 20, 'kernel.bin: larger than 200 MiB'
at, kinds = 0, []
while True:
    magic, version, kind, flags, crc, stored, raw = struct.unpack_from('<IHHIIQQ', kernel, at); at += 32
    assert magic == 0x46534450 and version == 1, 'kernel.bin: not a record'
    if kind == 0: break
    body = kernel[at:at + stored]; assert len(body) == stored and zlib.crc32(body) == crc, 'kernel.bin: checksum'
    kinds.append(kind)
    if kind == 1: text = body.decode('ascii')
    at += stored + (-(32 + stored) % 16)
assert kinds == [1, 2] and at == len(kernel), 'kernel.bin: not configuration + kernel and nothing else'
args = text.splitlines()[0].split()
assert 'rd=uuid' in args and ('boot-uuid=' + root) in args, 'kernel.bin: built for another root'
assert not any(a.startswith(('sfbench', 'pdsfstop', 'sfpoweroff')) for a in args), 'kernel.bin: a bench image'
is_bench = any(a.startswith(('sfmark', 'sfsession_seconds')) for a in args)
assert bench or not is_bench, 'kernel.bin: a bench image (its session ends by itself)'
# Stage 2 starts more than one processor only when the command line carries the word it was built
# to look for (src/pdsf_boot.c): a stage 2 and a kernel.bin of different namings do not start.
for word in sorted(set(re.findall(rb'[a-z]{2}cpuheld=1', boot))):
    assert word.decode() in args, 'kernel.bin: its command line lacks %s, which this boot.bin requires: install the boot.bin and the kernel.bin of one build together' % word.decode()
print('SFBOOT files ok: boot.bin %d bytes (span %d of %d), kernel.bin %d bytes%s, root %s...' % (
    len(boot), span, STAGE1, len(kernel), ' BENCH SESSION' if is_bench else '', root[:4]))
PY
}
# Rollback validates the complete resulting pair before the first rename.
# The installed boot partition can still carry the original oversized boot.bin.old.
if [ "$WHAT" = --rollback ]; then
  mount -t vfat -o ro "$DEV" "$MNT"
  [ -f "$MNT/sf/boot.bin.old" ] || [ -f "$MNT/sf/kernel.bin.old" ] || { echo "no previous file" >&2; exit 2; }
  RB_BOOT="$MNT/sf/boot.bin"; [ ! -f "$RB_BOOT.old" ] || RB_BOOT="$RB_BOOT.old"
  RB_KERNEL="$MNT/sf/kernel.bin"; [ ! -f "$RB_KERNEL.old" ] || RB_KERNEL="$RB_KERNEL.old"
  check "$RB_BOOT" "$RB_KERNEL" || refuse "rollback pair is not bootable; restore a verified kit through the normal install path"
  umount "$MNT"
  mount -t vfat -o rw,noatime "$DEV" "$MNT"
  for n in $NAMES; do
    [ -f "$MNT/sf/$n.old" ] || continue               # a file without a previous one stays
    [ ! -f "$MNT/sf/$n" ] || mv "$MNT/sf/$n" "$MNT/sf/$n.new"
    mv "$MNT/sf/$n.old" "$MNT/sf/$n"; [ ! -f "$MNT/sf/$n.new" ] || mv "$MNT/sf/$n.new" "$MNT/sf/$n.old"
  done
  sync -f "$MNT"; ls -la "$MNT/sf"; sha256sum "$MNT"/sf/*; echo "SFBOOT rolled back"; exit 0
fi
# Normal install.
[ -f "$WHAT" ] && [ -f "$KERNEL" ] || { echo "two files are needed: <boot.bin> <kernel.bin>" >&2; exit 2; }
# boot.bin: the installed stage 2 and nothing else, small enough for the stage 1 that is in the stub.
# kernel.bin: the records with their checksums, configuration + kernel, built for the root on this disk.

check "$WHAT" "$KERNEL" "${SF_BENCH:-0}" || refuse "the files are not a SiliconForest boot image and kernel for this machine"
mount -t vfat -o rw,noatime "$DEV" "$MNT"
mkdir -p "$MNT/sf"
[ "$(df --output=avail -B1 "$MNT" | tail -1)" -gt $(( $(stat -c %s "$WHAT") + $(stat -c %s "$KERNEL") + (8 << 20) )) ] || refuse "not enough room (remove sf/*.old by hand if they are not wanted)"
src() { case "$1" in boot.bin) echo "$WHAT";; kernel.bin) echo "$KERNEL";; esac; }
CHANGED=
for n in $NAMES; do
  rm -f "$MNT/sf/$n.new"
  if [ -f "$MNT/sf/$n" ] && cmp -s "$(src $n)" "$MNT/sf/$n"; then echo "sf/$n is already this file: left alone"; continue; fi
  cp "$(src $n)" "$MNT/sf/$n.new"; CHANGED="$CHANGED $n"
done
sync -f "$MNT"
for n in $CHANGED; do cmp "$(src $n)" "$MNT/sf/$n.new" || { rm -f "$MNT"/sf/*.new; refuse "the copy of $n differs; nothing was replaced"; }; done
for n in $CHANGED; do
  if [ -f "$MNT/sf/$n" ]; then
    # a kernel.bin of the bench (its session ends by itself) is not kept as the previous file
    if [ "$n" = kernel.bin ] && grep -a -q 'sfsession_seconds=\|sfmark=' <(head -c 4096 "$MNT/sf/$n"); then rm -f "$MNT/sf/$n"
    else mv -f "$MNT/sf/$n" "$MNT/sf/$n.old"; fi
  fi
  mv "$MNT/sf/$n.new" "$MNT/sf/$n"
done
sync -f "$MNT"
umount "$MNT"; mount -t vfat -o ro "$DEV" "$MNT"
for n in $NAMES; do cmp "$(src $n)" "$MNT/sf/$n" || refuse "after the remount sf/$n differs from the file: use --rollback"; done
ls -la "$MNT/sf"; sha256sum "$MNT"/sf/*
echo "SFBOOT updated: SiliconForest starts these files the next time it is chosen. Previous ones are sf/*.old (--rollback)."
