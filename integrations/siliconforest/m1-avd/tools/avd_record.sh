#!/bin/bash
# SPDX-License-Identifier: MIT
# Makes, on the workstation, what a replay boot needs for one clip: runs the host FFmpeg
# with the sfavd hwaccel against libsfavd's recording backend and writes
#   runs/avd/rec/<name>.pdaj    the driver calls of the whole clip (replayed by sfavd-replay)
#   runs/avd/rec/<name>.order   per output frame, the number of its picture in decode order
# The host FFmpeg is the patched source built natively in runs/avd/host/build.
# Usage: avd_record.sh <clip> [<name>]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
CLIP=${1:?clip}; NAME=${2:-$(basename "${CLIP%.*}")}
FF=$ROOT/runs/avd/host/build/ffmpeg
R=$ROOT/runs/avd/rec; mkdir -p "$R"
SIZE=$(/usr/bin/ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$CLIP" | head -1)
W=${SIZE%x*}; H=${SIZE#*x}
SFAVD_DEVICE=record:$R/$NAME.pdaj "$FF" -hide_banner -v error -threads 1 -hwaccel sfavd -i "$CLIP" -map 0:v:0 \
  -fps_mode passthrough -pix_fmt nv12 -f rawvideo - 2> "$R/$NAME.record.log" |
  python3 -I -c '
import struct, sys
w, h = int(sys.argv[1]), int(sys.argv[2])
size = w * h + 2 * ((w + 1) // 2) * ((h + 1) // 2)
while True:
    frame = sys.stdin.buffer.read(size)
    if len(frame) < size:
        break
    print(struct.unpack_from("<Q", frame)[0])
' "$W" "$H" > "$R/$NAME.order"
echo "$NAME: $(wc -l < "$R/$NAME.order") output frames, $(stat -c %s "$R/$NAME.pdaj") bytes$(grep -c . "$R/$NAME.record.log" | sed 's/^0$//; s/^\(.\+\)$/, \1 log lines/')"
