#!/bin/bash
# SPDX-License-Identifier: MIT
# One serialized boot of the M1 for the CPU offline/idle test (guest/darwin-cpu-offline.in),
# through tools/pcie_iterate.sh like smp_boot.sh. Takes runs/bench.lock itself for the whole
# boot (do not wrap it in another flock) and puts the shared collection link back.
# Usage: cpuoff_boot.sh <name, prefix pd> [cycles per secondary, default 100]
#   CPUOFF_KC        collection store path (default: build ./build-kc.nix)
#   CPUOFF_CPUS      guest processors (default 8)
#   CPUOFF_BOOTARGS  further kernel boot arguments (sfcpupark=0, sfidledeep=1, ...)
#   CPUOFF_GUEST     guest script template (default guest/darwin-cpu-offline.in)
#   CPUOFF_RUNTIME   seconds the guest runs (default 600)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
N=${1:?name}; CYCLES=${2:-100}; CPUS=${CPUOFF_CPUS:-8}
case "$N" in pd*) ;; *) echo "boot names are prefixed pd" >&2; exit 2;; esac
KC=${CPUOFF_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
RUNTIME=${CPUOFF_RUNTIME:-600}
S=$(mktemp); sed "s/@CYCLES@/$CYCLES/g" "${CPUOFF_GUEST:-$HERE/guest/darwin-cpu-offline.in}" > "$S"
sh -n "$S"
LINK=runs/m1-desktop-010/bwfm-kc-build-001/result
export KC S N RUNTIME CPUS LINK CPUOFF_BOOTARGS=${CPUOFF_BOOTARGS:-}
flock "$ROOT/runs/bench.lock" bash -c '
  set -uo pipefail
  old=$(readlink "$LINK" || true)
  ln -sfn "$KC" "$LINK"
  PCIE_BOOTARGS="cpus=$CPUS $CPUOFF_BOOTARGS" PCIE_GUEST_TEMPLATE="$S" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 \
    PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
    grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space" | cut -c1-240
  [ -n "$old" ] && ln -sfn "$old" "$LINK"
'
rm -f "$S"
R=runs/m1-desktop-010/pcie-enum-attempt-$N/guest
grep -a 'GUEST_CPUS\|Starting guest secondary' "$R/worker.log" | cut -c1-160 | head -12 || true
strings -n 4 "$R/console.bin" | grep -a 'CPUOFF_\|hw\.\|machdep\.sfcpu\|SFAppleBootCPUPMGR\|panic\|Panic\|cpuctl' | cut -c1-260 | tail -150
