#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU characterisation of the tree's ext4 driver on a WRITABLE 4096-byte-sector disk, for the
# write-fenced driver's design. One script, the phase chosen by the file /etc/dr-phase:
#   a  a read-only mount on a writable device (what does it write?), then a read-write mount,
#      files written, clean unmount
#   b  read-write mount, files written and synced, more written, and the machine is stopped
#      with the filesystem mounted (an unclean stop)
#   c  mount after the unclean stop (journal replay), what is there, clean unmount
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
phase=$(cat /etc/dr-phase)
echo QDDESK_BEGIN
whole=""
for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2; do
    [ -e "$d" ] && gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 *98304' && whole=$d
done
node=$(echo "$whole" | sed 's/rdisk/disk/')s2
echo "DRW phase=$phase node=$node"
mkdir -p /tmp/sf
stats() { dmesg | grep 'ext4: \(unmount stats\|journal\|mounted\)' | tail -${1:-3} | sed 's/^/DRW_DMESG /'; }
case "$phase" in
a)
    mount -t ext4 -o rdonly "$node" /tmp/sf; echo "DRW ro-mount rc=$?"
    mount | grep /tmp/sf | sed 's/^/DRW_MOUNT /'
    sha256sum /tmp/sf/bin/sh | sed 's/^/DRW ro-read /'
    umount /tmp/sf; echo "DRW ro-umount rc=$?"
    stats 3
    mount -t ext4 "$node" /tmp/sf; echo "DRW rw-mount rc=$?"
    mount | grep /tmp/sf | sed 's/^/DRW_MOUNT /'
    echo "written by phase a" > /tmp/sf/dr-a.txt
    mkdir /tmp/sf/dr-dir && cp /tmp/sf/usr/lib/libSystem.B.dylib /tmp/sf/dr-dir/copy.bin
    t0=$(date +%s); dd if=/dev/zero bs=1048576 count=20 2>/dev/null | tr '\0' 'x' > /tmp/sf/dr-20m.bin; sync; echo "DRW 20MiB seconds=$(( $(date +%s) - t0 ))"
    rm /tmp/sf/applets.txt; echo "DRW rm rc=$?"
    sha256sum /tmp/sf/dr-a.txt /tmp/sf/dr-dir/copy.bin /tmp/sf/dr-20m.bin | sed 's/^/DRW_SHA /'
    sync; umount /tmp/sf; echo "DRW rw-umount rc=$?"
    stats 3
    ;;
b)
    mount -t ext4 "$node" /tmp/sf; echo "DRW rw-mount rc=$?"
    echo "synced before the stop" > /tmp/sf/dr-b-synced.txt
    cp /tmp/sf/usr/lib/libSystem.B.dylib /tmp/sf/dr-b-synced.bin
    sync; sleep 2
    sha256sum /tmp/sf/dr-b-synced.txt /tmp/sf/dr-b-synced.bin | sed 's/^/DRW_SHA /'
    echo "not synced" > /tmp/sf/dr-b-unsynced.txt
    mkdir /tmp/sf/dr-b-dir; cp /tmp/sf/bin/sh /tmp/sf/dr-b-dir/sh.bin
    echo "DRW left mounted: the machine stops now"
    ;;
c)
    mount -t ext4 "$node" /tmp/sf; echo "DRW rw-mount-after-unclean rc=$?"
    stats 4
    ls -l /tmp/sf/dr-b-synced.txt /tmp/sf/dr-b-synced.bin /tmp/sf/dr-b-unsynced.txt /tmp/sf/dr-b-dir 2>&1 | sed 's/^/DRW_LS /'
    sha256sum /tmp/sf/dr-b-synced.txt /tmp/sf/dr-b-synced.bin 2>&1 | sed 's/^/DRW_SHA /'
    sfnvme-test tree /tmp/sf | grep ALL
    umount /tmp/sf; echo "DRW umount rc=$?"
    stats 2
    ;;
esac
# the running system's own libSystem: are the loader's cache changes written back to a writable root?
sha256sum /usr/lib/libSystem.B.dylib | sed 's/^/DRW_ROOTLIB /'
sync
echo QDDESK_PASS
echo QDDESK_END
