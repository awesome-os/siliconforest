#!/bin/sh
# SPDX-License-Identifier: MIT
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
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
/usr/bin/eof-io; rc=$?
sync
umount /tmp/eofscratch; urc=$?
echo "UPSTREAM_QEMU io=$rc unmount=$urc usb=$usb_rc utility=$util_rc frozen_utility=$frozen_util_rc"
[ "$rc" = 0 ] && [ "$urc" = 0 ] && [ "$usb_rc" = 0 ] && [ "$util_rc" = 253 ] && [ "$frozen_util_rc" = 254 ] && echo QDDESK_PASS
echo QDDESK_END
