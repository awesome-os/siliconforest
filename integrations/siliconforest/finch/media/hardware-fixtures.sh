#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Original synthetic clips; never downloads media. Fresh destination required.
set -euo pipefail
out=${1:?usage: hardware-fixtures.sh NEW_DIRECTORY}
mkdir "$out"
for codec in h264 hevc; do
  if [ "$codec" = h264 ]; then
    args=(-c:v libx264 -preset fast -threads 2)
  else
    args=(-c:v libx265 -preset fast -threads 2 -x265-params pools=2:frame-threads=2:log-level=error -tag:v hvc1)
  fi
  ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i testsrc2=size=1280x720:rate=30:duration=10 \
    "${args[@]}" -pix_fmt yuv420p -g 60 -bf 2 -an -movflags +faststart "$out/$codec.mp4"
  ffmpeg -nostdin -hide_banner -loglevel error -i "$out/$codec.mp4" -pix_fmt nv12 -f framemd5 "$out/$codec.nv12.framemd5"
done
