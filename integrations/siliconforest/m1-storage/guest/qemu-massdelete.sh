#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU: a mass deletion on a scratch ext4 filesystem (tools/dr_qemu_massdelete.sh). Files are
# made in one directory, then most of them removed, a few new ones made (they share
# inode-table blocks with the freed inodes), clean unmount. The host then runs e2fsck -fn.
# Boot dr41 on the M1: the driver left the extents flag on freed inodes over a zeroed i_block,
# and e2fsck reported the valid inodes next to them as garbage.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
node=""
for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2; do
    [ -e "$d" ] && gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 ' && node=$(echo "$d" | sed 's/rdisk/disk/')s2
done
echo "MD node=$node"
mkdir -p /tmp/md; mount -t ext4 "$node" /tmp/md; echo "MD mount rc=$?"
if [ "$(cat /etc/dr-phase 2>/dev/null)" != reuse ]; then
    mkdir -p /tmp/md/keep /tmp/md/d /tmp/md/d/sub
    echo "kept from the start" > /tmp/md/keep/first.txt
    i=0; while [ $i -lt 240 ]; do echo "file $i" > /tmp/md/d/f$i; i=$((i + 1)); done
    i=0; while [ $i -lt 40 ]; do ln -s "../f$i" /tmp/md/d/sub/l$i; mkdir /tmp/md/d/sub/dir$i; i=$((i + 1)); done
    dd if=/dev/zero of=/tmp/md/d/big bs=65536 count=40 2>/dev/null
    sync
    echo "MD made: $(ls /tmp/md/d | wc -l) entries in d"
    # every eighth file stays: valid inodes in the middle of the freed ones
    i=0; while [ $i -lt 240 ]; do [ $((i % 8)) = 0 ] || rm -f /tmp/md/d/f$i < /dev/null; i=$((i + 1)); done
    rm -rf /tmp/md/d/sub /tmp/md/d/big < /dev/null
    echo "made after the deletion" > /tmp/md/keep/after.txt
    : > /tmp/md/keep/truncated.txt; echo "grown again" >> /tmp/md/keep/truncated.txt
    echo "MD left: $(ls /tmp/md/d | wc -l) entries in d"
else
    # a later boot of the same filesystem: new files take freed inodes, a second deletion
    i=0; while [ $i -lt 60 ]; do echo "second $i" > /tmp/md/d/g$i; i=$((i + 1)); done
    i=0; while [ $i -lt 60 ]; do [ $((i % 4)) = 0 ] || rm -f /tmp/md/d/g$i < /dev/null; i=$((i + 1)); done
    echo "MD reuse left: $(ls /tmp/md/d | wc -l) entries in d"
fi
(cd /tmp/md && find . -type f | sort | while read f; do sha256sum "$f"; done | sha256sum | sed 's/^/MD tree /')
sync; umount /tmp/md; echo "MD umount rc=$?"
echo QDDESK_PASS
echo QDDESK_END
