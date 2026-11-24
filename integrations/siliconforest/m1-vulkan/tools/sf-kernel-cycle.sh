#!/bin/bash
# SPDX-License-Identifier: MIT
# One kernel install cycle on the M1, run from the workstation INSIDE runs/m1-coord/gui.lock (the
# caller holds it and announced the reboot 2 minutes ahead in runs/m1-coord/log.txt):
#   sf-kernel-cycle.sh <kit directory with kernel.bin and boot.bin> <name>
#   sf-kernel-cycle.sh --rollback <name>
# SiliconForest -> (marks for the .next + inject compositor; sync; reboot) -> the M1's Linux (default
# boot) -> ONE Linux session: identity of the SiliconForest root, e2fsck -fn (read-only), the kit's
# files to SFBOOT with linux/sf-bootbin-install.sh (each replaced file kept as .old; --rollback
# brings them back), --show, asahi-bless --next (one boot of SiliconForest; the default stays Linux),
# reboot -> SiliconForest polled every 9 s. Everything said is in runs/sfvk/kcycle-<name>.txt.
# Identifiers come from private/disk-root/sf-part.env at run time and are not printed.
set -uo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); cd "$ROOT"
. private/disk-root/sf-part.env
AGENT=${SFVK_AGENT:-GFX2}
if [ "${1:-}" = --rollback ]; then KIT=; NAME=${2:?name}; else KIT=$(cd "${1:?kit}" && pwd); NAME=${2:?name}; fi
OUT=runs/sfvk/kcycle-$NAME.txt
SF=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o GlobalKnownHostsFile=/dev/null
    -o StrictHostKeyChecking=yes -o HostKeyAlias=m1-siliconforest -o BatchMode=yes -o ConnectTimeout=6 root@10.1.1.211)
LX=(-o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=10)
log() { echo "$(date -u +%FT%TZ) $AGENT $*" >> runs/m1-coord/log.txt; echo "$*"; }
: > "$OUT"
if [ -n "$KIT" ]; then
  [ -f "$KIT/kernel.bin" ] && [ -f "$KIT/boot.bin" ] || { echo "no kit in $KIT"; exit 2; }
  echo "kit $(sha256sum "$KIT/kernel.bin" | cut -c1-16) kernel.bin, $(sha256sum "$KIT/boot.bin" | cut -c1-16) boot.bin" | tee -a "$OUT"
fi
# 1. SiliconForest: the standard compositor marks for the next boot, then a clean reboot (lands in Linux)
if "${SF[@]}" true 2>/dev/null; then
  "${SF[@]}" 'touch /private/var/db/desktop-next-try /private/var/db/desktop-inject; sync; sync; echo SF_UUID $(sysctl -n kern.kernelcacheuuid); (sleep 1; reboot) > /dev/null 2>&1 &' 2>&1 | tee -a "$OUT"
  log "SiliconForest rebooting for kernel cycle $NAME"
  sleep 60
else
  echo "SiliconForest not answering: assuming the M1 is in Linux" | tee -a "$OUT"
fi
# 2. Linux: wait (Linux's sshd rate-limits: one attempt every 15 s), copy, then one session
for i in $(seq 1 40); do ssh "${LX[@]}" chrisk@10.1.1.211 true 2>/dev/null && break; sleep 15; done
sleep 10
if [ -n "$KIT" ]; then
  scp -q "${LX[@]}" "$KIT/kernel.bin" "$KIT/boot.bin" integrations/siliconforest/m1-native-boot/install/linux/sf-bootbin-install.sh \
    chrisk@10.1.1.211:/tmp/ || { echo "copy to Linux failed" | tee -a "$OUT"; exit 5; }
  sleep 10
  WHAT="/tmp/boot.bin /tmp/kernel.bin"
else
  scp -q "${LX[@]}" integrations/siliconforest/m1-native-boot/install/linux/sf-bootbin-install.sh chrisk@10.1.1.211:/tmp/ || exit 5
  sleep 10
  WHAT=--rollback
