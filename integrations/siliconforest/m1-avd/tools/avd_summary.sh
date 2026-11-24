#!/bin/bash
# SPDX-License-Identifier: MIT
# What one avd_boot.sh boot showed: the driver's lines, and per staged recording the frames
# that match the workstation's software decode (<clip>.framemd5 next to the clip in
# runs/avd/clips or runs/avd/t, made with `ffmpeg -i <clip> -pix_fmt nv12 -f framemd5`).
# Usage: avd_summary.sh <boot name>
set -uo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
N=${1:?name}
D=runs/m1-desktop-010/pcie-enum-attempt-$N
C=$D/guest/console.bin
[ -f "$C" ] || { echo "no console at $C"; exit 1; }
T=$(mktemp); strings -n 4 "$C" > "$T"
grep -a '"status"' "$D"/result.json 2>/dev/null | head -2
grep -a 'SFAppleAVD:\|SFAvdDart:\|AVD_STATE\|AVD_LS\|SFAVD_INFO\|SFAVD_CONTEXT\|SFAVD_SUMMARY\|SFAVD_.*FAILED\|AVD_STATS\|AVD_CLIP_END\|panic\|SFVIDEO' "$T" | cut -c1-330 | head -${AVD_SUMMARY_LINES:-70}
echo "SFAVD_FAIL lines: $(grep -ac 'SFAVD_FAIL' "$T")"
rc=0
for name in $(sed -n 's/^AVD_CLIP_BEGIN //p' "$T"); do
  ref=
  for d in runs/avd/clips runs/avd/t; do
    for e in mp4 h264 webm mkv h265 ivf; do [ -f "$d/$name.$e.framemd5" ] && ref=$d/$name.$e.framemd5; done
  done
  awk -v n="$name" '$0=="AVD_CLIP_BEGIN " n {on=1; next} /^AVD_CLIP_END/ {on=0} on' "$T" > "$T.clip"
  order=runs/avd/rec/$name.order; [ -s "$order" ] || order=
  [ -n "$ref" ] || { r=$(find runs/avd/clips/jvt -name "$name.*.framemd5" 2>/dev/null | head -1); [ -n "$r" ] && ref=$r; }
  if [ -n "$ref" ]; then python3 "$HERE/tools/avd_compare.py" "$T.clip" "$ref" "$name" $order || rc=1
  else echo "$name: no reference framemd5; $(grep -c SFAVD_FRAME "$T.clip") pictures decoded"; fi
done
rm -f "$T" "$T.clip"
exit $rc
