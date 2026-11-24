#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU rehearsal of ext4-downgrade.patch on a writable 4096-byte-sector disk. Each phase ENDS
# WITH THE FILESYSTEM STILL MOUNTED and the machine stopped; the host then runs e2fsck -fn on
# the partition, which must be clean without needs_recovery. Phase from /etc/dr-phase:
#   m  the data disk's partition mounted read-write on /tmp/sf, files written, one file held
#      open for writing, then mount -u -o rdonly; writes afterwards must fail
#   r  the ROOT is the data disk's partition, read-write (rd=uuid): files written, then the
#      root downgraded to read-only
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
phase=$(cat /etc/dr-phase 2>/dev/null || echo r)
echo QDDESK_BEGIN
echo "DRD phase=$phase"
mount 2>&1 | sed 's/^/DRD_MOUNT0 /'
down() {   # <special> <mount point>
    mount -u -o rdonly "$2" > /tmp/dr-down.out 2>&1; rc=$?
    echo "DRD downgrade form1 rc=$rc $(head -1 /tmp/dr-down.out 2>/dev/null)"
    if ! mount | grep " on $3 " | grep -q read-only; then
        mount -t ext4 -o update,rdonly "$1" "$2" > /tmp/dr-down.out 2>&1; echo "DRD downgrade form2 rc=$? $(head -1 /tmp/dr-down.out 2>/dev/null)"
    fi
    mount | grep " on $3 " | sed 's/^/DRD_MOUNT1 /'
}
case "$phase" in
m)
    whole=""
    for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2; do
        [ -e "$d" ] && gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 *98304' && whole=$d
    done
    node=$(echo "$whole" | sed 's/rdisk/disk/')s2
    mkdir -p /tmp/sf
    mount -t ext4 "$node" /tmp/sf; echo "DRD rw-mount rc=$? node=$node"
    echo "written before the downgrade" > /tmp/sf/dr-d1.txt
    cp /usr/lib/libSystem.B.dylib /tmp/sf/dr-d2.bin
    mkdir /tmp/sf/dr-ddir; cp /bin/sh /tmp/sf/dr-ddir/sh.bin
    echo "unsynced line" >> /tmp/sf/dr-d3.txt
    # a machine without a clock: files deleted with the time near 1970 must not look like orphan-list pointers to e2fsck
    date -u 010100011970 > /dev/null 2>&1; echo "DRD clock $(date -u +%Y)"
    echo "to be deleted" > /tmp/sf/dr-del1.txt; cp /bin/sh /tmp/sf/dr-del2.bin; sync; rm /tmp/sf/dr-del1.txt /tmp/sf/dr-del2.bin; echo "DRD delete rc=$?"
    ( exec 3>> /tmp/sf/dr-open.txt; echo "held open" >&3; sleep 300 ) &
    sleep 1
    sha256sum /tmp/sf/dr-d1.txt /tmp/sf/dr-d2.bin /tmp/sf/dr-ddir/sh.bin /tmp/sf/dr-d3.txt /tmp/sf/dr-open.txt | sed 's/^/DRD_SHA /'
    down "$node" /tmp/sf /private/tmp/sf
    sh -c 'echo x > /tmp/sf/dr-after.txt' 2>/tmp/dr-err; echo "DRD write-after create rc=$? $(head -1 /tmp/dr-err)"
    sh -c 'echo x >> /tmp/sf/dr-d1.txt' 2>/tmp/dr-err; echo "DRD write-after append rc=$? $(head -1 /tmp/dr-err)"
    rm -f /tmp/sf/dr-d3.txt 2>/tmp/dr-err < /dev/null; echo "DRD write-after unlink rc=$? $(head -1 /tmp/dr-err)"
    sha256sum /tmp/sf/dr-d1.txt /tmp/sf/dr-d2.bin | sed 's/^/DRD_SHA_AFTER /'
    ;;
r)
    echo "written to the read-write root" > /dr-root1.txt
    cp /usr/lib/libSystem.B.dylib /private/dr-root2.bin
    mkdir -p /private/var/dr-rootdir; cp /bin/sh /private/var/dr-rootdir/sh.bin
    sha256sum /dr-root1.txt /private/dr-root2.bin /private/var/dr-rootdir/sh.bin | sed 's/^/DRD_SHA /'
    ps -ax 2>/dev/null | wc -l | sed 's/^/DRD processes /'
    sync
    # mount(8) wants the device node, not the "root_device" name the mount table shows
    whole=""
    for d in /dev/rdisk0 /dev/rdisk1 /dev/rdisk2; do
        [ -e "$d" ] && gpt -r show "$d" 2>/dev/null | grep -q '^ *4102 *98304' && whole=$d
    done
    down "$(echo "$whole" | sed 's/rdisk/disk/')s2" / /
    sh -c 'echo x > /dr-after.txt' 2>/tmp/dr-err; echo "DRD write-after create rc=$? $(head -1 /tmp/dr-err 2>/dev/null)"
    sh -c 'echo x >> /dr-root1.txt' 2>/dev/null; echo "DRD write-after append rc=$?"
    sha256sum /dr-root1.txt /private/dr-root2.bin | sed 's/^/DRD_SHA_AFTER /'
    ;;
esac
sync
dmesg 2>/dev/null | grep 'ext4: ' | tail -8 | sed 's/^/DRD_DMESG /'
echo "DRD left mounted: the machine stops now"
echo QDDESK_PASS
echo QDDESK_END
