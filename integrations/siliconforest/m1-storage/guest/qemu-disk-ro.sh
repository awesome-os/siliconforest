#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU rehearsal of the 'part' phase of darwin-disk-ro.in: the data disk (4096-byte sectors,
# attached read-only, made by tools/dr_qemu_disk.py) stands for the M1's disk.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
whole=""
for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2; do
    [ -e "$d" ] && gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 *98304' && whole=$d
done
echo "DRQ whole=$whole nodes: $(ls /dev/disk* | tr '\n' ' ')"
gpt -r show "$whole" 2>&1 | sed 's/^/DRQ_GPT /'
index=$(gpt -r show "$whole" 2>/dev/null | awk '$1 == 4102 && $2 == 98304 { print $3 }')
node=$(echo "$whole" | sed 's/rdisk/disk/')s$index
mkdir -p /tmp/sf
sfnvme-test wtest "${whole}s$index" 2>&1
mount -t ext4 "$node" /tmp/sf > /tmp/sf-rw.out 2>&1; echo "DRQ_MOUNT_RW rc=$? $(head -1 /tmp/sf-rw.out)"
mount 2>&1 | grep '/tmp/sf' | sed 's/^/DRQ_MOUNT_RW_STATE /'
umount /tmp/sf 2>/dev/null
for round in 1 2; do
    mount -t ext4 -o rdonly "$node" /tmp/sf 2>&1 | sed 's/^/DRQ_MOUNT_ERR /'
    mount 2>&1 | grep '/tmp/sf' | sed "s/^/DRQ_MOUNT$round /"
    ls /tmp/sf | tr '\n' ' ' | sed "s/^/DRQ_LS$round /"; echo
    if [ "$round" = 1 ]; then
        sfnvme-test tree /tmp/sf etc/passwd bin/sh usr/lib/libSystem.B.dylib 2>&1
        touch /tmp/sf/sf-write-test 2>/tmp/sf-w.out; echo "DRQ_FSWRITE create rc=$? $(head -1 /tmp/sf-w.out)"
        mkdir /tmp/sf/sf-write-dir 2>/tmp/sf-w.out; echo "DRQ_FSWRITE mkdir rc=$? $(head -1 /tmp/sf-w.out)"
        rm -f /tmp/sf/applets.txt 2>/tmp/sf-w.out </dev/null; echo "DRQ_FSWRITE unlink rc=$? $(head -1 /tmp/sf-w.out)"
        sh -c 'echo x >> /tmp/sf/etc/hosts' 2>/tmp/sf-w.out; echo "DRQ_FSWRITE append rc=$? $(head -1 /tmp/sf-w.out)"
        mount -t ext4 -o update,rw "$node" /tmp/sf > /tmp/sf-w.out 2>&1; echo "DRQ_FSWRITE remount-rw rc=$? $(head -1 /tmp/sf-w.out)"
        mount 2>&1 | grep '/tmp/sf' | sed 's/^/DRQ_MOUNT_AFTER_UPGRADE /'
    else
        sfnvme-test tree /tmp/sf 2>&1 | grep NVT_TREE_ALL | sed 's/^NVT_TREE_ALL/NVT_TREE2_ALL/'
    fi
    sync
    umount /tmp/sf; echo "DRQ_UMOUNT$round rc=$?"
done
dmesg 2>/dev/null | grep -i 'ext4' | tail -40 | sed 's/^/DRQ_DMESG /'
echo QDDESK_PASS
echo QDDESK_END
