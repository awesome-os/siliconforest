#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the compositor (chonkstep-darwin-desktop) and the terminal (chonk-terminal-darwin) for
# arm64 Darwin against the SiliconForest source rebase/pd.nix selects (the tip; SF_PD=pin: the pin),
# as private builds under runs/m1-desktop-010/. Nothing is written into a source repository:
# both sources are copied into the build directory first.
#
#   tools/build_desktop_binaries.sh compositor <name> [source-dir]
#       source-dir: a compositor tree (default: the source of wm-darwin-scale-desktop-build-009,
#       i.e. branch wm-darwin's code as shipped). -> runs/m1-desktop-010/<name>/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop
#   tools/build_desktop_binaries.sh terminal <name> [source-dir]
#       source-dir default: the source of terminal-application-build-008.
#       -> runs/m1-desktop-010/<name>/target/aarch64-apple-darwin/release/chonk-terminal-darwin
#
# Link inputs (toolchain, linker, SDK, libSystem, SFGOP + static IOKit, xkbcommon, iconv) come from
# ../link-inputs.nix through ../darwin-linker-inputs.py; the spawn adapter object is compiled here
# with the same compiler and SDK. JOBS (default 2) is cargo's job count.
# DESKTOP_FEATURES: further features of wm-darwin for the compositor, e.g. test-inject (requested
# synthetic input and screen captures, a test instrument: the build that is installed as the
# desktop is made without it; see terminal-app/compositor/README).
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); LAB=$(cd "$HERE/../../.." && pwd)
export PATH=$HOME/.nix-profile/bin:$PATH
KIND=${1:?compositor|terminal}; NAME=${2:?build name}; RUNS=$LAB/runs/m1-desktop-010; OUT=$RUNS/$NAME
case "$KIND" in
  compositor) SRC=${3:-$RUNS/wm-darwin-scale-desktop-build-009/source};;
  terminal) SRC=${3:-$RUNS/terminal-application-build-008/source};;
  *) echo "usage: build_desktop_binaries.sh compositor|terminal <name> [source-dir]" >&2; exit 2;;
esac
case "$KIND" in
  compositor) BIN=$OUT/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop;;
  terminal) BIN=$OUT/target/aarch64-apple-darwin/release/chonk-terminal-darwin;;
esac
# a build directory without its binary is an interrupted or failed build: it is continued
[ -e "$BIN" ] && { echo "$BIN exists" >&2; exit 2; }
[ -f "$SRC/Cargo.toml" ] || { echo "no Cargo.toml in $SRC" >&2; exit 2; }
INPUTS=$(cd "$HERE" && nix-build --extra-experimental-features 'nix-command flakes' --max-jobs 1 --cores 2 --no-out-link link-inputs.nix | tail -1)
[ -f "$INPUTS" ] || { echo "link-inputs.nix did not build" >&2; exit 2; }
mkdir -p "$OUT"
[ -d "$OUT/source" ] || { cp -a "$SRC" "$OUT/source"; rm -rf "$OUT/source/target" "$OUT/source/.git"; }
install -m 644 "$INPUTS" "$OUT/link-inputs.json"
get() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$INPUTS" "$1"; }
export DARWIN_LINK_INPUTS=$OUT/link-inputs.json CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER=$HERE/darwin-linker-inputs.py
build() {
  set -e
  echo "start $(date -Is)"; echo "kind: $KIND"; echo "source: copy of $SRC"; echo "link inputs: $INPUTS"; cat "$INPUTS"; echo
  if [ "$KIND" = compositor ]; then
    CC=$(get toolchain)/bin/arm64-apple-darwin20.4-clang; SDK=$(get sdk)
    DARWIN_SDK_ROOT=$SDK SDKROOT=$SDK "$CC" -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -isystem "$SDK/usr/include" -I "$HERE/darwin-spawn" \
      -c "$HERE/darwin-spawn/darwin_spawn.c" -o "$OUT/darwin_spawn.o"
    echo "darwin_spawn.o sha256 $(sha256sum "$OUT/darwin_spawn.o" | cut -d' ' -f1)"
    env CARGO_TARGET_DIR="$OUT/target" \
      CARGO_TARGET_AARCH64_APPLE_DARWIN_RUSTFLAGS="-L native=$(get xkb) -L native=$(get sfgop) -C panic=unwind -C link-arg=$OUT/darwin_spawn.o" \
      cargo rustc -j "${JOBS:-2}" --offline --locked --release --manifest-path "$OUT/source/Cargo.toml" -p wm-darwin \
        --features "native-managed-spawn${DESKTOP_FEATURES:+,$DESKTOP_FEATURES}" --bin chonkstep-darwin-desktop --target aarch64-apple-darwin -- -C link-arg=-Wl,-dead_strip_dylibs
  else
    (cd "$OUT/source" && cargo build -j "${JOBS:-2}" --offline --locked --release --target aarch64-apple-darwin --target-dir ../target)
  fi
  echo "end $(date -Is)"; echo "binary $BIN $(stat -c %s "$BIN") bytes sha256 $(sha256sum "$BIN" | cut -d' ' -f1)"
}
set +e
( build ) >> "$OUT/build.log" 2>&1; RC=$?
set -e
[ "$RC" = 0 ] || { tail -30 "$OUT/build.log" >&2; exit 1; }
tail -2 "$OUT/build.log"
