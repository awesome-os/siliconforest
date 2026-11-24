# SPDX-License-Identifier: MIT
# Sourced first by every guest script of a rebase boot (tools/rb_boot.sh).
# Upstream's platform expert publishes /arm-io/ans, the internal NVMe controller, as a nub on
# Apple SoCs since the tip. Nothing in the collection may drive it. If the nub has any child
# in the service plane (a driver attached), or the check itself cannot be made, the session
# ends here: the end marker makes the capture stop and the M1 return to Linux.
rb_ans=$(/usr/sbin/ioreg -p IOService -n ans -r -w 0 2>&1)
rb_rc=$?
rb_lines=$(printf '%s\n' "$rb_ans" | grep -c '+-o ')
rb_disks=$(ls /dev/disk* 2>/dev/null | tr '\n' ' ')
# and no object of an NVMe class anywhere in the service plane, whatever it hangs on
rb_nvme=$(/usr/sbin/ioreg -p IOService -w 0 2>/dev/null | grep '+-o ' | grep -ci 'nvme')
printf '\nRB_NVME ioreg_rc=%s entries=%s nvme_objects=%s disks=%s\n' "$rb_rc" "$rb_lines" "$rb_nvme" "$rb_disks"
printf '%s\n' "$rb_ans" | grep '+-o ' | head -8
if [ "$rb_rc" -ne 0 ] || [ "$rb_lines" -gt 1 ] || [ "${rb_nvme:-1}" -ne 0 ] || ! /usr/sbin/ioreg -p IOService -w 0 2>/dev/null | grep -q 'IOPlatformExpertDevice'; then
    printf 'RB_STOP nvme-nub-check-failed: session ended before anything else ran\n'
    printf '\nPD_BENCH_DONE\n'
    exit 97
fi
printf 'RB_NVME_OK nothing attached to the NVMe nub (entries=%s: 0 no nub, 1 the nub alone)\n' "$rb_lines"
