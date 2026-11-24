#!/bin/sh
# SPDX-License-Identifier: MIT
# QEMU rehearsal of the disk root's own boot and shutdown path (system/: sf-install.sh, sf-boot,
# the reboot/shutdown commands, sf-halt) with the ROOT on the data disk's partition, read-write
# (tools/dr_qemu_halt.sh). Phase from /etc/dr-phase:
#   a  install the tools from /usr/share/sf-system, leave files of "the previous boot" in
#      /private/var/run and /private/tmp, write a file, start one process that ignores SIGTERM,
#      then the user's command `reboot`
#   b  the next boot: sf-boot (once: clock, cleanup), the leftovers must be gone, the file must
#      be there, a second sf-boot must do nothing, then `shutdown -r now`
# The machine has no SMC and no fenced NVMe driver: sf-clock says so, sf-halt only downgrades.
# The host then checks the partition: e2fsck -fn clean, no needs_recovery, the file's hash.
PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH
phase=$(cat /etc/dr-phase 2>/dev/null || echo a)
echo QDDESK_BEGIN
echo "DRH phase=$phase"
mount 2>&1 | sed 's/^/DRH_MOUNT /'
case "$phase" in
a)
    sh /usr/share/sf-system/sf-install.sh /usr/share/sf-system /; echo "DRH install rc=$?"
    mkdir -p /private/var/run/gpu-desktop/session-x /private/var/sf-persist
    echo sock > /private/var/run/gpu-desktop/wayland-0.lock; echo pid > /private/var/run/stale.pid
    echo t > /private/tmp/stale1; echo h > /private/tmp/.hidden; mkdir -p /private/tmp/dir/sub; echo d > /private/tmp/dir/sub/f
    echo "written in phase a" > /private/var/sf-persist/q-a.txt
    sha256sum /private/var/sf-persist/q-a.txt | sed 's/^/DRH_SHA /'
    /sbin/sf-boot; echo "DRH sf-boot rc=$?"
    ls -la /private/var/run /private/tmp 2>&1 | sed 's/^/DRH_LS_A /'
    # what the next boot must clear (made after this boot's own cleanup)
    mkdir -p /private/var/run/gpu-desktop/session-x
    echo sock > /private/var/run/gpu-desktop/wayland-0.lock; echo pid > /private/var/run/stale.pid
    echo t > /private/tmp/stale1; echo h > /private/tmp/.hidden; mkdir -p /private/tmp/dir/sub; echo d > /private/tmp/dir/sub/f
    sf-halt restart -n; echo "DRH plan rc=$?"
    poweroff; echo "DRH poweroff refused rc=$?"
    ( trap '' TERM; while :; do sleep 1; done ) &
    sleep 300 &
    ps -ax | sed 's/^/DRH_PS /' | head -20
    echo "DRH now: reboot"
    SF_HALT_REPORT=1 SF_HALT_MARKER=QDDESK_END exec reboot
    ;;
b)
    ls -la /private/var/run /private/tmp 2>&1 | sed 's/^/DRH_LS_BEFORE /'
    /sbin/sf-boot; echo "DRH sf-boot rc=$?"
    ls -la /private/var/run /private/tmp 2>&1 | sed 's/^/DRH_LS_AFTER /'
    left=$(ls -A /private/tmp | wc -l); run=$(ls -A /private/var/run | grep -v '^sf-boot.done$' | wc -l)
    echo "DRH leftovers tmp=$left run=$run"
    echo "made by this boot" > /private/var/run/this-boot.pid
    /sbin/sf-boot; echo "DRH second sf-boot rc=$? this-boot.pid $([ -e /private/var/run/this-boot.pid ] && echo kept || echo REMOVED)"
    sha256sum /private/var/sf-persist/q-a.txt | sed 's/^/DRH_SHA /'
    echo "written in phase b" > /private/var/sf-persist/q-b.txt
    sha256sum /private/var/sf-persist/q-b.txt | sed 's/^/DRH_SHA /'
    ls -l /bin/reboot /bin/reboot.xnu /bin/shutdown /sbin/sf-halt 2>&1 | sed 's/^/DRH_LS /'
    echo "DRH stat mtime=$(stat -c %Y /private/var/sf-persist/q-b.txt 2>&1) now=$(date -u +%s) bootsession=$(sysctl -n kern.bootsessionuuid | wc -c)"
    dmesg 2>/dev/null | grep 'ext4: ' | tail -6 | sed 's/^/DRH_DMESG /'
    shutdown now; echo "DRH shutdown without -r or -h rc=$?"
    echo "DRH now: shutdown -r now"
    SF_HALT_MARKER=QDDESK_END exec shutdown -r now
    ;;
esac
echo "DRH the halt command returned: FAILED"
echo QDDESK_END
