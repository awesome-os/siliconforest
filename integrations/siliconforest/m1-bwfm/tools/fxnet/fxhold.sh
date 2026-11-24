#!/bin/sh
# SPDX-License-Identifier: MIT
# FXNET: one gui.lock hold running fxnav.py in a window on the M1. args: TAG MOZLOG(0/1) [EXTRAENV] ; env FXPREFS/FXSITES pass through
TAG=$1; LOGF=$2; EXTRA=${3:-}
PB=$(printf %s "${FXPREFS:-}" | base64 -w0)
D=$(cd "$(dirname "$0")" && pwd)
L=/home/chrisk/src/darwin-lab/runs/m1-coord
O=${FXNET_OUT:-/home/chrisk/src/darwin-lab/runs/fxnet}; mkdir -p $O
SSH="ssh -o UserKnownHostsFile=$HOME/.ssh/known_hosts_m1_siliconforest -o HostKeyAlias=m1-siliconforest -o BatchMode=yes root@10.1.1.211"
flock -w 600 $L/gui.lock sh -c "
  echo \"\$(date -u +%FT%TZ) FXNET gui.lock: own Firefox window (-no-remote, /private/var/db/sffxnet/p1, MOZ_CRASHREPORTER_DISABLE=1), Marionette-only navigation of 10 sites ($TAG), no seat input, cap 5 min\" >> $L/log.txt
  timeout 300 $SSH \"mkdir -p /private/var/db/sffxnet && FXPREFS_B64=$PB FXSITES=${FXSITES:-} /opt/homebrew/bin/python3 -I - $TAG window $LOGF '$EXTRA'\" < $D/fxnav.py > $O/fx-$TAG.txt 2>&1
  rc=\$?
  echo \"\$(date -u +%FT%TZ) FXNET gui.lock released ($TAG rc=\$rc)\" >> $L/log.txt
"
cut -c1-360 $O/fx-$TAG.txt
