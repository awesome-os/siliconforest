#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Generate only our own synthetic video/audio; keep binary fixtures under runs/.
set -euo pipefail
fixture_dir=${1:?usage: generate-player-fixture.sh NEW_DIRECTORY}
if [[ -e "$fixture_dir" ]]; then echo "Use a fresh output directory" >&2; exit 1; fi
mkdir -p "$fixture_dir"
ffmpeg -hide_banner -nostdin -f lavfi -i testsrc2=size=320x180:rate=30 \
 -f lavfi -i sine=frequency=997:sample_rate=48000 -t 5 -c:v libx264 \
 -preset ultrafast -bf 0 -pix_fmt yuv420p -c:a aac -b:a 96k -ac 2 \
 -movflags +faststart "$fixture_dir/movie.mov" > "$fixture_dir/generate.log" 2>&1
ffmpeg -hide_banner -nostdin -i "$fixture_dir/movie.mov" -an -pix_fmt nv12 \
 -f framemd5 "$fixture_dir/movie.nv12.framemd5" > "$fixture_dir/hash.log" 2>&1
ffprobe -v error -show_entries format=duration:stream=codec_type,codec_name,nb_frames,duration \
 -of json "$fixture_dir/movie.mov" > "$fixture_dir/fixture.json"
