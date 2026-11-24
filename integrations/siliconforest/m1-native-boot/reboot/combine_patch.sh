#!/bin/bash
# SPDX-License-Identifier: MIT
# The launcher patch (PCIE_LAB_PATCH) for a boot whose guest restarts the M1 itself:
# ../launcher-native.patch as it is now, then ./launcher-self-reboot.diff on top, written as
# one -p1 patch against the launcher tree. Made per boot because launcher-native.patch moves.
# Usage: combine_patch.sh <output patch> [launcher tree]
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd)
OUT=${1:?output patch}; LAB=${2:-$ROOT/runs/m1-desktop-010/pcie-m2-host-001/lab}
[ -d "$LAB" ] || { echo "no launcher tree at $LAB" >&2; exit 2; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
FILES=$(sed -n 's#^+++ b/##p' "$HERE/../launcher-native.patch" "$HERE/launcher-self-reboot.diff" | cut -f1 | sort -u)
for t in a b; do
  mkdir "$T/$t"
  for f in $FILES; do mkdir -p "$T/$t/$(dirname "$f")"; cp "$LAB/$f" "$T/$t/$f"; done
done
patch -s -d "$T/b" -p1 < "$HERE/../launcher-native.patch" || { echo "launcher-native.patch does not apply" >&2; exit 2; }
patch -s -d "$T/b" -p1 < "$HERE/launcher-self-reboot.diff" || { echo "launcher-self-reboot.diff does not apply on launcher-native.patch" >&2; exit 2; }
for f in $FILES; do (cd "$T" && diff -u "a/$f" "b/$f") || [ $? = 1 ]; done > "$OUT"
[ -s "$OUT" ]
