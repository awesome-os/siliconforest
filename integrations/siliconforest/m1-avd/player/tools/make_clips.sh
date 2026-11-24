#!/bin/bash
# SPDX-License-Identifier: MIT
# Test clips for the video decoder work, cut from Big Buck Bunny (c) Blender Foundation,
# CC BY 3.0, https://peach.blender.org. Source file: bbb_sunflower_1080p_30fps_normal.mp4
# (1920x1080, 30 fps, H.264 High + MP3 + AC-3), from the zip of the same name in
# demo/movies/BBB/ of the Blender download tree. Output goes to runs/avd/clips (never committed).
# The Linux-native ffmpeg here may use libx264/libx265/libvpx: it runs on the workstation only.
# Usage: make_clips.sh <ffmpeg bin dir> <source mp4> <output dir>
set -euo pipefail
FF=$1; SRC=$2; OUT=$3
mkdir -p "$OUT"
ff() { taskset -c 0-3 "$FF/ffmpeg" -v error -nostdin -y "$@"; }
SS=125        # a section with motion and a scene change
X264="-c:v libx264 -preset medium -threads 4 -pix_fmt yuv420p -g 60 -sc_threshold 40"
AAC="-c:a aac -b:a 128k -ac 2 -ar 48000"
# (a) 10 s 1080p H.264 High + AAC
ff -ss $SS -t 10 -i "$SRC" -map 0:v:0 -map 0:a:0 $X264 -profile:v high -level 4.0 -crf 24 -maxrate 6M -bufsize 12M $AAC -movflags +faststart "$OUT/bbb-1080p-h264-10s.mp4"
# (b) 5 s 640x360 H.264 High + AAC
ff -ss $SS -t 5 -i "$SRC" -map 0:v:0 -map 0:a:0 -vf scale=640:360:flags=lanczos $X264 -profile:v high -level 3.0 -crf 23 $AAC -movflags +faststart "$OUT/bbb-360p-h264-5s.mp4"
# (c) the same 10 s as HEVC Main (hvc1) + AAC, and VP9 profile 0 + Opus in WebM
ff -ss $SS -t 10 -i "$SRC" -map 0:v:0 -map 0:a:0 -c:v libx265 -preset medium -pix_fmt yuv420p -profile:v main -crf 26 -x265-params "pools=4:log-level=error:keyint=60" -tag:v hvc1 $AAC -movflags +faststart "$OUT/bbb-1080p-hevc-10s.mp4"
ff -ss $SS -t 10 -i "$SRC" -map 0:v:0 -map 0:a:0 -c:v libvpx-vp9 -profile:v 0 -pix_fmt yuv420p -b:v 0 -crf 34 -deadline good -cpu-used 3 -row-mt 1 -threads 4 -g 60 -c:a libopus -b:a 128k -ac 2 "$OUT/bbb-1080p-vp9-10s.webm"
# (d) 3 s 1080p variants, video only, each also as a raw Annex-B stream
variant() { # name, x264 arguments
  local n=$1; shift
  ff -ss $SS -t 3 -i "$SRC" -map 0:v:0 -an $X264 -level 4.0 -crf 24 "$@" -movflags +faststart "$OUT/$n.mp4"
  ff -i "$OUT/$n.mp4" -map 0:v:0 -c:v copy -bsf:v h264_mp4toannexb -f h264 "$OUT/$n.h264"
}
variant bbb-1080p-h264-cbp-nob-3s    -profile:v baseline -bf 0                    # Constrained Baseline: CAVLC, no B
variant bbb-1080p-h264-main-nob-3s   -profile:v main -bf 0 -coder 1               # Main: CABAC, no B
variant bbb-1080p-h264-main-b-3s     -profile:v main -bf 3 -b-pyramid normal -coder 1   # Main: CABAC, B-frames (reference B)
ls -l "$OUT"
