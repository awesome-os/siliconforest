#!/bin/bash
# SPDX-License-Identifier: MIT
# The SiliconForest desktop in QEMU on this workstation: the M1's own arm64 binaries
# (compositor, terminal, audio server, session logic) on upstream PureDarwin's arm64 `virt`
# target under TCG. No M1, no bench. See integrations/siliconforest/qemu-dev/README.md for what
# this does and does not prove.
#
# Usage: tools/qemu_desktop.sh [options]
#   (no option)        headless, unattended: the compositor's autopilot drives the desktop,
#                      screenshots and the serial log go to runs/qemu-dev/<name>/, exit 0 = pass
#   --window           a QEMU window with the desktop, keyboard and pointer by hand (never full
#                      screen, never grabs the pointer: the guest sees a USB tablet)
#   --name N           run directory runs/qemu-dev/N (default: last)
#   --src DIR          build the compositor from this source tree first (cargo, incremental;
#                      build products in runs/qemu-dev/target, nothing is written to DIR)
#   --bin FILE         compositor binary (default: the --src build, else the M1's current one)
#   --terminal FILE    terminal binary (default: the M1's current one)
#   --only PHASES      autopilot phases, comma separated (default: all that apply here)
#   --seconds N        session length (default: 300 unattended, 3600 in a window)
#   --scale S          CHONKSTEP_SCALE (1, 1.25, 1.5, 1.75, 2, 2.5, 3; default: by the panel)
#   --theme ID         CHONKSTEP_THEME
#   --env 'A=B C=D'    further environment for the compositor
#   --terminals N      terminals opened at start (default 2)
#   --no-sound         do not start coreaudiod
#   --autopilot        with --window: the autopilot drives, you watch
#   --file SRC=/DEST   another file for the guest root (repeatable)
#   --shot-every N     seconds between screenshots (default 10; 0 none)
#   --resolution WxH   framebuffer size: 640x480, 800x600 or 1024x768 (default), all this firmware's ramfb offers
#   --kernel debug     upstream's DEBUG kernel instead of RELEASE (much slower)
#   --bootargs 'A B'   further kernel boot arguments
#   --libsystem homebrew   the Homebrew stream's libSystem (issue 46) instead of the M1 RAM root's
#   --smp N, --memory MB, --timeout S, --verbose-boot, --refresh (ask Nix again for inputs)
# Environment: SF_PD=pin  the base image (kernel collection, upstream userland) and libSystem of
#                    the old SiliconForest pin e7aa00a6 instead of upstream's tip, the default
#                    (integrations/siliconforest/rebase/pd.nix); templates and remembered Nix
#                    answers are kept per source
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
Q=integrations/siliconforest/qemu-dev
NAME=last; WINDOW=0; AUTO=; SRC=; BIN=; ONLY=; SECS=; SCALE=; THEME=; ENVS=; TERMS=2; SOUND=1
TERMINAL=runs/m1-desktop-010/terminal-application-build-008/target/aarch64-apple-darwin/release/chonk-terminal-darwin
IMAGE_ARGS=(); RUN_ARGS=(); TIMEOUT=; RES=1024x768
while [ $# -gt 0 ]; do
  case "$1" in
    --window) WINDOW=1;; --autopilot) AUTO=1;; --no-sound) SOUND=0; export QD_AUDIO=0;;
    --name) NAME=$2; shift;; --src) SRC=$2; shift;; --bin) BIN=$2; shift;; --terminal) TERMINAL=$2; shift;;
    --only) ONLY=$2; shift;; --seconds) SECS=$2; shift;; --scale) SCALE=$2; shift;; --theme) THEME=$2; shift;;
    --env) ENVS=$2; shift;; --terminals) TERMS=$2; shift;; --timeout) TIMEOUT=$2; shift;;
    --file) IMAGE_ARGS+=(--file "$2"); shift;;
    --verbose-boot|--refresh|--mesa) IMAGE_ARGS+=("$1");;
    --resolution) RES=$2; shift;;
    --kernel|--bootargs|--libsystem) IMAGE_ARGS+=("$1" "$2"); shift;;
    --shot-every|--smp|--memory|--pointer|--cpu) RUN_ARGS+=("$1" "$2"); shift;;
    -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d' | cut -c3-; exit 0;;
    *) echo "unknown option $1 (see --help)" >&2; exit 2;;
  esac; shift
