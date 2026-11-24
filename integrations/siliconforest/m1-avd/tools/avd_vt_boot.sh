#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot (no desktop) that decodes clips through the public VideoToolbox API on the M1:
# stages VideoToolbox.framework of ../build-videotoolbox.nix with CoreMedia, IOSurface and a
# CoreVideo that is the tree's CVPixelBuffer.c alone (the full CoreVideo needs the Cocoa
# root's OpenGL, Mesa and LLVM), vt-decode-test and the clips, and runs the test once on the
# hardware decoder and once in software for each clip. The frame hashes are compared with
# the workstation's software decode by tools/avd_summary.sh.
# Usage: avd_vt_boot.sh <name, prefix av> <clip> ...     (clips from runs/avd/clips or runs/avd/t)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; shift
V=$(integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py m1-avd build-videotoolbox.nix)
[ -x "$V/usr/bin/vt-decode-test" ] || { echo "VideoToolbox build failed" >&2; exit 2; }
F="$V/usr/bin/vt-decode-test=/usr/bin/vt-decode-test"
for f in VideoToolbox CoreMedia CoreVideo IOSurface; do
  p=System/Library/Frameworks/$f.framework/Versions/A/$f
  F="$F;$V/test-root/$p=/$p"
done
E=
for c in "$@"; do
  f=runs/avd/clips/$c; [ -f "$f" ] || f=runs/avd/t/$c
  [ -f "$f" ] || { echo "no clip $c" >&2; exit 2; }
  F="$F;$ROOT/$f=/private/var/avd/$c"
  b=${c%.*}
  # the two runs of a clip are reported under two names; both compare with <clip>.framemd5
  E="$E
printf 'AVD_CLIP_BEGIN %s\n' '$b'; VT_AVD_TIMING=1 vt-decode-test /private/var/avd/$c 2>&1; printf 'AVD_CLIP_END %s rc=%s\n' '$b' \"\$?\"
printf 'AVD_STATS vt %s\n' \"\$(get stats)\"
printf 'AVD_VTSW_BEGIN %s\n' '$b'; VT_AVD_TIMING=1 vt-decode-test --software /private/var/avd/$c 2>&1 | sed 's/^SFAVD_FRAME/VTSW_FRAME/'; printf 'AVD_VTSW_END %s\n' '$b'"
done
AVD_FILES="$F" AVD_EXTRA="$E" AVD_RUNTIME=${AVD_RUNTIME:-240} "$HERE/tools/avd_boot.sh" "$N"
C=runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin
T=$(mktemp); strings -n 4 "$C" > "$T"
grep -a 'VT_\|dyld\|Symbol not found\|Library not loaded' "$T" | cut -c1-300 | head -40
# the software runs, compared the same way
for b in $(sed -n 's/^AVD_VTSW_BEGIN //p' "$T"); do
  ref=; for d in runs/avd/clips runs/avd/t; do for e in mp4 h264 webm mkv; do [ -f "$d/$b.$e.framemd5" ] && ref=$d/$b.$e.framemd5; done; done
  awk -v n="$b" '$0=="AVD_VTSW_BEGIN " n {on=1; next} /^AVD_VTSW_END/ {on=0} on' "$T" | sed 's/^VTSW_FRAME/SFAVD_FRAME/' > "$T.clip"
  [ -n "$ref" ] && python3 "$HERE/tools/avd_compare.py" "$T.clip" "$ref" "$b (VideoToolbox, software)" || true
done
rm -f "$T" "$T.clip"
