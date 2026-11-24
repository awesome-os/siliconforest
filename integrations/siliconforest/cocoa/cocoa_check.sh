#!/bin/bash
# SPDX-License-Identifier: MIT
# One automated check of Mac applications on the SiliconForest desktop in QEMU (arm64 virt,
# TCG, headless): builds what is missing, makes a run image, boots it and runs a step list.
#
#   cocoa_check.sh <check> [--name N] [--scale S] [--theme ID] [--compositor BIN] [--root DIR]
#                  [--tree DIR=/DEST]... [--seconds N] [--keep-image]
#
# <check> is a file in ./checks (without .json): the applications to start, extra environment
# for the session, and the steps (see cocoa_run.py). @SCALE@ in it is the --scale value (1
# when none is given). Output: runs/cocoa/<name>/ (serial.log, shots/, result.json);
# exit 0 = every step done and every check passed.
#
#   --scale S        CHONKSTEP_SCALE for the session (1, 1.25, 1.5, 1.75, 2, 2.5, 3)
#   --theme ID       CHONKSTEP_THEME
#   --compositor B   the compositor binary (default: runs/cocoa/target/.../chonkstep-darwin-desktop
#                    when it exists, built from the Cocoa worktree; else the M1's current one)
#   --root DIR       the Cocoa system root (default: nix-build build-desktop-root.nix)
#   --tree D=/DEST   a further directory tree for the image (an application bundle)
# COCOA_LIBSYSTEM_NIX=<recipe> selects another libSystem (./build-libsystem-cocoa.nix: with the
# newer sandbox profile compiler); its System/ tree, if it has one, is staged as well.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); LAB=$(cd "$HERE/../../.." && pwd); cd "$LAB"
[ $# -ge 1 ] || { sed -n '2,/^set -euo/p' "$0" | sed '$d' | cut -c3-; exit 2; }
CHECK=$1; shift
NAME=; SCALE=; THEME=; COMP=; ROOTDIR=; SECS=; KEEP=0; TREES=()
while [ $# -gt 0 ]; do
  case "$1" in
    --name) NAME=$2; shift;; --scale) SCALE=$2; shift;; --theme) THEME=$2; shift;; --compositor) COMP=$2; shift;;
    --root) ROOTDIR=$2; shift;; --tree) TREES+=(--tree "$2"); shift;; --seconds) SECS=$2; shift;; --keep-image) KEEP=1;;
    *) echo "unknown option $1" >&2; exit 2;;
  esac; shift
done
SPEC=$HERE/checks/$CHECK.json
[ -f "$SPEC" ] || { echo "no check $SPEC" >&2; exit 2; }
[ -n "$NAME" ] || NAME=$CHECK${SCALE:+-s$SCALE}${THEME:+-$THEME}
case "$NAME" in *[!A-Za-z0-9._-]*) echo "--name: letters, digits, . _ - only" >&2; exit 2;; esac
OUT=runs/cocoa/$NAME; mkdir -p "$OUT"
export PATH=$HOME/.nix-profile/bin:$PATH NIX_CONFIG='build-users-group = '
nixb() { nix-build --extra-experimental-features 'nix-command flakes' -j 1 --cores "${COCOA_CORES:-3}" --no-out-link "$@" 2>>"$OUT/build.log" | tail -1; }
: > "$OUT/build.log"
[ -n "$ROOTDIR" ] || ROOTDIR=$(nixb "$HERE/build-desktop-root.nix")
[ -d "$ROOTDIR" ] || { echo "the Cocoa root did not build ($OUT/build.log)" >&2; exit 2; }
# the lab.s default libSystem and dyld (libsystem-default: ___chkstk_darwin as a real stack probe, the macOS ABI entry points)
LIBSYSTEM=$(nixb "${COCOA_LIBSYSTEM_NIX:-$LAB/integrations/siliconforest/libsystem-default/build-libsystem.nix}")
[ -d "$LIBSYSTEM/usr/lib" ] || { echo "libSystem did not build ($OUT/build.log)" >&2; exit 2; }
if [ -z "$COMP" ]; then
  COMP=runs/cocoa/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop
  [ -x "$COMP" ] || COMP=runs/m1-desktop-010/wm-darwin-scale-desktop-build-009/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop
fi
TERMINAL=runs/m1-desktop-010/terminal-application-build-008/target/aarch64-apple-darwin/release/chonk-terminal-darwin
[ -x "$COMP" ] && [ -x "$TERMINAL" ] || { echo "no compositor ($COMP) or terminal ($TERMINAL)" >&2; exit 2; }
python3 - "$SPEC" "$OUT" "${SCALE:-1}" "${SECS:-}" <<'PY'
import json, sys
spec, out, scale, seconds = sys.argv[1:5]
text = open(spec).read().replace('@SCALE@', scale)
check = json.loads(text)
open(out + '/steps.json', 'w').write(json.dumps(check['steps'], indent=1) + '\n')
open(out + '/cocoa-apps', 'w').write('\n'.join(check.get('apps', [])) + '\n')
open(out + '/session.env', 'w').write(check.get('env', '') + '\n')
open(out + '/session.seconds', 'w').write(str(seconds or check.get('seconds', 600)) + '\n')
open(out + '/session.terminals', 'w').write(str(check.get('terminals', 1)) + '\n')
open(out + '/session.memory', 'w').write(str(check.get('memory', 2048)) + '\n')
PY
ENVLINE=$(cat "$OUT/session.env"); [ -n "$ENVLINE" ] && ENVLINE="export $ENVLINE"
sed -e "s/@SECONDS@/$(cat "$OUT/session.seconds")/g" -e "s/@SCALE@/$SCALE/g" -e "s/@THEME@/$THEME/g" -e 's/@APPDELAY@/3/g' -e 's/@APPGAP@/4/g' \
    -e "s/@TERMINALS@/$(cat "$OUT/session.terminals")/g" -e "s|@ENV@|$ENVLINE|g" "$HERE/guest/cocoa-session.in" > "$OUT/cocoa-session"
[ -d "$LIBSYSTEM/System" ] && TREES+=(--tree "$LIBSYSTEM/System=/System")
python3 -B "$HERE/cocoa_image.py" --output "$OUT/disk.img" --tree "$ROOTDIR=/" --tree "$LIBSYSTEM/usr/lib=/usr/lib" "${TREES[@]}" \
  --file "$OUT/cocoa-session=/usr/bin/cocoa-session" --file "$COMP=/usr/bin/chonkstep-darwin-desktop" \
  --file "$TERMINAL=/usr/bin/chonk-terminal-darwin" \
  --data "$HERE/guest/org.siliconforest.cocoa-session.plist=/System/Library/LaunchDaemons/org.siliconforest.cocoa-session.plist" \
  --data "$OUT/cocoa-apps=/etc/cocoa-apps" > "$OUT/image.json"
rc=0
python3 -B "$HERE/cocoa_run.py" --image "$OUT/disk.img" --out "$OUT" --script "$OUT/steps.json" --memory "$(cat "$OUT/session.memory")" \
  --timeout $(( $(cat "$OUT/session.seconds") + 900 )) || rc=$?
[ "$KEEP" = 1 ] || rm -f "$OUT/disk.img"
python3 - "$OUT" <<'PY'
import json, sys
r = json.load(open(sys.argv[1] + '/result.json'))
for name, m in r.get('measured', {}).items():
    print('cocoa-check: measured %s: %s' % (name, json.dumps(m)))
PY
exit $rc
