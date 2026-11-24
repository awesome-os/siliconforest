#!/bin/bash
# SPDX-License-Identifier: MIT
# QEMU: does a mass deletion leave an ext4 filesystem that Linux's e2fsck -fn accepts?
# (guest/qemu-massdelete.sh on a scratch 64 MiB filesystem with the M1 partition's features.)
#   tools/dr_qemu_massdelete.sh <name> [tree|old] [reuse <scratch fs image of an earlier run>]
#     tree  the driver as the collections have it (./ext4-downgrade.patch, with the freed-inode fix)
#     old   the driver of before boot dr41 (./ext4-downgrade.before-dr41.patch): e2fsck -fn returns 4
#   reuse: a second boot on the filesystem an earlier run left (new files take freed inodes, more are deleted)
#   output: runs/m1-storage/qemu-dr/<name>/, the filesystem afterwards in <name>-after.img,
#   e2fsck's words in <name>-fsck.txt. A hard time limit ends the guest (DR_QEMU_TIMEOUT, 600 s).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; WHICH=${2:-tree}; M=integrations/siliconforest/m1-storage; W=runs/m1-storage/qemu-dr
case "$WHICH" in
  tree) ARG=();;
  old) ARG=(--arg ext4Patch ./ext4-downgrade.before-dr41.patch);;
  *) echo "tree or old" >&2; exit 2;;
esac
BASE=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-qemu-image.nix "${ARG[@]}" 2>/dev/null | tail -1)
ls "$BASE"/*.img > /dev/null || { echo "no QEMU base image" >&2; exit 2; }
mkdir -p "$W"; FS=$W/$N-fs.img; DISK=$W/$N-disk.img; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
if [ "${3:-}" = reuse ]; then cp --reflink=auto "${4:?scratch fs image}" "$FS"; echo reuse > "$T/phase"
else truncate -s 64M "$FS"; mke2fs -q -t ext4 -b 4096 -I 256 -L scratch -O metadata_csum,64bit,metadata_csum_seed "$FS"; echo first > "$T/phase"; fi
python3 -B $M/tools/dr_qemu_disk.py "$FS" "$DISK" --guid 5F1D0A7E-0000-4000-8000-0000000000A1 > "$W/$N-disk.txt"
QD_BASE_IMAGE=$(ls "$BASE"/*.img | head -1) DR_QEMU_SINGLE=1 DR_QEMU_KERNEL=debug DR_QEMU_BOOTARGS=" -s" DR_QEMU_FILES="$T/phase=/etc/dr-phase" \
  $M/tools/dr_qemu.sh "$N" $M/guest/qemu-massdelete.sh "$DISK" rw "${DR_QEMU_TIMEOUT:-600}" || true
grep -a '^MD ' "$W/$N/serial.log" | cut -c1-200 || true
dd if="$DISK" of="$W/$N-after.img" bs=4096 skip=$((6 + 4096)) count=$(( $(stat -c %s "$FS") / 4096 )) status=none
set +e; e2fsck -fn "$W/$N-after.img" > "$W/$N-fsck.txt" 2>&1; RC=$?; set -e
echo "MD_HOST $WHICH driver: e2fsck -fn rc=$RC; garbage=$(grep -c 'seems to contain garbage' "$W/$N-fsck.txt" || true); $(tail -1 "$W/$N-fsck.txt")"
