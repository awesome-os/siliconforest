#!/bin/bash
# SPDX-License-Identifier: MIT
# Work on the SiliconForest partition from the M1's Linux, in ONE ssh session, on that
# partition ONLY. Before anything it is identified three ways and the session ends if one
# does not hold: PARTUUID and label (blkid), first block, length (sysfs), against
# private/disk-root/sf-part.env; it must not be mounted. Nothing else on the disk is opened
# for writing. Run it through the bench: BENCH_WHO=disk tools/bench.sh run .../dr_linux_root.sh <cmd>
#   check                  identity, e2fsck -fn, usage
#   transform <sf-roinit>  mount, tools/sf-root-transform.sh on it, unmount, e2fsck -fn
#   tree                   mount read-only (noload), a listing digest, unmount
#   untransform            mount, tools/sf-root-untransform.sh on it (the read-write root of
#                          step 3d; the way back is transform), unmount, e2fsck -fn
#   wcheck                 after a fenced-write boot: the superblock's state BEFORE anything else,
#                          e2fsck -fn (nothing is repaired or replayed), then a read-only mount
#                          without journal replay and the SHA-256 of everything under
#                          /private/sf-wtest. DR_REPLAY=1 (an unclean stop was intended): after
#                          the first e2fsck -fn, e2fsck -fy replays the journal, then -fn again.
#   netlog                 mount read-only (noload), the last lines of the root's /private/var/log/sf-netd.log
#                          (network names not printed), unmount: what the disk's own session reached
#   DR_OUT=<file>          where the output goes (default below)
# Output: runs/m1-storage/linux-root-<cmd>-<time>.txt
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); cd "$ROOT"
. private/disk-root/sf-part.env
CMD=${1:?check|transform|tree|wcheck|untransform|netlog}; INIT=${2:-}
M1="ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=8 -o ServerAliveInterval=15 chrisk@10.1.1.211"
OUT=${DR_OUT:-runs/m1-storage/linux-root-$CMD-$(date +%Y%m%d-%H%M%S).txt}
for i in $(seq 1 36); do $M1 true 2>/dev/null && break; sleep 5; done
{
  echo "WANT_PARTUUID=$SF_PARTUUID WANT_LBA=$SF_PART_LBA WANT_BLOCKS=$SF_PART_BLOCKS CMD=$CMD REPLAY=${DR_REPLAY:-0}"
  if [ "$CMD" = transform ]; then
    [ -f "$INIT" ] || { echo "transform needs the sf-roinit binary" >&2; exit 2; }
    echo "INIT_B64=$(base64 -w0 "$INIT")"; echo "TRANSFORM_B64=$(base64 -w0 "$HERE/sf-root-transform.sh")"
  fi
  [ "$CMD" != untransform ] || echo "UNTRANSFORM_B64=$(base64 -w0 "$HERE/sf-root-untransform.sh")"
  cat <<'REMOTE'
set -u
P=/dev/nvme0n1p3; S=/sys/block/nvme0n1/nvme0n1p3; MNT=/run/sf-dst
fail() { echo "SFL_FAIL $1"; exit 1; }
[ "$(blkid -s PARTUUID -o value $P)" = "$WANT_PARTUUID" ] || fail "PARTUUID of $P is not ours"
[ "$(blkid -s LABEL -o value $P)" = SiliconForest ] || fail "label"
[ "$(( $(cat $S/start) / 8 ))" = "$WANT_LBA" ] || fail "first block"
[ "$(( $(cat $S/size) / 8 ))" = "$WANT_BLOCKS" ] || fail "length"
findmnt -rn -S $P >/dev/null && fail "it is mounted"
echo "SFL_IDENTITY ok (PARTUUID, label, first block, length; not mounted)"
case "$CMD" in
check)
  e2fsck -fn $P 2>&1 | tail -3 | sed 's/^/SFL_FSCK /'; echo "SFL_FSCK_RC ${PIPESTATUS[0]}";;
