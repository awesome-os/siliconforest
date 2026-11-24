#!/bin/bash
# SPDX-License-Identifier: MIT
# The lines of a ds boot's console that say what happened. Usage: ds_summary.sh <name> [more grep patterns]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
C=$ROOT/runs/m1-desktop-010/pcie-enum-attempt-${1:?name}/guest/console.bin
[ -r "$C" ] || { echo "no console capture at $C" >&2; exit 2; }
strings -n 6 "$C" |
  grep -a "DSDESK\|WMAUTO\|WMSCALE\|WMSET\|WMCLIENT\|TERMSHM\|TERMSCALE\|WMSTAT selected\|WMSTAT phase=\(scale\|settings\)\|WMPOWER\|panic\|chonkstep-darwin: \|finite desktop failure${2:+\|$2}" |
  cut -c1-1400
python3 - "$ROOT/runs/m1-desktop-010/pcie-enum-attempt-$1" <<'PY'
import json, sys, pathlib
p = pathlib.Path(sys.argv[1]) / 'result.json'
if p.exists():
    r = json.loads(p.read_text())
    print('RESULT', {k: r[k] for k in ('status', 'linux_verified', 'reset_attempts', 'diagnostics_complete') if k in r})
else:
    print('RESULT none (no result.json: the cycle did not finish)')
PY
