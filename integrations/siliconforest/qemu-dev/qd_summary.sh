#!/bin/bash
# SPDX-License-Identifier: MIT
# The lines of a QEMU desktop run that matter. Usage: qd_summary.sh runs/qemu-dev/<name>
set -euo pipefail
OUT=${1:?run directory}
[ -r "$OUT/serial.log" ] || { echo "no serial log in $OUT" >&2; exit 2; }
strings -n 6 "$OUT/serial.log" | grep -a '^QDDESK_\|^WMAUTO [a-z]* .*result=\|^WMAUTO done\|^WMAUTO failed\|renderer unavailable\|panic(' | grep -av '^WMAUTO check' | cut -c1-260 | tail -60
echo "screenshots: $(ls "$OUT/shots" 2>/dev/null | wc -l) in $OUT/shots"