transform)
  T=$(mktemp -d); echo "$INIT_B64" | base64 -d > $T/sf-roinit; echo "$TRANSFORM_B64" | base64 -d > $T/transform.sh
  mkdir -p $MNT && mount -t ext4 $P $MNT || fail "mount"
  sh $T/transform.sh $MNT $T/sf-roinit 2>&1 | sed 's/^/SFL_TRANSFORM /'; rc=${PIPESTATUS[0]}
  ls -ld $MNT/var $MNT/private/var $MNT/private/tmp $MNT/private/ram $MNT/etc/resolv.conf $MNT/usr/appleinternal/sbin/launchd.sfro 2>&1 | sed 's/^/SFL_LS /'
  sync; umount $MNT || fail "umount"; rm -rf $T
  echo "SFL_TRANSFORM_RC $rc"
  e2fsck -fn $P 2>&1 | tail -3 | sed 's/^/SFL_FSCK /'; echo "SFL_FSCK_RC ${PIPESTATUS[0]}";;
wcheck)
  dumpe2fs -h $P 2>/dev/null | grep -iE 'Filesystem state|features|Journal' | sed 's/^/SFL_SB_FIRST /'
  e2fsck -fn $P > /tmp/sf-wcheck-fsck.txt 2>&1; frc=$?
  tail -4 /tmp/sf-wcheck-fsck.txt | sed 's/^/SFL_FSCK /'; echo "SFL_FSCK_RC $frc"
  # what e2fsck objects to, line by line (a count per kind, and the inodes it calls garbage)
  grep -v '^$\|^Pass [1-5]\|^e2fsck \|could be narrower\|seems to contain garbage\|WARNING: Filesystem still has errors\|files (.*blocks$' /tmp/sf-wcheck-fsck.txt | head -20 | sed 's/^/SFL_FSCK_OTHER /'
  echo "SFL_FSCK_GARBAGE $(sed -n 's/^Inode \([0-9]*\) seems to contain garbage.*/\1/p' /tmp/sf-wcheck-fsck.txt | tr '\n' ' ')"
  if [ "$REPLAY" = 1 ]; then
    e2fsck -fy $P 2>&1 | tail -6 | sed 's/^/SFL_REPLAY /'; echo "SFL_REPLAY_RC ${PIPESTATUS[0]}"
    e2fsck -fn $P 2>&1 | tail -3 | sed 's/^/SFL_FSCK2 /'; echo "SFL_FSCK2_RC ${PIPESTATUS[0]}"
  fi
  mkdir -p $MNT && mount -t ext4 -o ro,noload $P $MNT || fail "mount"
  (cd $MNT/private/sf-wtest 2>/dev/null && find . -type f | LC_ALL=C sort | sed 's|^\./||' | xargs -r sha256sum) 2>&1 | sed 's/^/SFL_WSHA /'
  (cd $MNT && sha256sum bin/sh usr/lib/libSystem.B.dylib) 2>&1 | sed 's/^/SFL_OLDSHA /'
  (cd $MNT/private/var/sf-persist 2>/dev/null && find . -type f | LC_ALL=C sort | sed 's|^\./||' | xargs -r sha256sum) 2>&1 | sed 's/^/SFL_PSHA /'
  (cd $MNT/private/var/db/desktop-state 2>/dev/null && find . -type f | LC_ALL=C sort | sed 's|^\./||' | xargs -r sha256sum) 2>&1 | sed 's/^/SFL_DSHA /'
  # times: Linux's clock now, and the modification times Darwin gave the files it wrote (a clock that was never set gives 1970)
  echo "SFL_NOW $(date +%s)"
  (cd $MNT/private/var/sf-persist 2>/dev/null && find . -type f -printf '%T@ %P\n' | LC_ALL=C sort -k2 | sed 's/\.[0-9]* / /') 2>&1 | sed 's/^/SFL_PTIME /'
  (cd $MNT/private/var/db/desktop-state 2>/dev/null && find . -type f -printf '%T@ %P\n' | LC_ALL=C sort -k2 | sed 's/\.[0-9]* / /') 2>&1 | sed 's/^/SFL_DTIME /'
  (cd $MNT/private/var/db/desktop-state/chonkstep 2>/dev/null && for f in theme scale wallpaper; do [ -f $f ] && echo "$f=$(head -c 60 $f | tr -d '\n')"; done) 2>&1 | sed 's/^/SFL_DESKSTATE /'
  (cd $MNT && ls -A private/var/run private/tmp 2>&1 | tr '\n' ' ') | sed 's/^/SFL_LEFT /'; echo
  dumpe2fs -h $P 2>/dev/null | grep -iE 'Last (mount|write) time|Mount count' | sed 's/^/SFL_SBTIME /'
  echo "SFL_TREE entries=$(cd $MNT && find . | wc -l)"
  df -k $MNT | tail -1 | sed 's/^/SFL_DF /'
  umount $MNT || fail "umount";;
