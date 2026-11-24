#!/bin/bash
# SPDX-License-Identifier: MIT
# The desktop session, Wi-Fi and an all-CPU load in one boot (guest/darwin-smp-desktop-
# stress.in). Stages what `WM_GL_STEP=session tools/wm_gl_boot.sh` stages (its dry run names
# the files) plus the session script itself, on the Wi-Fi boot path.
# Usage: smp_desktop_boot.sh <name, prefix sm> [load seconds, default 200]
#   SMP_KC, SMP_CPUS, SMP_BOOTARGS as for smp_boot.sh
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
N=${1:?name}; LOAD=${2:-200}
X=$(WM_GL_STEP=session WM_GL_DRY_RUN=1 tools/wm_gl_boot.sh dry "${SMP_KC:?collection}" | grep '=/' | grep -v '=/usr/bin/darwin-desktop-test$' | paste -sd ';')
X="$X;$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-session.in=/usr/local/bin/wm-gl-session"
S=$(mktemp); sed "s/@LOAD@/$LOAD/g" "$HERE/guest/darwin-smp-desktop-stress.in" > "$S"
SMP_EXTRA_FILES="$X" SMP_GUEST="$S" SMP_RUNTIME=${SMP_RUNTIME:-$((150 + 40 + 330))} \
  SMP_BOOTARGS="sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 ${SMP_BOOTARGS:-}" \
  "$HERE/tools/smp_boot.sh" "$N" 0 | grep -a '"status"\|rror\|delivered\|GUEST_CPUS\|No space' | cut -c1-240 || true
rm -f "$S"
strings -n 6 "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" |
  grep -a 'SMPD_\|SMP_WORKER\|hw\.\|WMAUTO\|WMGL3_\|WMSTAT.*final\|WMSTAT selected\|bytes from\|packet loss\|panic\|Panic\|assert' | cut -c1-260 | tail -120
