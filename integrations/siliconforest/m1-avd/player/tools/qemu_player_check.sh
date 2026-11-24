#!/bin/bash
# SPDX-License-Identifier: MIT
# sfvideo on the Chonkstep desktop in QEMU (arm64 `virt`, TCG): the qemu-dev stream's image,
# session script and runner, with sfvideo, clips and a hook that plays them once the desktop
# is up. Nothing of that stream is edited; its session template is instantiated here the way
# tools/qemu_desktop.sh does it.
#
# Usage (from the lab root):
#   qemu_player_check.sh <name> <sfvideo output path> [--scale S] [--audio standin|none] [--seconds N]
#                        [--settle N] -- '<sfvideo arguments>'...
# Clip paths in the arguments are host paths under runs/avd/clips; they are staged as
# /private/var/avd/clips/<basename> and rewritten.
# Output: runs/avd/qemu/<name>/{serial.log,shots/*.png,result.json,stats.txt}
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd); cd "$ROOT"
HERE=integrations/siliconforest/m1-avd/player; Q=integrations/siliconforest/qemu-dev
NAME=$1; PDV=$2; shift 2
SCALE=; AUDIO=standin; SECS=240; SETTLE=10; COMPOSITOR=; RENDER_ENV=
while [ $# -gt 0 ] && [ "$1" != -- ]; do
  case "$1" in
    --scale) SCALE=$2; shift;; --audio) AUDIO=$2; shift;; --seconds) SECS=$2; shift;; --settle) SETTLE=$2; shift;;
    --compositor) COMPOSITOR=$2; shift;;
    *) echo "unknown option $1" >&2; exit 2;;
  esac; shift
done
[ "${1:-}" = -- ] && shift
OUT=runs/avd/qemu/$NAME; mkdir -p "$OUT"; rm -rf "$OUT/shots"
IMG=runs/qemu-dev/avd-$NAME/disk.img; mkdir -p "$(dirname "$IMG")"   # qd_image.py writes run images only there
BIN=${COMPOSITOR:-runs/m1-desktop-010/wm-darwin-scale-desktop-build-007/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop}
TERMINAL=runs/m1-desktop-010/terminal-application-build-008/target/aarch64-apple-darwin/release/chonk-terminal-darwin
ARGS=(--file "$PDV/usr/bin/sfvideo=/usr/local/bin/sfvideo" --data "$HERE/guest/avd-play-hook.sh=/usr/local/bin/avd-play-hook")
: > "$OUT/play.list"
for line in "$@"; do
  out=
  for word in $line; do
    if [ -f "$word" ]; then
      ARGS+=(--data "$word=/private/var/avd/clips/$(basename "$word")"); word=/private/var/avd/clips/$(basename "$word")
    fi
    out="$out $word"
  done
  echo "${out# }" >> "$OUT/play.list"
done
printf 'AVD_AUDIO_STANDIN=%s\nAVD_SETTLE=%s\n' "$([ "$AUDIO" = standin ] && echo 1 || echo 0)" "$SETTLE" > "$OUT/play.env"
ARGS+=(--data "$OUT/play.list=/private/var/avd/play.list" --data "$OUT/play.env=/private/var/avd/play.env")
# the session: by hand (no autopilot), one terminal, the compositor's CPU renderer; coreaudiod is
# started by the hook (with the stand-in device) instead of the session
sed -e "s/@SECONDS@/$SECS/g" -e "s/@AUTOPILOT@/0/g" -e "s/@SCALE@/$SCALE/g" -e "s/@THEME@//g" -e "s/@ONLY@//g" \
    -e "s/@STATS@/3600/g" -e "s/@SOUND@/0/g" -e "s/@RENDERER@/cpu/g" -e "s/@TERMINALS@/1/g" -e "s/@FETCH@/0/g" -e "s/@PERSIST@/0/g" \
    -e "s|@ENV@|. /usr/local/bin/avd-play-hook;|g" "$Q/guest/qemu-desktop-session.in" > "$OUT/qemu-desktop-session"
python3 -B "$Q/qd_image.py" --output "$IMG" "${ARGS[@]}" \
  --file "$BIN=/usr/bin/chonkstep-darwin-desktop" --file "$TERMINAL=/usr/bin/chonk-terminal-darwin" \
  --file "$OUT/qemu-desktop-session=/usr/bin/qemu-desktop-session" \
  --file "$Q/guest/org.siliconforest.qemu-desktop.plist=/System/Library/LaunchDaemons/org.siliconforest.qemu-desktop.plist" > "$OUT/image.json"
rc=0
taskset -c 0-3 python3 -B "$Q/qd_run.py" --image "$IMG" --out "$OUT" --input-seen QDDESK_INPUT \
  --end 'AVDPLAY_DONE|QDDESK_END' --passed 'AVDPLAY_DONE' --shot-every "${AVD_SHOT_EVERY:-2}" --timeout $((SECS + 300)) || rc=$?
rm -rf "$(dirname "$IMG")"
grep -a 'AVDPLAY_\|WMSCALE\|QDDESK_DESKTOP' "$OUT/serial.log" | tr -d '\r' | cut -c1-1200 > "$OUT/stats.txt" || true
cat "$OUT/stats.txt"
exit $rc
