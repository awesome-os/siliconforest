#!/bin/bash
# The desktop session boot (tools/wm_gl_boot.sh, WM_GL_STEP=session) on a multitouch collection,
# with the trackpad self-test in front of the session:
#   integrations/siliconforest/m1-trackpad-child/multitouch/mt_session_boot.sh <new-name> <kc-store-path> [extra boot-args]
# Adds the boot-args sfm1tpselftest=2 sfm1tpdebug=40 and the guest prelude mt-selftest-prelude.in
# in front of the unmodified session script (the combined script is written under artifacts/,
# which git ignores). Prints the driver's MT lines and the two self-test results afterwards.
#   MT_DRY_RUN=1       only write the combined guest script and show the staging list
#   MT_PRELUDE_ONLY=1  run the self-test without the desktop session (about three minutes)
# On the shared bench: flock /home/chrisk/src/darwin-lab/runs/bench.lock <this command>
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); cd "$ROOT"
N=${1:?name}; KC=${2:?kernel collection store path}; EXTRA=${3:-}
SESSION=$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-session.in
mkdir -p artifacts/m1-desktop-010
T=$ROOT/artifacts/m1-desktop-010/mt-selftest-session-$N.in
STEP=session
if [ "${MT_PRELUDE_ONLY:-0}" = 1 ]; then
  # The self-test alone, without the desktop session: a shorter boot.
  { cat "$HERE/mt-selftest-prelude.in"; printf "printf 'MTSELFTEST_END\\n'\n"; } > "$T"
  STEP=probe; export DCP_RUNTIME=${DCP_RUNTIME:-185}
else
  cat "$HERE/mt-selftest-prelude.in" "$SESSION" > "$T"
fi
sh -n "$T"
export DCP_RUNTIME=${DCP_RUNTIME:-285}
ARGS="sfm1tpselftest=2 sfm1tpdebug=40 $EXTRA"
if [ "${MT_DRY_RUN:-0}" = 1 ]; then
  WM_GL_DRY_RUN=1 WM_GL_STEP=$STEP WM_GL_TEMPLATE="$T" tools/wm_gl_boot.sh "$N" "$KC" "$ARGS"; exit 0
fi
WM_GL_STEP=$STEP WM_GL_TEMPLATE="$T" tools/wm_gl_boot.sh "$N" "$KC" "$ARGS"
echo "---- trackpad multitouch ----"
strings -n 6 "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" | grep -a 'SFM1SPIHID: MT\|MTSELFTEST\|SFM1SPIHID: stopped\|SFM1SPIHID: T2 fault\|SFM1SPIHID: first device-1' | cut -c1-260
