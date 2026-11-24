#!/bin/bash
# SPDX-License-Identifier: MIT
# QEMU rehearsal of the disk root's boot and shutdown path (../system) on the M1's own root
# image as a read-write root on a 4096-byte-sector GPT disk: two boots of guest/qemu-halt.sh
# (phases a and b), each ended by the system's own command (`reboot`, `shutdown -r now`), and
# after each the host's e2fsck -fn of the partition, the superblock's state and the hashes of
# the files the guest wrote. No M1, no bench.
#   tools/dr_qemu_halt.sh <name>          output in runs/m1-storage/qemu-dr/<name>-a, <name>-b
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; M=integrations/siliconforest/m1-storage; W=runs/m1-storage/qemu-dr; S=$M/system
SRC=${DR_QEMU_ROOT_IMAGE:-artifacts/m1-desktop-010/pcie-enum-candidate-sw01/ramdisk.img}
TOOLS=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-system-tools.nix 2>/dev/null | tail -1)
BASE=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-qemu-image.nix 2>/dev/null | tail -1)
[ -x "$TOOLS/sbin/sf-halt" ] && ls "$BASE"/*.img > /dev/null || { echo "no tools or no QEMU base image" >&2; exit 2; }
FS=$W/$N-fs.img; DISK=$W/$N-disk.img; GUID=5F1D0A7E-0000-4000-8000-0000000000A1
mkdir -p "$W"; cp --reflink=auto "$SRC" "$FS"; chmod u+w "$FS"
inject() {  # <image> <phase>
  { echo "mkdir /usr/share/sf-system"
    echo "rm /usr/share/sf-system/org.siliconforest.desktop.plist"; echo "write $M/../m1-native-boot/guest/org.siliconforest.desktop.plist /usr/share/sf-system/org.siliconforest.desktop.plist"
    for f in sf-boot sf-command sf-install.sh darwin-desktop-test.disk sf-session; do echo "rm /usr/share/sf-system/$f"; echo "write $S/$f /usr/share/sf-system/$f"; done
    echo "rm /usr/share/sf-system/sf-halt"; echo "write $TOOLS/sbin/sf-halt /usr/share/sf-system/sf-halt"
    echo "rm /usr/share/sf-system/sf-clock"; echo "write $TOOLS/usr/sbin/sf-clock /usr/share/sf-system/sf-clock"
    echo "rm /usr/bin/dr-guest"; echo "write $M/guest/qemu-halt.sh /usr/bin/dr-guest"
    echo "rm /etc/dr-phase"; printf '%s\n' "$2" > "$W/$N-phase"; echo "write $W/$N-phase /etc/dr-phase"
  } | debugfs -w "$1" > /dev/null 2>&1
}
check() {  # <phase>: the partition out of the disk, as the guest left it
  FIRST=$((6 + 4096)); BLOCKS=$(( $(stat -c %s "$FS") / 4096 ))
  dd if="$DISK" of="$W/$N-after-$1.img" bs=4096 skip=$FIRST count=$BLOCKS status=none
  dumpe2fs -h "$W/$N-after-$1.img" 2>/dev/null | grep -iE 'Filesystem state|Filesystem features|Journal start' | sed "s/^/DRH_HOST $1 /"
  e2fsck -fn "$W/$N-after-$1.img" 2>&1 | tail -2 | sed "s/^/DRH_HOST $1 fsck: /"; echo "DRH_HOST $1 fsck rc=${PIPESTATUS[0]}"
  for f in /private/var/sf-persist/q-a.txt /private/var/sf-persist/q-b.txt; do
    echo "DRH_HOST $1 $(debugfs -R "cat $f" "$W/$N-after-$1.img" 2>/dev/null | sha256sum | cut -c1-64) $f"
  done
  debugfs -R "ls -l /private/var/run" "$W/$N-after-$1.img" 2>/dev/null | awk '{print $NF}' | tr '\n' ' ' | sed "s/^/DRH_HOST $1 var-run: /"; echo
  debugfs -R "ls -l /private/tmp" "$W/$N-after-$1.img" 2>/dev/null | awk '{print $NF}' | tr '\n' ' ' | sed "s/^/DRH_HOST $1 tmp: /"; echo
}
for PHASE in a b; do
  if [ "$PHASE" = a ]; then
    inject "$FS" a
    python3 -B $M/tools/dr_qemu_disk.py "$FS" "$DISK" --guid "$GUID" > "$W/$N-disk.txt"
  else
    # the phase file is changed in the partition as the guest left it (a host-side edit of a QEMU image)
    inject "$W/$N-after-a.img" b
    dd if="$W/$N-after-a.img" of="$DISK" bs=4096 seek=$((6 + 4096)) conv=notrunc status=none
  fi
  QD_BASE_IMAGE=$(ls "$BASE"/*.img | head -1) DR_QEMU_SINGLE=1 DR_QEMU_KERNEL=debug \
    DR_QEMU_BOOTARGS="rd=uuid boot-uuid=$GUID -s" $M/tools/dr_qemu.sh "$N-$PHASE" $M/guest/qemu-halt.sh "$DISK" rw "${DR_QEMU_TIMEOUT:-420}" || true
  grep -a 'DRH\|SF_\|NV_DOWNGRADE\|NV_FSWRITE\|NV_MOUNT\|ext4: downgraded\|Quick reboot\|MACH Reboot' "$W/$N-$PHASE/serial.log" | cut -c1-220 || true
  check "$PHASE"
done
