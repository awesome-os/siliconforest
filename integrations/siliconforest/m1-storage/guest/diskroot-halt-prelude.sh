# ---- disk root, READ-WRITE, ended by the system's own command (m1-storage/tools/dr_desk_boot.sh with DR_RW=1 DR_HALT=...): facts first; tools that are not on the disk are in the memory disk
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/private/ram/usr/bin; export PATH
printf 'NV_EARLY_NCPU %s\n' "$(sysctl -n hw.ncpu)"
printf 'NV_EARLY_BOOTARGS %s\n' "$(sysctl -n kern.bootargs | sed 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g')"
mount 2>&1 | sed 's/^/NV_EARLY_MOUNT /'
dr_rootdev=$(dmesg 2>/dev/null | sed -n 's/.*BSD root: \(disk[0-9s]*\).*/\1/p' | head -1)
if mount | grep '^root_device on / (ext4' | grep -qv 'read-only'; then printf 'NV_EARLY_ROOT disk read-write: %s on %s\n' "$(mount | grep ' on / ')" "$dr_rootdev"
else printf 'NV_EARLY_FAIL the root is not read-write ext4: %s\n' "$(mount | grep ' on / ')"; fi
for n in state part media fence gate_selftest wstats wgate_selftest; do printf 'NV_EARLY_%s %s\n' "$(echo $n | tr a-z A-Z)" "$(sysctl -n kern.sf_nvme.$n 2>/dev/null)"; done
printf 'NV_EARLY_SMART %s\n' "$(sysctl -n kern.sf_nvme.smart)"
dmesg 2>/dev/null | grep 'ext4: \(journal\|.*replay\|.*orphan\)\|boot-uuid\|BSD root' | sed 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g' | tail -8 | sed 's/^/NV_EARLY_DMESG /'
# what earlier boots left: files of this test, and the desktop's own state directory
mkdir -p /private/var/sf-persist /private/var/db/desktop-state
(cd /private/var/sf-persist && find . -type f | sort | sed 's|^\./||' | while read f; do sha256sum "$f"; done) 2>&1 | sed 's/^/NV_PSHA found /'
(cd /private/var/db/desktop-state && find . -type f | sort | sed 's|^\./||' | while read f; do sha256sum "$f"; done) 2>&1 | sed 's/^/NV_DSHA found /'
printf 'written by @NAME@ (%s) to the read-write disk root\n' "$nonce" > /private/var/sf-persist/@NAME@.txt; printf 'NV_EARLY_FSWRITE /private/var/sf-persist/@NAME@.txt rc=%s\n' "$?"
touch /usr/dr-x 2>/dev/null; printf 'NV_EARLY_FSWRITE /usr/dr-x rc=%s\n' "$?"; rm -f /usr/dr-x < /dev/null
# ---- the system's own tools: installed from the RAM root image when this boot carries them (@INSTALL@ = 1), through the fenced driver
if [ "@INSTALL@" = 1 ]; then
    sh /private/ram/usr/share/sf-system/sf-install.sh /private/ram/usr/share/sf-system /; printf 'NV_SFINSTALL rc=%s\n' "$?"
    if [ -r /private/ram/usr/bin/chonkstep-darwin-desktop ] && ! cmp -s /private/ram/usr/bin/chonkstep-darwin-desktop /usr/bin/chonkstep-darwin-desktop; then
        cp /private/ram/usr/bin/chonkstep-darwin-desktop /usr/bin/chonkstep-darwin-desktop.sf-new && chmod 755 /usr/bin/chonkstep-darwin-desktop.sf-new && mv -f /usr/bin/chonkstep-darwin-desktop.sf-new /usr/bin/chonkstep-darwin-desktop
        printf 'NV_SFINSTALL compositor rc=%s %s\n' "$?" "$(sha256sum /usr/bin/chonkstep-darwin-desktop | cut -c1-64)"
    fi
    # what daily use needs besides the desktop (userland/: the SSH server, the Homebrew account, the terminal, the desktop's picture)
    if [ -r /private/ram/usr/share/sf-userland/sf-userland-install.sh ]; then
        sh /private/ram/usr/share/sf-userland/sf-userland-install.sh /private/ram/usr/share/sf-userland / 2>&1 | sed 's/^/NV_SFINSTALL /'
    fi
fi
# ---- once a boot: the clock from the hardware, the last boot's leftovers removed (a no-op when the disk's own darwin-desktop-test already ran it)
ls -A /private/var/run /private/tmp 2>&1 | tr '\n' ' ' | sed 's/^/NV_LEFT before sf-boot (or after it, when the dispatcher ran it): /'; echo
/sbin/sf-boot; printf 'NV_SFBOOT rc=%s stamp=%s\n' "$?" "$([ "$(cat /private/var/run/sf-boot.done 2>/dev/null)" = "$(sysctl -n kern.bootsessionuuid)" ] && echo this-boot || echo MISSING)"
ls -A /private/var/run /private/tmp 2>&1 | tr '\n' ' ' | sed 's/^/NV_LEFT after sf-boot: /'; echo
printf 'NV_CLOCK start epoch=%s utc=%s uptime_s=%s\n' "$(date -u +%s)" "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$(sysctl -n kern.sf_rtc | sed 's/.*read_at_uptime_ms=\([0-9]*\).*/\1/')"
printf 'NV_CLOCK kernel %s\n' "$(sysctl -n kern.sf_rtc 2>&1)"
dmesg 2>/dev/null | grep 'SFReboot: \|thermal: first SMC\|SFApcie.*radio\|SFSmc: .*quiesc' | tail -8 | cut -c1-230 | sed 's/^/NV_DMESG_CLOCK /'
printf 'NV_HALTGATE start %s | %s\n' "$(sysctl -n kern.sf_smc_halt_writes 2>&1)" "$(sysctl -n kern.sf_pmu_writes 2>&1)"
# a file made now must carry the time of now (Linux reads its mtime afterwards)
printf 'made by @NAME@ after the clock was set\n' > /private/var/sf-persist/@NAME@-time.txt
printf 'NV_FTIME @NAME@-time.txt mtime=%s now=%s\n' "$(stat -c %Y /private/var/sf-persist/@NAME@-time.txt 2>&1)" "$(date -u +%s)"
# ---- the desktop's own settings (the menu's Theme and Display rows keep one file each in its state directory)
for f in theme scale wallpaper; do [ -e /private/var/db/desktop-state/chonkstep/$f ] && printf 'NV_DESKSTATE found %s=%s\n' "$f" "$(cat /private/var/db/desktop-state/chonkstep/$f)"; done
[ -e /private/var/db/desktop-state/chonkstep/theme ] || printf 'NV_DESKSTATE found none\n'
# the compositor drives its own menu (synthetic pointer records through its input decoder; labelled so in its report)
if [ -n "@AUTO_ONLY@" ]; then
    export CHONKSTEP_DARWIN_AUTOPILOT=1 CHONKSTEP_DARWIN_AUTOPILOT_WIFI_TOGGLE=0 CHONKSTEP_DARWIN_AUTOPILOT_ONLY=@AUTO_ONLY@
    [ "@AUTO_KEEP@" = 1 ] && export CHONKSTEP_DARWIN_AUTOPILOT_KEEP=1
    [ "@AUTO_END@" = restart ] && export CHONKSTEP_DARWIN_AUTOPILOT_END=restart
fi
# ---- the desktop's own script from here