fi
{
  echo "export WANT_PARTUUID=$SF_PARTUUID WANT_LBA=$SF_PART_LBA WANT_BLOCKS=$SF_PART_BLOCKS BOOT=$SF_BOOT_PARTUUID WHAT='$WHAT'"
  cat <<'REMOTE'
set -u
P=/dev/nvme0n1p3; S=/sys/block/nvme0n1/nvme0n1p3
ok=1
[ "$(blkid -s PARTUUID -o value $P)" = "$WANT_PARTUUID" ] || { echo "SFL_FAIL PARTUUID"; ok=0; }
[ "$(blkid -s LABEL -o value $P)" = SiliconForest ] || { echo "SFL_FAIL label"; ok=0; }
[ "$(( $(cat $S/start) / 8 ))" = "$WANT_LBA" ] || { echo "SFL_FAIL first block"; ok=0; }
[ "$(( $(cat $S/size) / 8 ))" = "$WANT_BLOCKS" ] || { echo "SFL_FAIL length"; ok=0; }
findmnt -rn -S $P >/dev/null && { echo "SFL_FAIL mounted"; ok=0; }
if [ $ok = 1 ]; then
  echo "SFL_IDENTITY ok"
  e2fsck -fn $P 2>&1 | sed 's/^/SFL_FSCK /'; echo "SFL_FSCK_RC ${PIPESTATUS[0]}"
  sha256sum /tmp/kernel.bin /tmp/boot.bin 2>/dev/null | sed 's/^/SFL_SHA /'
  bash /tmp/sf-bootbin-install.sh "$BOOT" $WHAT 2>&1 | sed 's/^/SFL_INSTALL /'; echo "SFL_INSTALL_RC ${PIPESTATUS[0]}"
  bash /tmp/sf-bootbin-install.sh "$BOOT" --show 2>&1 | grep -E 'sf/' | sed 's/^/SFL_SHOW /'
  asahi-bless --next --set-boot SiliconForest -y 2>&1 | tail -2 | sed 's/^/SFL_BLESS /'
fi
echo SFL_END
(sleep 2; systemctl reboot) > /dev/null 2>&1 &
REMOTE
} | ssh "${LX[@]}" chrisk@10.1.1.211 'sudo -n bash -s' >> "$OUT" 2>&1
echo "linux session rc=$? $(date -u +%T)"
grep -E 'SFL_(IDENTITY|FAIL|FSCK_RC|SHA|INSTALL_RC|SHOW|BLESS|END)' "$OUT" | grep -v -E 'SFL_SHOW (total|d)' | sed -E 's/[0-9a-f]{8}-[0-9a-f-]{27}/<id>/g' | cut -c1-160
grep -q 'SFL_INSTALL_RC 0' "$OUT" || { log "kernel cycle $NAME: install did NOT succeed (see $OUT); M1 reboots into whatever SFBOOT now holds"; }
# 3. back in SiliconForest
sleep 75
for i in $(seq 1 70); do "${SF[@]}" true 2>/dev/null && break; sleep 9; done
for i in $(seq 1 30); do "${SF[@]}" 'pgrep -x chonkstep-darwin-desktop.next > /dev/null || pgrep -x chonkstep-darwin-desktop > /dev/null' && break; sleep 9; done
"${SF[@]}" 'echo SF_UUID $(sysctl -n kern.kernelcacheuuid); echo SF_BOOTTIME $(sysctl -n kern.boottime); sysctl -n kern.bootargs | tr " " "\n" | grep -E "^(sfavd|if_attach_nx)="; pgrep -fl chonkstep-darwin-desktop | cut -c1-60; ls -d /private/var/run/gpu-desktop/inject 2>&1' 2>&1 | tee -a "$OUT" | sed 's/^/  /'
grep -q '^SF_UUID' <(tail -8 "$OUT") && log "M1 back in SiliconForest after kernel cycle $NAME ($(grep '^SF_UUID' "$OUT" | tail -1))"
