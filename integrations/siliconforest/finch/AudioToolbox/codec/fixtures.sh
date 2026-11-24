#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
out=${1:?new output directory required}
mkdir "$out"
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'aevalsrc=0.04*sin(2*PI*440*t)|0.03*sin(2*PI*880*t):s=48000:d=0.25' -c:a pcm_s16le "$out/tone.wav"
ffmpeg -nostdin -hide_banner -loglevel error -i "$out/tone.wav" -c:a aac -b:a 128k "$out/tone-aac.m4a"
ffmpeg -nostdin -hide_banner -loglevel error -i "$out/tone.wav" -c:a alac "$out/tone-alac.m4a"
ffmpeg -nostdin -hide_banner -loglevel error -i "$out/tone.wav" -c:a libmp3lame -b:a 128k "$out/tone.mp3"
for file in "$out"/tone*; do
  ffprobe -v error -show_format -show_streams -show_packets -of json "$file" > "$file.ffprobe.json"
  ffmpeg -nostdin -hide_banner -loglevel error -i "$file" -c:a pcm_s16le -f s16le "$file.s16le"
done
