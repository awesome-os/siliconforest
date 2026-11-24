#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the Chonkstep dock (chonk-dock, a client of the compositor) for arm64 Darwin, the way
# ../../chonkstep-darwin/tools/build_desktop_binaries.sh builds the terminal: same link inputs
# (../../chonkstep-darwin/link-inputs.nix through darwin-linker-inputs.py), into
# runs/m1-desktop-010/<name>/; the source is copied first (nothing is written into it).
#   build_dock.sh <name> <source-dir with the desktop patches>
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)/chonkstep-darwin; LAB=$(cd "$HERE/../../.." && pwd)
export PATH=$HOME/.nix-profile/bin:$PATH
NAME=${1:?build name}; SRC=${2:?source dir}; OUT=$LAB/runs/m1-desktop-010/$NAME
BIN=$OUT/target/aarch64-apple-darwin/release/chonk-dock
[ -e "$BIN" ] && { echo "$BIN exists" >&2; exit 2; }
INPUTS=$(cd "$HERE" && nix-build --extra-experimental-features 'nix-command flakes' --max-jobs 1 --cores 2 --no-out-link link-inputs.nix | tail -1)
mkdir -p "$OUT"
[ -d "$OUT/source" ] || { cp -a "$SRC" "$OUT/source"; rm -rf "$OUT/source/target" "$OUT/source/.git"; }
install -m 644 "$INPUTS" "$OUT/link-inputs.json"
export DARWIN_LINK_INPUTS=$OUT/link-inputs.json CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER=$HERE/darwin-linker-inputs.py
( echo "start $(date -Is)"; cat "$INPUTS"; echo
  cd "$OUT/source" && cargo build -j "${JOBS:-2}" --offline --locked --release -p chonk-dock --bin chonk-dock --target aarch64-apple-darwin --target-dir ../target
  echo "end $(date -Is)"; echo "binary $BIN $(stat -c %s "$BIN") bytes sha256 $(sha256sum "$BIN" | cut -d' ' -f1)" ) >> "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log" >&2; exit 1; }
tail -1 "$OUT/build.log"
