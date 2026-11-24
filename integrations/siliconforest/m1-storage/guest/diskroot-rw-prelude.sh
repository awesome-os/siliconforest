# ---- disk root, READ-WRITE (m1-storage/tools/dr_desk_boot.sh with DR_RW=1): facts first; tools that are not on the disk are in the memory disk
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
# a desktop setting: light or dark, kept by the shell in its state directory (file "appearance"; the
# Theme menu writes it, a session reads it at start). Asked for through the shell's own request file.
if [ -e /private/var/db/desktop-state/appearance ]; then printf 'NV_DESKSET found appearance=%s\n' "$(cat /private/var/db/desktop-state/appearance)"
else printf 'dark\n' > /private/var/db/desktop-state/appearance-request; printf 'NV_DESKSET none found; requested dark through appearance-request\n'; fi
# a boot's leftovers that a session must not find (a real system clears these at boot)
rm -rf /private/var/run/gpu-desktop < /dev/null
# ---- the desktop's own script from here
