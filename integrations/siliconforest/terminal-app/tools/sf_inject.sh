#!/bin/bash
# SPDX-License-Identifier: MIT
# Requested input and screen captures on a running SiliconForest desktop, through the
# compositor's inject module (../compositor/wm-darwin-inject.patch; the session turns it on
# where root made /private/var/db/desktop-inject: ../../m1-storage/system/sf-session).
#
#   sf_inject.sh <item> ...      items as sf_keys.py takes them:
#       text:<string>  key:<name>  chord:<mods>+<key>  wait:<ms>  capture:<name>
#       click:<x>,<y>  rclick:<x>,<y>  move:<x>,<y>  down:<x>,<y>  up:   (panel pixels)
#   sf_inject.sh --raw '<request lines separated by ;>'
# A request starts with a tap of Shift (the display may be asleep: a key wakes it and is not
# delivered). Captures are fetched to runs/sf-terminal/shots/<name>.png: the whole screen as
# the compositor rendered it. Everything injected is synthetic and the compositor says so
# (WMINJECT ... input=synthetic-inject); it is not evidence of a physical keyboard or trackpad.
# SF_INJECT_FETCH=0: leave the captures on the target (in the inject directory).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../../../.." && pwd); W=$ROOT/runs/sf-terminal
SF_HOST=${SF_HOST:-10.1.1.211}; SF_KNOWN=${SF_KNOWN:-$HOME/.ssh/known_hosts_m1_siliconforest}
SSH=(ssh -o UserKnownHostsFile="$SF_KNOWN" -o GlobalKnownHostsFile=/dev/null -o StrictHostKeyChecking=yes -o HostKeyAlias=m1-siliconforest
     -o PasswordAuthentication=no -o BatchMode=yes -o ConnectTimeout=6 "root@$SF_HOST")
if [ "${1:-}" = --raw ]; then REQ=$2; else REQ="key e1 down;key e1 up;wait 400;$(python3 -B "$(dirname "$(readlink -f "$0")")/sf_keys.py" "$@")"; fi
REQ=$(printf '%s\n' "$REQ" | tr ';' '\n')
printf '%s\n' "$REQ" | "${SSH[@]}" 'D=/private/var/run/gpu-desktop/inject; [ -d $D ] || { echo "no inject directory: the session has no /private/var/db/desktop-inject, or its compositor has no inject module"; exit 1; }
  rm -f $D/done; cat > $D/request.part && mv $D/request.part $D/request; i=0; while [ ! -e $D/done ] && [ $i -lt 900 ]; do sleep 0.1; i=$((i+1)); done; cat $D/done 2>/dev/null || echo "no done file"'
[ "${SF_INJECT_FETCH:-1}" = 0 ] && exit 0
mkdir -p "$W/shots"
for n in $(printf '%s\n' "$REQ" | awk '$1=="capture" {print $2}'); do
  "${SSH[@]}" "cat /private/var/run/gpu-desktop/inject/$n.ppm" > "$W/shots/$n.ppm" < /dev/null
  python3 -c "from PIL import Image; import sys; im = Image.open(sys.argv[1]); im.save(sys.argv[2]); print(sys.argv[2], im.size)" "$W/shots/$n.ppm" "$W/shots/$n.png" && rm -f "$W/shots/$n.ppm"
done
