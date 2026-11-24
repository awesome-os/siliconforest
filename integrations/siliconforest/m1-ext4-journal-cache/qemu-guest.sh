#!/bin/sh
# SPDX-License-Identifier: MIT
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
trap 'echo CACHE_GUEST_EXIT; echo QDDESK_END' 0
echo QDDESK_BEGIN
echo 'UPSTREAM_QEMU inventory'
ioreg -p IOService -w 0
echo 'UPSTREAM_QEMU devices'
ls /dev/*disk* /dev/usb* 2>/dev/null
/usr/bin/usb-read; usb_rc=$?
node=""
for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2 /dev/rdisk3; do
    [ -e "$d" ] && gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 ' && node=$(echo "$d" | sed 's/rdisk/disk/')s2
done
[ -n "$node" ] || { echo 'UPSTREAM_QEMU FAIL no scratch'; echo QDDESK_END; exit 1; }
rawnode="/dev/r${node#/dev/}"
/usr/bin/upstream-ext4.util -q "$rawnode"; util_rc=$?
/usr/bin/frozen-ext4.util -q "$rawnode"; frozen_util_rc=$?
mkdir -p /tmp/eofscratch
mount -t ext4 "$node" /tmp/eofscratch || { echo 'UPSTREAM_QEMU FAIL mount'; echo QDDESK_END; exit 1; }
echo CACHE_REHEARSAL_BEGIN
for mode in 0 1; do
    sysctl kern.sf_ext4.journal_cache=$mode || exit 1
    sysctl kern.sf_ext4.cache_stats
    mkdir /tmp/eofscratch/cache$mode || exit 1
    for i in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31; do
        f=/tmp/eofscratch/cache$mode/file-$i
        printf 'CACHE-PATTERN-%s-%s\n' "$mode" "$i" > "$f" || exit 1
        [ "$(cat "$f")" = "CACHE-PATTERN-$mode-$i" ] || exit 1
        chmod 640 "$f" || exit 1
        mv "$f" "$f.saved" || exit 1
        cp "$f.saved" "$f.tmp" || exit 1
        rm "$f.tmp" || exit 1
    done
    sysctl kern.sf_ext4.cache_stats
    echo CACHE_REHEARSAL_MODE_$mode=PASS
 done
sysctl kern.sf_ext4.journal_cache=1 || exit 1
echo CACHE_REHEARSAL_END
/usr/bin/eof-io; rc=$?
sync
umount /tmp/eofscratch; urc=$?
echo "UPSTREAM_QEMU io=$rc unmount=$urc usb=$usb_rc utility=$util_rc frozen_utility=$frozen_util_rc"
[ "$rc" = 0 ] && [ "$urc" = 0 ] && [ "$usb_rc" = 0 ] && [ "$util_rc" = 253 ] && [ "$frozen_util_rc" = 254 ] && echo QDDESK_PASS
echo QDDESK_END
