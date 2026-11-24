#!/bin/bash
# SPDX-License-Identifier: MIT
# For the overnight batch, not for a session: N boots of the tip collection on eight CPUs with
# the SMP bench (m1-smp/guest/darwin-smp-bench.in), each queued through tools/pcie_iterate.sh.
# The corecrypto hang the DRBG patch fixed showed in 1 boot of 9 on the unpatched kext; three
# daytime boots do not show its absence, twenty give 9 in 10 to have seen it.
# Stops at the first boot that does not end in verified Linux.
# Usage: rb_overnight_smp.sh <first number, e.g. 20> [count, default 20]
set -uo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
FIRST=${1:?first boot number}; COUNT=${2:-20}; OUT=runs/rebase/overnight; mkdir -p $OUT
for i in $(seq "$FIRST" $((FIRST + COUNT - 1))); do
  n=rb$i
  RB_CPUS=8 RB_DONE=prompt RB_SED="-e s/@SOAK@/0/g" "$HERE/tools/rb_boot.sh" $n integrations/siliconforest/m1-smp/guest/darwin-smp-bench.in 420 > $OUT/$n.log 2>&1
  C=runs/m1-desktop-010/pcie-enum-attempt-$n/guest/console.bin
  printf '%s %s end_markers=%s\n' $n "$(grep -a -o 'RB_RESULT.*' $OUT/$n.log | cut -d' ' -f3)" "$(strings -n 6 $C 2>/dev/null | grep -ac '^SMP_END_')" >> $OUT/summary.txt
  grep -q 'LINUX_VERIFIED' $OUT/$n.log || { echo "stopped after $n: not back in verified Linux" >> $OUT/summary.txt; exit 1; }
done
