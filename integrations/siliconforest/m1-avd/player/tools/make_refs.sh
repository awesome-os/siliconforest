#!/bin/bash
# SPDX-License-Identifier: MIT
# Reference hashes next to each clip, from the Linux-native software FFmpeg of the pinned nixpkgs:
#   <clip>.framemd5     ffmpeg -i <clip> -map 0:v:0 -fps_mode passthrough -pix_fmt nv12 -f framemd5
#   <clip>.planes.md5   tools/planes_md5.py (per frame: md5 of Y, md5 of interleaved UV)
# Usage: make_refs.sh <ffmpeg bin dir> <clip>...
set -euo pipefail
FF=$1; shift
HERE=$(cd "$(dirname "$0")" && pwd)
for c in "$@"; do
  taskset -c 0-3 "$FF/ffmpeg" -v error -nostdin -threads 2 -i "$c" -map 0:v:0 -an -sn -fps_mode passthrough -pix_fmt nv12 -f framemd5 - > "$c.framemd5"
  taskset -c 0-3 python3 -I "$HERE/planes_md5.py" --ffmpeg "$FF" "$c" "$c.planes.md5"
  printf '%s: %s frames\n' "$c" "$(grep -vc '^#' "$c.framemd5")"
done
