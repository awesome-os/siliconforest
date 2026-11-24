#!/bin/bash
# SPDX-License-Identifier: MIT
# One serialized SMP boot of the M1 through tools/pcie_iterate.sh (unchanged), with this
# directory's collection and guest script. The bench lock is held for the whole boot; the
# shared collection link is put back afterwards.
# Usage: smp_boot.sh <name, prefix sm> [soak seconds, default 0]
#   SMP_KC        collection store path (default: build ./build-kc.nix)
#   SMP_CPUS      guest processors, 1..8 (default 8; 1 is the single-CPU fallback boot)
#   SMP_BOOTARGS  further kernel boot arguments
#   SMP_GUEST     guest script template (default guest/darwin-smp-bench.in)
#   SMP_RUNTIME   seconds the guest runs (default: 150 boot + benchmark + soak)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
N=${1:?name}; SOAK=${2:-0}; CPUS=${SMP_CPUS:-8}
case "$N" in sm*) ;; *) echo "boot names are prefixed sm" >&2; exit 2;; esac
KC=${SMP_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
RUNTIME=${SMP_RUNTIME:-$((150 + 50 + 40 + 60 + 90 + 30 + SOAK))}
S=$(mktemp); sed "s/@SOAK@/$SOAK/g" "${SMP_GUEST:-$HERE/guest/darwin-smp-bench.in}" > "$S"
LINK=runs/m1-desktop-010/bwfm-kc-build-001/result
export KC S N RUNTIME CPUS LINK SMP_BOOTARGS=${SMP_BOOTARGS:-}
flock "$ROOT/runs/bench.lock" bash -c '
  set -uo pipefail
  old=$(readlink "$LINK" || true)
  ln -sfn "$KC" "$LINK"
  PCIE_BOOTARGS="cpus=$CPUS $SMP_BOOTARGS" PCIE_GUEST_TEMPLATE="$S" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 \
    PCIE_WIFI_JOIN=1 PCIE_EXTRA_FILES="${SMP_EXTRA_FILES:-}" LINES_MAX=1 tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
    grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space" | cut -c1-240
  [ -n "$old" ] && ln -sfn "$old" "$LINK"
'
rm -f "$S"
R=runs/m1-desktop-010/pcie-enum-attempt-$N/guest
grep -a 'GUEST_CPUS\|Starting guest secondary\|RVBAR = ' "$R/worker.log" | cut -c1-200 | head -20 || true
strings -n 4 "$R/console.bin" | grep -a 'SMP_\|hw\.\|kern\.\|SFAppleBootCPUPMGR\|SFPerfControl\|panic\|Panic' | cut -c1-240 | tail -80
