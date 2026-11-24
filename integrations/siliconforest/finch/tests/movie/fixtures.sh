#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
out=${1:?usage: fixtures.sh NEW_DIRECTORY}
mkdir "$out"
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i testsrc2=size=320x192:rate=30:duration=30 \
  -f lavfi -i 'aevalsrc=0.04*sin(2*PI*440*t)|0.04*sin(2*PI*880*t):s=48000:d=30' \
  -c:v libx264 -preset fast -threads 2 -pix_fmt yuv420p -g 60 -bf 2 -c:a aac -b:a 96k \
  -movflags +faststart -shortest "$out/movie.mov"
ffmpeg -nostdin -hide_banner -loglevel error -i "$out/movie.mov" -map 0:v:0 -pix_fmt nv12 -f framemd5 "$out/movie.nv12.framemd5"
ffprobe -v error -show_streams -show_format -of json "$out/movie.mov" > "$out/movie.ffprobe.json"