untransform)
  T=$(mktemp -d); echo "$UNTRANSFORM_B64" | base64 -d > $T/untransform.sh
  mkdir -p $MNT && mount -t ext4 $P $MNT || fail "mount"
  sh $T/untransform.sh $MNT 2>&1 | sed 's/^/SFL_UNTRANSFORM /'; rc=${PIPESTATUS[0]}
  ls -ld $MNT/var $MNT/private/var $MNT/private/tmp $MNT/etc/resolv.conf $MNT/var.rolink $MNT/private/var.rolink $MNT/private/sf-wtest 2>&1 | sed 's/^/SFL_LS /'
  sync; umount $MNT || fail "umount"; rm -rf $T
  echo "SFL_UNTRANSFORM_RC $rc"
  e2fsck -fn $P 2>&1 | tail -3 | sed 's/^/SFL_FSCK /'; echo "SFL_FSCK_RC ${PIPESTATUS[0]}";;
netlog)
  mkdir -p $MNT && mount -t ext4 -o ro,noload $P $MNT || fail "mount"
  # /var and /private/var are two directories on this root (no link between them): the
  # launchd jobs write below /var, the session's own files are below /private/var
  mask() { sed -e 's/ssid=[^ ]*/ssid=(not printed)/g' -e 's/[0-9a-fA-F:]\{17\}/(address)/g' | cut -c1-160; }
  for d in var/run var/log private/var/run private/var/log Library/Preferences Library/Preferences/SystemConfiguration; do
    if [ -d $MNT/$d ] && [ ! -L $MNT/$d ]; then echo "SFL_NETDIR /$d: $(ls -A $MNT/$d | tr '\n' ' ' | cut -c1-300)"
    else echo "SFL_NETDIR /$d: $(ls -ld $MNT/$d 2>&1 | awk '{print $1, $(NF-1), $NF}')"; fi
  done
  K=$MNT/Library/Preferences/SystemConfiguration/org.siliconforest.wifi.known
  if [ -f $K ]; then echo "SFL_NETKNOWN remembered networks: $(wc -l < $K) (names and keys not printed) mode=$(stat -c %a $K)"; else echo "SFL_NETKNOWN no file of remembered networks"; fi
  for f in var/log/sf-netd.log var/log/wifi-autojoin.log var/log/mdnsd.log var/run/sf-netd.en0.status; do
    if [ -f $MNT/$f ]; then
      echo "SFL_NETLOG /$f bytes=$(stat -c %s $MNT/$f) mtime=$(stat -c %Y $MNT/$f)"
      tail -n 25 $MNT/$f | mask | sed "s|^|SFL_NETLOG $(basename $f): |"
    else echo "SFL_NETLOG /$f: not there"; fi
  done
  echo "SFL_NOW $(date +%s)"
  umount $MNT || fail "umount";;
tree)
  mkdir -p $MNT && mount -t ext4 -o ro,noload $P $MNT || fail "mount"
  echo "SFL_TREE entries=$(cd $MNT && find . | wc -l) listing=$(cd $MNT && find . -printf '%y %p %s %l\n' | grep -v '^d ' | LC_ALL=C sort | sha256sum | cut -c1-64)"
  df -k $MNT | tail -1 | sed 's/^/SFL_DF /'
  umount $MNT || fail "umount";;
esac
dumpe2fs -h $P 2>/dev/null | grep -iE 'state|Mount count|features' | sed 's/^/SFL_SB /'
dmesg | grep -iE 'EXT4-fs.*nvme0n1p3' | tail -4 | sed 's/^/SFL_DMESG /'
echo SFL_END
REMOTE
} | sed -e '1s/^/export /' -e 's/^\(INIT_B64\|TRANSFORM_B64\|UNTRANSFORM_B64\)=/export &/' | $M1 'sudo -n bash -s' > "$OUT" 2>&1 || echo "ssh session ended with $?" >&2
grep -a "^SFL_" "$OUT" | cut -c1-200
grep -q "^SFL_END" "$OUT"
