# ---- disk root (m1-storage/tools/dr_desk_boot.sh): facts first; tools that are not on the disk are in the memory disk
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/private/ram/usr/bin; export PATH
printf 'NV_EARLY_NCPU %s\n' "$(sysctl -n hw.ncpu)"
printf 'NV_EARLY_BOOTARGS %s\n' "$(sysctl -n kern.bootargs | sed 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g')"
mount 2>&1 | sed 's/^/NV_EARLY_MOUNT /'
dr_rootdev=$(dmesg 2>/dev/null | sed -n 's/.*BSD root: \(disk[0-9]*s[0-9]*\).*/\1/p' | head -1)
if mount | grep -q '^root_device on / (ext4.*read-only'; then printf 'NV_EARLY_ROOT disk read-only: %s on %s\n' "$(mount | grep ' on / ')" "$dr_rootdev"
else printf 'NV_EARLY_FAIL the root is not read-only ext4: %s\n' "$(mount | grep ' on / ')"; fi
for n in state part media fence gate_selftest; do printf 'NV_EARLY_%s %s\n' "$(echo $n | tr a-z A-Z)" "$(sysctl -n kern.sf_nvme.$n 2>/dev/null)"; done
printf 'NV_EARLY_SMART %s\n' "$(sysctl -n kern.sf_nvme.smart)"
touch /usr/dr-x 2>/dev/null; printf 'NV_EARLY_FSWRITE /usr/dr-x rc=%s\n' "$?"
sh -c 'echo hello >> /var/run/dr-x' 2>/dev/null; printf 'NV_EARLY_STATEWRITE /var/run/dr-x rc=%s\n' "$?"
# ---- the desktop's own script from here