done
case "$NAME" in *[!A-Za-z0-9._-]*|'') echo "--name: letters, digits, . _ - only" >&2; exit 2;; esac
case "$SCALE" in ''|1|1.25|1.5|1.75|2|2.5|3) ;; *) echo "--scale is one of 1 1.25 1.5 1.75 2 2.5 3" >&2; exit 2;; esac
case "$THEME$ONLY" in *[!a-z0-9,-]*) echo "--theme and --only are lower-case ids" >&2; exit 2;; esac
[ -n "$AUTO" ] || AUTO=$((1 - WINDOW))
[ -n "$SECS" ] || { [ "$WINDOW" = 1 ] && [ "$AUTO" = 0 ] && SECS=3600 || SECS=300; }
[ -n "$TIMEOUT" ] || TIMEOUT=$((SECS + 240))
OUT=runs/qemu-dev/$NAME; mkdir -p "$OUT"
T0=$(date +%s.%N)
if [ -n "$SRC" ]; then
  # As runs/m1-desktop-010/wm-darwin-scale-desktop-build-007/build.sh, with a shared incremental
  # target directory. cargo only reads the source tree.
  [ -f "$SRC/Cargo.toml" ] || { echo "no Cargo.toml in $SRC" >&2; exit 2; }
  TARGET=$ROOT/runs/qemu-dev/target
  env CARGO_TARGET_DIR="$TARGET" CARGO_BUILD_JOBS="${QD_JOBS:-8}" \
    CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER="$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-linker.py" \
    CARGO_TARGET_AARCH64_APPLE_DARWIN_RUSTFLAGS="-L native=/nix/store/5shniy0j07mkrl7kys7vslm9vh02lw6d-puredarwin-libxkbcommon-nox-1.13.1/lib -L native=/nix/store/nqi47vjchbk1yv29mz7kkxbfcmz7zhp5-puredarwin-chonkstep-pdgop-static-0.1/lib -C panic=unwind -C link-arg=$ROOT/runs/m1-desktop-010/darwin-spawn-build-002/darwin_spawn.o" \
    cargo rustc --offline --locked --release --manifest-path "$(cd "$SRC" && pwd)/Cargo.toml" -p wm-darwin --features native-managed-spawn \
    --bin chonkstep-darwin-desktop --target aarch64-apple-darwin -- -C link-arg=-Wl,-dead_strip_dylibs > "$OUT/build.log" 2>&1 \
    || { grep -n '^error' -A12 "$OUT/build.log" | head -60; echo "compositor build failed ($OUT/build.log)" >&2; exit 2; }
  [ -n "$BIN" ] || BIN=$TARGET/aarch64-apple-darwin/release/chonkstep-darwin-desktop
fi
[ -n "$BIN" ] || BIN=runs/m1-desktop-010/wm-darwin-scale-desktop-build-007/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop
[ -x "$BIN" ] || { echo "no compositor binary at $BIN" >&2; exit 2; }
[ -x "$TERMINAL" ] || { echo "no terminal binary at $TERMINAL" >&2; exit 2; }
T1=$(date +%s.%N)
# Phases that need the M1 (backlight keys, Touch Bar, display power) report SKIPPED by themselves.
[ "$AUTO" = 1 ] && STATS=120 || STATS=3600
EXPORTS=; for kv in $ENVS; do EXPORTS="${EXPORTS}export $kv; "; done
sed -e "s/@SECONDS@/$SECS/g" -e "s/@AUTOPILOT@/$AUTO/g" -e "s/@SCALE@/$SCALE/g" -e "s/@THEME@/$THEME/g" -e "s/@ONLY@/$ONLY/g" \
    -e "s/@STATS@/$STATS/g" -e "s/@SOUND@/$SOUND/g" -e "s/@RENDERER@/cpu/g" -e "s/@TERMINALS@/$TERMS/g" -e "s/@FETCH@/1/g" -e "s/@PERSIST@/0/g" -e "s|@ENV@|$EXPORTS|g" \
    "$Q/guest/qemu-desktop-session.in" > "$OUT/qemu-desktop-session"
python3 -B "$Q/qd_image.py" --output "$OUT/disk.img" --resolution "$RES" "${IMAGE_ARGS[@]}" \
  --file "$BIN=/usr/bin/chonkstep-darwin-desktop" --file "$TERMINAL=/usr/bin/chonk-terminal-darwin" \
  --file "$OUT/qemu-desktop-session=/usr/bin/qemu-desktop-session" \
  --file "$Q/guest/org.siliconforest.qemu-desktop.plist=/System/Library/LaunchDaemons/org.siliconforest.qemu-desktop.plist" > "$OUT/image.json"
T2=$(date +%s.%N)
printf 'qemu-desktop: build %.1f s, image %.1f s\n' "$(echo "$T1 - $T0" | bc)" "$(echo "$T2 - $T1" | bc)"
W=(); [ "$WINDOW" = 1 ] && W=(--window --pointer tablet)
# The pass marker: the autopilot's own verdict, or a clean exit by hand.
[ "$AUTO" = 1 ] && PASS='WMAUTO done result=ok' || PASS='QDDESK_PASS'
rc=0
python3 -B "$Q/qd_run.py" --image "$OUT/disk.img" --out "$OUT" --input-seen QDDESK_INPUT \
  --timeout "$TIMEOUT" --passed "$PASS" "${W[@]}" "${RUN_ARGS[@]}" || rc=$?
"$Q/qd_summary.sh" "$OUT" || true
exit $rc
