#!/bin/bash
# SPDX-License-Identifier: MIT
# The sources of SFAppleNVMeRW (the write-fenced build) are the read-only driver's (../kernel,
# never edited for this) with ../kernel-rw/rw.patch applied, plus ../kernel-rw's own files.
#   rw_src.sh make <dir>      writes them into <dir> (what build-kext-rw.nix compiles)
#   rw_src.sh refresh <dir>   regenerates rw.patch from an edited <dir>
set -euo pipefail
M=$(cd "$(dirname "$0")/.." && pwd); CMD=${1:?make|refresh}; D=${2:?directory}
PATCHED="sf_ans.h sf_ans.cpp SFAppleNVMe.cpp"
case $CMD in
make)
  mkdir -p "$D"; cp "$M"/kernel/*.c "$M"/kernel/*.cpp "$M"/kernel/*.h "$D/"
  cp "$M"/kernel-rw/CMakeLists.txt "$M"/kernel-rw/Info.plist "$M"/kernel-rw/sf_nvme_wring.c "$M"/kernel-rw/sf_nvme_wring.h "$D/"
  patch -s -p1 -d "$D" < "$M/kernel-rw/rw.patch" ;;
refresh)
  : > "$M/kernel-rw/rw.patch.new"
  for f in $PATCHED; do diff -u --label "a/$f" --label "b/$f" "$M/kernel/$f" "$D/$f" >> "$M/kernel-rw/rw.patch.new" || true; done
  mv "$M/kernel-rw/rw.patch.new" "$M/kernel-rw/rw.patch"; wc -l "$M/kernel-rw/rw.patch" ;;
*) echo "make|refresh" >&2; exit 2 ;;
esac
