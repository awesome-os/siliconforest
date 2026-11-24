#!/bin/sh
# SPDX-License-Identifier: MIT
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
node=""
for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2; do
    [ -e "$d" ] && gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 ' && node=$(echo "$d" | sed 's/rdisk/disk/')s2
done
[ -n "$node" ] || { echo 'EOF_GUEST FAIL no scratch device'; echo QDDESK_END; exit 1; }
mkdir -p /tmp/eofscratch
mount -t ext4 "$node" /tmp/eofscratch || { echo 'EOF_GUEST FAIL mount'; echo QDDESK_END; exit 1; }
/usr/bin/eof-io; rc=$?
sync
umount /tmp/eofscratch; urc=$?
echo "EOF_GUEST result=$rc unmount=$urc"
[ "$rc" = 0 ] && [ "$urc" = 0 ] && echo QDDESK_PASS
echo QDDESK_END
