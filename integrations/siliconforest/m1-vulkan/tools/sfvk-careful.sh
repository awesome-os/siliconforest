#!/bin/bash
# SPDX-License-Identifier: MIT
# One GPU test on the M1 that might take the machine down, run so that the evidence survives:
#   sfvk-careful.sh <name> <command...>
# - takes runs/m1-coord/gui.lock for the whole run (refuses if another agent holds it or core.lock) and writes
#   the announcement line to runs/m1-coord/log.txt first (SFVK_IGNORE_CORE=1: core.lock may be
#   held, by a QEMU rehearsal that stays on the workstation);
# - on the M1: sync (so a crash leaves as little unwritten as possible), then the kernel log is
#   streamed to this workstation once a second while the command runs and for WATCH_AFTER
#   seconds after it (default 110: the GPU scheduler's job timeout is 100 s), each line as it
#   appears, into runs/sfvk/<name>.txt here; if the machine dies the file ends at its last second.
# Environment of the command: the stage's (sfvk.sh run). The command's complete output also stays on the M1 in
# /private/tmp/sfvk-careful-<name>.txt (lines are streamed from it by line number, whole lines only).
set -uo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
name=$1; shift
cmd="$*"
WATCH_AFTER=${WATCH_AFTER:-110}
MAX_RUN=${MAX_RUN:-300}   # the command is killed (SIGKILL) if it still runs after this many seconds
cd "$ROOT"
AGENT=${SFVK_AGENT:-GFX2}
# gui.lock is HELD for the whole run (whoever runs a risky test owns the recovery if it drops)
exec 9>>runs/m1-coord/gui.lock
# SFVK_HAVE_GUI_LOCK=1: the caller holds gui.lock for a whole session of runs
[ -n "${SFVK_SELFTEST:-}${SFVK_HAVE_GUI_LOCK:-}" ] || { echo "waiting for gui.lock (up to 30 min)"; flock -w 1800 9; } || { echo "gui.lock stayed held: not now"; exit 3; }
[ -n "${SFVK_SELFTEST:-}${SFVK_IGNORE_CORE:-}" ] || flock -n runs/m1-coord/core.lock true || { echo "core.lock is held (core install pipeline): not now"; exit 3; }
[ -n "${SFVK_SELFTEST:-}" ] || echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $AGENT risky GPU test over SSH (may take the M1 down; kernel log streamed live): $name: $cmd" >> runs/m1-coord/log.txt
OUT=runs/sfvk/$name.txt
D=/private/var/db/sfvk
# The M1's kernel console: route uart0 to the USB-C debug pins (macvdmtool serial, no reset; it
# does not survive a reboot of the M1) and capture /dev/cu.debug-console on the M5 for the whole
# run; the capture is copied to runs/sfvk/<name>-uart.txt afterwards. SFVK_UART=0 skips it.
M5="ssh -o HostKeyAlias=10.1.1.93 -o BatchMode=yes -o ConnectTimeout=8 chriskearney@${M5_HOST:-10.1.1.55}"
VDM=/Users/chriskearney/darwin-lab-firstboot-007/native-control-src/macvdmtool
if [ "${SFVK_UART:-1}" = 1 ]; then
  $M5 'mkdir -p ~/vk-serial' && scp -q -o HostKeyAlias=10.1.1.93 -o BatchMode=yes "$HERE/m5_uart_capture.py" \
    "chriskearney@${M5_HOST:-10.1.1.55}:vk-serial/" || { echo "M5 not reachable: no capture, not running"; exit 4; }
  BENCH_WHO=$AGENT tools/bench.sh run $M5 "sudo -n $VDM serial" | tail -1
  # one reader only: an older capture still holding the device would take the bytes
  $M5 'for p in $(pgrep -f "[m]5_uart_capture.py"); do kill $p; done; true'
  $M5 "cd ~/vk-serial && rm -f $name.txt && nohup python3 m5_uart_capture.py $name.txt $((MAX_RUN + WATCH_AFTER + 120)) >/dev/null 2>&1 &"
  echo "UART capture running on the M5 (~/vk-serial/$name.txt)"
fi
ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest \
    -o StrictHostKeyChecking=yes -o ConnectTimeout=15 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 \
    "${SFVK_HOST:-root@10.1.1.211}" "
  sync
  n=\$(dmesg | wc -l)
  echo \"CAREFUL start \$(date -u +%T) dmesg_lines=\$n\"
  export VK_ICD_FILENAMES=$D/icd.d/asahi_icd.json DYLD_LIBRARY_PATH=$D/lib PATH=$D/bin:\$PATH
  C=/private/tmp/sfvk-careful-$name.txt
  ( $cmd ) > \$C 2>&1 &
  pid=\$!
  end=0; t=0; r=0; c=0
  while :; do
    r=\$((r+1))
    if [ \$end = 0 ] && [ \$r -ge $MAX_RUN ]; then echo \"CAREFUL command still running after ${MAX_RUN}s: SIGKILL\"; pkill -9 -P \$pid; kill -9 \$pid; fi
    k=\$(wc -l < \$C | tr -d ' ')
    [ \$k -gt \$c ] && sed -n \"\$((c+1)),\${k}p\" \$C | while IFS= read -r x; do echo \"CMD \$x\"; done && c=\$k
    m=\$(dmesg | wc -l)
    [ \$m -gt \$n ] && dmesg | tail -n \$((m-n)) | while IFS= read -r x; do echo \"KLOG \$x\"; done && n=\$m
    if [ \$end = 0 ] && ! kill -0 \$pid 2>/dev/null; then end=1; echo \"CAREFUL command ended \$(date -u +%T); watching ${WATCH_AFTER}s more\"; fi
    [ \$end = 1 ] && t=\$((t+1)) && [ \$t -ge $WATCH_AFTER ] && break
    sleep 1
  done
  echo \"CAREFUL end \$(date -u +%T)\"
" 2>&1 | while IFS= read -r l; do printf '%s %s\n' "$(date -u +%T)" "$l"; done | tee "$OUT"
rc=${PIPESTATUS[0]}
echo "ssh rc=$rc (255 = the connection died)" | tee -a "$OUT"
if [ "${SFVK_UART:-1}" = 1 ]; then
  $M5 "cat ~/vk-serial/$name.txt" > "runs/sfvk/$name-uart.txt" 2>/dev/null
  echo "UART capture so far: $(wc -c < runs/sfvk/$name-uart.txt) bytes in runs/sfvk/$name-uart.txt (it keeps running on the M5)" | tee -a "$OUT"
fi
[ -n "${SFVK_SELFTEST:-}" ] || echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) $AGENT risky GPU test $name finished: ssh rc=$rc; $(grep -c '^.* KLOG' "$OUT") kernel lines; last: $(tail -2 "$OUT" | head -1 | cut -c1-120)" >> runs/m1-coord/log.txt
