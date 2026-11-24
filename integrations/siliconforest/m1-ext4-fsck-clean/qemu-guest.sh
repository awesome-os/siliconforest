#!/bin/sh
# SPDX-License-Identifier: MIT
# Guest side of run-qemu.py: after every step the scratch filesystem is downgraded to read-only
# (what /sbin/sf-halt does to the root before the write fence is sealed), copied raw to the
# snapshot disk, unmounted and mounted again. @SEED@ and @BLOCKS@ are filled in by the runner.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
node=""; snap=""
for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2 /dev/rdisk3; do
    [ -e "$d" ] || continue
    if gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 '; then node=$(echo "$d" | sed 's/rdisk/disk/')s2
    elif dd if="$d" bs=512 count=1 2>/dev/null | grep -q SF-FSCK-SNAPSHOT-DISK; then snap=$d; fi
done
[ -n "$node" ] && [ -n "$snap" ] || { echo "FSCK_GUEST FAIL devices node=$node snap=$snap"; echo QDDESK_END; exit 1; }
raw="/dev/r${node#/dev/}"; m=/tmp/fsckscratch; mkdir -p $m
mount -t ext4 "$node" $m || { echo 'FSCK_GUEST FAIL mount'; echo QDDESK_END; exit 1; }
sysctl kern.sf_ext4.journal_cache
n=0; bad=0
seal() {
    sync
    mount -t ext4 -o update,rdonly "$node" $m; rc=$?
    # 1 MiB units on the snapshot disk; slot n starts at 1 + n * (size in MiB)
    dd if="$raw" of="$snap" bs=1048576 count=@MIB@ seek=$((1 + n * @MIB@)) 2>/dev/null; drc=$?
    echo "FSCK_GUEST snapshot=$n step=$1 downgrade=$rc copy=$drc"
    [ "$rc" = 0 ] && [ "$drc" = 0 ] || bad=1
    n=$((n + 1))
}
again() {
    umount $m; urc=$?
    mount -t ext4 "$node" $m; mrc=$?
    echo "FSCK_GUEST remount unmount=$urc mount=$mrc"
    [ "$urc" = 0 ] && [ "$mrc" = 0 ] || bad=1
}
for step in pair htree small cache fill purge; do
    /usr/bin/fsck-load $m $step @SEED@ || bad=1
    seal $step
    again
done
# sealed with deleted files still open: they are on the orphan list, which the next mount empties
/usr/bin/fsck-load $m hold @SEED@ &
holder=$!
sleep 3
seal orphans
kill $holder; wait $holder 2>/dev/null
again
seal after-orphans
sysctl kern.sf_ext4.cache_stats
dmesg 2>/dev/null | grep 'ext4:' | grep -E 'repaired|orphan|downgraded|commit failed|corrupt' | tail -40
umount $m
echo "FSCK_GUEST snapshots=$n bad=$bad"
[ "$bad" = 0 ] && echo QDDESK_PASS
echo QDDESK_END
