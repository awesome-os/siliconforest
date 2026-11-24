#!/bin/bash
# SPDX-License-Identifier: MIT
# The 30-minute 8-worker thermal run (issue #26, C7 acceptance), for an unattended batch.
# One boot of about 34 minutes; takes runs/bench.lock itself (do not wrap it in flock).
#   integrations/siliconforest/m1-cpufreq/tools/thermal_soak_overnight.sh [name, default thsoak01] [seconds, default 1800]
# The collection is TH_KC, or ../build-kc.nix built now. Ends with th_summary.py --soak:
# one PASS/FAIL line per check and a final "RESULT PASS" or "RESULT FAIL"; exit status 0
# only for PASS. The console is runs/m1-desktop-010/pcie-enum-attempt-<name>/guest/console.bin.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
N=${1:-thsoak01}; SOAK=${2:-1800}
TH_LONG=1 TH_SOAK=$SOAK TH_RUNTIME=$((150 + 70 + SOAK + 30)) TH_GUEST="$HERE/guest/darwin-cpupower-soak.in" \
    "$HERE/tools/th_boot.sh" "$N" > /dev/null 2>&1 || true
exec python3 -B "$HERE/tools/th_summary.py" --soak "$HERE/../../../runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin"
