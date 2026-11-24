#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Synthetic media only. Generate into a fresh directory outside the source tree.
set -euo pipefail
out=${1:?usage: generate-fixtures.sh NEW_DIRECTORY}
mkdir "$out"
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=64x48:rate=10:duration=1' -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=1' -c:v libx264 -pix_fmt yuv420p -g 10 -bf 0 -c:a aac -movflags +faststart -shortest "$out/test-av.mp4"
ffmpeg -nostdin -hide_banner -loglevel error -i "$out/test-av.mp4" -c:v copy -c:a pcm_s16le "$out/test-av.mov"
for ext in mp4 mov; do
  ffprobe -v error -show_format -show_streams -show_packets -of json "$out/test-av.$ext" > "$out/test-av.$ext.ffprobe.json"
done
ffmpeg -nostdin -hide_banner -loglevel error -i "$out/test-av.mp4" -map 0:v:0 -pix_fmt nv12 -f framemd5 "$out/test-av.nv12.framemd5"
