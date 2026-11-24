#!/bin/bash
# SPDX-License-Identifier: MIT
# QEMU rehearsal of a disk root's OWN session (system/sf-session started by launchd's job
# org.siliconforest.desktop, no RAM root, nothing typed) and of its end by the system's own
# command from that job, on the M1's root image as a read-write root on a 4096-byte-sector
# GPT disk. Three boots:
#   a  single-user, typed (guest/qemu-halt.sh phase a): sf-install.sh puts the tools, the
#      session, the launchd job and the bench's pieces (tools/dr_bench_templates.sh, a stand-in
#      range list) on the root; ended by `reboot`
#   s  multi-user with sfbench=qs sfsession_seconds=11: launchd starts the job, the dispatcher
#      runs sf-boot and the disk's session, the session runs the bench's prelude, the desktop
#      (which cannot start on this machine: no GPU, the script must survive that), the bench's
#      end script and `reboot`: sf-halt from a launchd job, with launchd's other jobs running.
#   p  multi-user WITHOUT sfbench= (sfsession_seconds=11 sfmark=1): the plain session and its plain `reboot`
# After each the host checks the partition: e2fsck -fn, the superblock, what the boots wrote.
# The machine has no fenced NVMe driver, no SMC and no GPU: the storage facts print empty.
# The virt machine does not reset on `reboot`; qd_run.py ends its own QEMU. No M1, no bench.
#   tools/dr_qemu_session.sh <name>        output in runs/m1-storage/qemu-dr/<name>-a, <name>-s
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; M=integrations/siliconforest/m1-storage; W=runs/m1-storage/qemu-dr; S=$M/system
SRC=${DR_QEMU_ROOT_IMAGE:-artifacts/m1-desktop-010/pcie-enum-candidate-sw01/ramdisk.img}
TOOLS=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-system-tools.nix 2>/dev/null | tail -1)
BASE=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-qemu-image.nix 2>/dev/null | tail -1)
TEST=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-sfnvme-test.nix 2>/dev/null | tail -1)
[ -x "$TOOLS/sbin/sf-halt" ] && [ -x "$TEST/usr/bin/sfnvme-test" ] && ls "$BASE"/*.img > /dev/null || { echo "no tools or no QEMU base image" >&2; exit 2; }
FS=$W/$N-fs.img; DISK=$W/$N-disk.img; GUID=5F1D0A7E-0000-4000-8000-0000000000A1
mkdir -p "$W"; cp --reflink=auto "$SRC" "$FS"; chmod u+w "$FS"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
$M/tools/dr_bench_templates.sh "$T"; printf '0 16\n64 16\n' > "$T/ranges"
printf '#!/bin/sh\necho QDDESK_BEGIN\necho "DRS the typed command does nothing: the session is launchd'"'"'s"\nsleep 3600\n' > "$T/idle"
wr() { echo "rm $2"; echo "write $1 $2"; }
inject() {  # <image> <guest script> <phase>
  { echo "mkdir /usr/share/sf-system"; echo "mkdir /usr/share/sf-system/bench"
    for f in sf-boot sf-command sf-install.sh darwin-desktop-test.disk sf-session; do wr "$S/$f" "/usr/share/sf-system/$f"; done
    wr "$M/../m1-native-boot/guest/org.siliconforest.desktop.plist" /usr/share/sf-system/org.siliconforest.desktop.plist
    wr "$TOOLS/sbin/sf-halt" /usr/share/sf-system/sf-halt; wr "$TOOLS/usr/sbin/sf-clock" /usr/share/sf-system/sf-clock
    wr "$T/bench-prelude.in" /usr/share/sf-system/bench/prelude.in; wr "$T/bench-end.in" /usr/share/sf-system/bench/end.in
    wr "$TEST/usr/bin/sfnvme-test" /usr/share/sf-system/bench/sfnvme-test; wr "$T/ranges" /usr/share/sf-system/bench/sfnvme-ranges
    wr "$2" /usr/bin/dr-guest
    printf '%s\n' "$3" > "$T/phase"; wr "$T/phase" /etc/dr-phase
  } | debugfs -w "$1" > /dev/null 2>&1
}
check() {  # <phase>: the partition out of the disk, as the guest left it
  FIRST=$((6 + 4096)); BLOCKS=$(( $(stat -c %s "$FS") / 4096 ))
  dd if="$DISK" of="$W/$N-after-$1.img" bs=4096 skip=$FIRST count=$BLOCKS status=none
  dumpe2fs -h "$W/$N-after-$1.img" 2>/dev/null | grep -iE 'Filesystem state|Filesystem features|Journal start' | sed "s/^/DRS_HOST $1 /"
  e2fsck -fn "$W/$N-after-$1.img" 2>&1 | tail -2 | sed "s/^/DRS_HOST $1 fsck: /"; echo "DRS_HOST $1 fsck rc=${PIPESTATUS[0]}"
  for f in /private/var/sf-persist/q-a.txt /private/var/sf-persist/qs.txt /usr/libexec/sf-session /System/Library/LaunchDaemons/org.siliconforest.desktop.plist /private/var/db/sf-bench/end.in; do
    echo "DRS_HOST $1 $(debugfs -R "cat $f" "$W/$N-after-$1.img" 2>/dev/null | sha256sum | cut -c1-16) $f"
  done
  echo "DRS_HOST $1 sources: $(sha256sum < $S/sf-session | cut -c1-16) sf-session, $(sha256sum < "$T/bench-end.in" | cut -c1-16) end.in"
}
show() { grep -a 'DRH\|DRS\|SF_\|NV_\(DOWNGRADE\|FSWRITE\|ROOT\|EARLY_ROOT\|SFBOOT\|HALT\|SESSION_END\|PSHA\|PHASE\|FAIL\|STATEWRITE\|WPROBE\|AFTER_STOP\)\|GPUDESK\|ext4: downgraded\|Quick reboot\|MACH Reboot\|SF_BENCH_REBOOT\|panic' "$W/$N-$1/serial.log" | cut -c1-200 | head -${2:-80} || true; }
inject "$FS" $M/guest/qemu-halt.sh a
python3 -B $M/tools/dr_qemu_disk.py "$FS" "$DISK" --guid "$GUID" > "$W/$N-disk.txt"
QD_BASE_IMAGE=$(ls "$BASE"/*.img | head -1) DR_QEMU_SINGLE=1 DR_QEMU_KERNEL=debug \
  DR_QEMU_BOOTARGS="rd=uuid boot-uuid=$GUID -s" $M/tools/dr_qemu.sh "$N-a" $M/guest/qemu-halt.sh "$DISK" rw "${DR_QEMU_TIMEOUT:-420}" || true
show a 40; check a
# the typed command of the second boot is changed in the partition as the guest left it (a host-side edit of a QEMU image)
{ wr "$T/idle" /usr/bin/dr-guest; } | debugfs -w "$W/$N-after-a.img" > /dev/null 2>&1
dd if="$W/$N-after-a.img" of="$DISK" bs=4096 seek=$((6 + 4096)) conv=notrunc status=none
QD_BASE_IMAGE=$(ls "$BASE"/*.img | head -1) DR_QEMU_KERNEL=debug DR_QEMU_UP=SF_BENCH_REBOOT \
  DR_QEMU_BOOTARGS="rd=uuid boot-uuid=$GUID sfbench=qs sfsession_seconds=11" $M/tools/dr_qemu.sh "$N-s" "$T/idle" "$DISK" rw "${DR_QEMU_TIMEOUT:-900}" || true
show s 120; check s
# p: the same root once more WITHOUT sfbench= (the session an installed system has): no prelude, no end script,
# the plain `exec /bin/reboot` at the session's end; sfmark=1 only adds sf-halt's last line for this capture
dd if="$W/$N-after-s.img" of="$DISK" bs=4096 seek=$((6 + 4096)) conv=notrunc status=none
QD_BASE_IMAGE=$(ls "$BASE"/*.img | head -1) DR_QEMU_KERNEL=debug DR_QEMU_UP=SF_BENCH_REBOOT \
  DR_QEMU_BOOTARGS="rd=uuid boot-uuid=$GUID sfsession_seconds=11 sfmark=1" $M/tools/dr_qemu.sh "$N-p" "$T/idle" "$DISK" rw "${DR_QEMU_TIMEOUT:-900}" || true
show p 120; check p
echo "DRS_HOST p bench lines in the plain boot (must be 0): $(grep -a -c '^NV_' "$W/$N-p/serial.log" || true)"
echo "DRS_HOST p $(debugfs -R 'ls -l /Library/Preferences' "$W/$N-after-p.img" 2>/dev/null | awk '$NF == "SystemConfiguration" {print "the directory of the remembered networks is there, mode " $2}')"
