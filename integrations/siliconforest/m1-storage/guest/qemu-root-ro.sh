#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU rehearsal of a read-only disk root: run from the data disk's own root filesystem.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
echo QDDESK_BEGIN
mount 2>&1 | sed 's/^/DRR_MOUNT /'
mount | grep -q ' on / (ext4.*read-only' && echo "DRR root read-only: yes" || echo "DRR root read-only: NO"
mount | grep -q ' on /private/ram (ext4' && ! mount | grep ' on /private/ram ' | grep -q read-only && echo "DRR state writable: yes" || echo "DRR state writable: NO"
for f in /var/run/dr-x /tmp/dr-x /private/var/tmp/dr-x /etc/resolv.conf; do
    sh -c "echo hello >> $f" 2>/dev/null; echo "DRR write $f rc=$? $(ls -l $f 2>&1 | cut -c1-80)"
done
for f in /etc/dr-x /usr/dr-x /dr-x; do touch $f 2>/tmp/dr-err; echo "DRR rootwrite $f rc=$? $(head -1 /tmp/dr-err)"; done
ls -ld /var /tmp /private/var /private/tmp /private/ram 2>&1 | sed 's/^/DRR_LS /'
ps -ax 2>/dev/null | head -30 | sed 's/^/DRR_PS /'
dmesg 2>/dev/null | grep -i 'ext4\|sf-roinit\|load_init\|BSD root\|rooting' | tail -30 | sed 's/^/DRR_DMESG /'
sync
echo QDDESK_PASS
echo QDDESK_END
