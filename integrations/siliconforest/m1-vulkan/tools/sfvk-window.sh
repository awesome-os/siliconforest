#!/bin/bash
# SPDX-License-Identifier: MIT
# One windowed Vulkan test on the M1's desktop, run from the workstation INSIDE gui.lock:
#   flock runs/m1-coord/gui.lock integrations/siliconforest/m1-vulkan/tools/sfvk-window.sh <name> <client command...>
# It starts the client (from /private/var/db/sfvk, the stage's environment) on the running
# compositor's socket, takes one compositor capture after CAPTURE_AT seconds, waits for the
# client to end (it must end by itself: vkcube --c <frames>), and copies back to runs/sfvk/<name>/:
# the client's output, the capture, and the compositor's log lines written meanwhile (WMDMABUF*).
# No compositor or session restart; the window is gone when the client ends.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
name=$1; shift
OUT=$ROOT/runs/sfvk/$name; mkdir -p "$OUT"
CAPTURE_AT=${CAPTURE_AT:-4}
SSH=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest
     -o StrictHostKeyChecking=yes -o ConnectTimeout=15 "${SFVK_HOST:-root@10.1.1.211}")
D=/private/var/db/sfvk
T=/private/tmp/sfvk-$name
"${SSH[@]}" "rm -rf $T && mkdir -p $T && L=/var/log/desktop-session.log && wc -c < \$L > $T/log-start &&
  export XDG_RUNTIME_DIR=/private/var/run/gpu-desktop WAYLAND_DISPLAY=wayland-0
  export VK_ICD_FILENAMES=$D/icd.d/asahi_icd.json DYLD_LIBRARY_PATH=$D/lib PATH=$D/bin:\$PATH
  start=\$(date +%s)
  ( $* ) > $T/client.txt 2>&1 & pid=\$!
  sleep $CAPTURE_AT
  if kill -0 \$pid 2>/dev/null; then chonkstep-darwin-desktop --screencapture $T/capture.png > $T/capture.txt 2>&1 || true; fi
  wait \$pid; rc=\$?; end=\$(date +%s)
  echo \"client_rc=\$rc seconds=\$((end-start))\" > $T/result.txt
  tail -c +\$((\$(cat $T/log-start)+1)) \$L > $T/compositor.txt || true
  cat $T/result.txt"
"${SSH[@]}" "cd $T && tar cf - ." | tar -C "$OUT" -xf -
echo "results in $OUT"; ls "$OUT"
