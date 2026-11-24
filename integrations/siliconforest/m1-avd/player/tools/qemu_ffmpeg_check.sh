#!/bin/bash
# SPDX-License-Identifier: MIT
# Proof of the cross-built FFmpeg: boot the qemu-dev stream's image (arm64 `virt`, TCG) with
# usr/bin/ffmpeg of build-ffmpeg.nix and the given clips, decode each in the guest with
#   ffmpeg -i <clip> -map 0:v:0 -fps_mode passthrough -pix_fmt nv12 -f framemd5
# and compare every line with <clip>.framemd5 (the Linux-native FFmpeg's, tools/make_refs.sh).
# Usage: qemu_ffmpeg_check.sh <name> <ffmpeg output path> <clip>...     (run from the lab root)
# Output: runs/avd/qemu/<name>/{serial.log,compare.txt}; exit 0 when every frame of every clip matches.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../../.." && pwd); cd "$ROOT"
HERE=integrations/siliconforest/m1-avd/player; Q=integrations/siliconforest/qemu-dev
NAME=$1; FF=$2; shift 2
OUT=runs/avd/qemu/$NAME; mkdir -p "$OUT"
IMG=runs/qemu-dev/avd-$NAME/disk.img; mkdir -p "$(dirname "$IMG")"   # qd_image.py writes run images only there
ARGS=(--file "$FF/usr/bin/ffmpeg=/usr/local/bin/ffmpeg" --file "$FF/usr/bin/ffprobe=/usr/local/bin/ffprobe"
      --file "$HERE/guest/avd-ffcheck.sh=/usr/local/bin/avd-ffcheck")
for c in "$@"; do ARGS+=(--data "$c=/private/var/avd/clips/$(basename "$c")"); done
python3 -B "$Q/qd_image.py" --output "$IMG" "${ARGS[@]}" > "$OUT/image.json"
rc=0
taskset -c 0-3 python3 -B "$Q/qd_run.py" --image "$IMG" --out "$OUT" --command '/usr/local/bin/avd-ffcheck' \
  --begin 'AVDFF_BEGIN' --up 'AVDFF_VERSION' --end 'AVDFF_END' --passed 'AVDFF_END' --shot-every 0 \
  --timeout "${AVD_TIMEOUT:-1500}" || rc=$?
rm -rf "$(dirname "$IMG")"
python3 -I "$HERE/tools/compare_framemd5.py" "$OUT/serial.log" "$@" | tee "$OUT/compare.txt"
exit "${PIPESTATUS[0]}"
