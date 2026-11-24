#!/bin/bash
# SPDX-License-Identifier: MIT
# The 2-hour all-at-once soak, for the overnight batch (NOT to be run without the
# coordinator scheduling it: there is no thermal protection yet). One boot: drm-storm on the
# render node, four exact-hash GL loops, Wi-Fi off/on/rejoin, and the GL desktop session, each
# stage sized by the scale, with 8 CPU workers under every stage.
# Usage: la_soak.sh <name, prefix la> [total minutes, default 120]
#   LA_KC as for la_boot.sh.
# Pass:  the console has   LA_RESULT stages=5 failed=0 status=PASS   and   LA_END_<nonce>,
#        every DRMSTORM line ends result=ok, every LA_GL_LOOP has fail=0, no "panic",
#        and hw.activecpu is still 8 on the last sysctl line.
# Fail:  any LA_*_STALLED, any LA_*_RC other than 0, a missing LA_END line (hang or panic:
#        symbolise with integrations/siliconforest/m1-dcp/tools/symbolize.sh).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
N=${1:?name}; MIN=${2:-120}
# the stages' timed parts are 8 + 2 + 3 + 3 = 16 scales long; the session adds its own ~5 min
SCALE=$(( (MIN * 60 - 600) / 16 ))
[ "$SCALE" -ge 20 ] || SCALE=20
LA_RUNTIME=$(( MIN * 60 + 900 )) exec "$HERE/la_boot.sh" "$N" all "$SCALE"
