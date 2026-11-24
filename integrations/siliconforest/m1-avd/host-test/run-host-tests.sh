#!/bin/bash
# SPDX-License-Identifier: MIT
# Host tests of the SFAppleAVD port (no hardware, no boot):
#   1. the codec, hardware and V4L2 helper files under kernel/linux, the two media headers
#      and v4l2-controls.h are byte-identical to the pinned Linux tree's (omacom/linux-aurora
#      integration/aurora, sources/gpu-plan/linux-aurora): the instruction-stream generators
#      ARE the Linux driver's;
#   2. the driver core and those files build with -Wall under ASan/UBSan against the host
#      stand-in for the decoder;
#   3. every recording given (libsfavd recordings made by FFmpeg's sfavd hwaccel; default:
#      runs/avd/rec/*.pdaj) replays through the core with every decode succeeding, no
#      sanitizer report, no device memory left, and the same instruction stream on a second
#      run; the stream's SHA-256 is printed.
# What this does not prove: that the decoder accepts the streams (the frame hashes of a boot
# do), or that the surroundings feed the generators as V4L2 would for input this never saw.
# Usage: run-host-tests.sh [recording ...]
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
LINUX=$ROOT/sources/gpu-plan/linux-aurora
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
fail=0
same() { # <file here> <path in the Linux tree>
  if git -C "$LINUX" show "HEAD:$2" 2>/dev/null | cmp -s - "$1"; then echo "AVDHOST identical $2"; else echo "AVDHOST DIFFERS $2"; fail=1; fi
}
A=drivers/media/platform/apple/avd
for f in avd.h avd-inst.h avd-regs.h avd-hw.c avd-h264.c avd-hevc.c avd-vp9.c; do same "$HERE/kernel/linux/$f" "$A/$f"; done
same "$HERE/kernel/linux/v4l2-h264.c" drivers/media/v4l2-core/v4l2-h264.c
same "$HERE/kernel/linux/v4l2-vp9.c" drivers/media/v4l2-core/v4l2-vp9.c
same "$HERE/kernel/shim/media/v4l2-h264.h" include/media/v4l2-h264.h
same "$HERE/kernel/shim/media/v4l2-vp9.h" include/media/v4l2-vp9.h
same "$HERE/include/sfavd/v4l2-controls.h" include/uapi/linux/v4l2-controls.h
echo "AVDHOST linux commit $(git -C "$LINUX" rev-parse --short HEAD)"
python3 "$HERE/host-test/test_irq_source.py"
# Resolve explicit recording arguments before the build changes directory.
# Missing explicit inputs are failures, never a vacuous ALL PASS.
records=()
for record in "$@"; do
  [ -f "$record" ] || { echo "AVDHOST FAIL missing recording $record"; exit 1; }
  records+=("$(realpath -- "$record")")
done
set -- "${records[@]}"
CC=${CC:-clang}
cd "$HERE/kernel"
for f in sf_avd_core.c linux/avd-hw.c linux/avd-h264.c linux/avd-hevc.c linux/avd-vp9.c linux/v4l2-h264.c linux/v4l2-vp9.c; do
  $CC -std=gnu11 -c -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Werror -Wno-unused-function \
    -I. -Ishim -Ilinux -o "$W/$(basename "$f" .c).o" "$f"
done
cd "$HERE"
$CC -std=gnu11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Werror -Iinclude -Ikernel \
  -o "$W/avd_host" host-test/avd_host.c host-test/host_os.c "$W"/*.o
echo "AVDHOST built"
[ $# -gt 0 ] || set -- "$ROOT"/runs/avd/rec/*.pdaj
seen=0
for r in "$@"; do
  [ -f "$r" ] || { echo "AVDHOST no recording $r"; continue; }
  seen=$((seen+1))
  n=$(basename "$r" .pdaj)
  if "$W/avd_host" "$r" "$W/$n.1" > "$W/$n.out" 2> "$W/$n.err" && "$W/avd_host" "$r" "$W/$n.2" > /dev/null 2>&1 && cmp -s "$W/$n.1" "$W/$n.2"; then
    echo "AVDHOST PASS $(cut -d: -f2- "$W/$n.out" | sed "s|^|$n:|") words $(grep -c '^vp' "$W/$n.1") sha256 $(sha256sum "$W/$n.1" | cut -c1-16)"
  else
    echo "AVDHOST FAIL $n"; tail -5 "$W/$n.err"; fail=1
  fi
done
[ $seen -gt 0 ] || { echo "AVDHOST BUILT_NO_RECORDINGS: no decode replay was tested"; exit 0; }
[ $fail = 0 ] && echo "AVDHOST ALL PASS" || { echo "AVDHOST FAILED"; exit 1; }
