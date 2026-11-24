#!/bin/bash
# SPDX-License-Identifier: MIT
# One serialized boot of the M1 with the SFCpuPower collection and a guest script, through
# tools/pcie_iterate.sh (unchanged). Takes runs/bench.lock itself for the whole boot: do
# not wrap it in another flock. The shared collection link is put back afterwards.
# Usage: th_boot.sh <name, prefix th>
#   TH_KC        collection store path (default: build ../build-kc.nix)
#   TH_GUEST     guest script template (default guest/darwin-cpupower-suite.in)
#   TH_RUNTIME   seconds the guest runs, at most 600 unless TH_LONG=1 (default 520)
#   TH_BOOTARGS  further kernel boot arguments (sfcpufreq=, sfthermal_limit=, ...)
#   TH_CPUS      guest processors (default 8)
#   TH_HEAT, TH_FORCED, TH_BELOW, TH_STRESS, TH_SOAK, TH_AUTO, TH_CONTROL   fill the template's placeholders
#   TH_CONFIGS   for guest/darwin-cpupower-tune.in: "run NAME MODE PERIOD UP HOLD; run ..."
#   TH_TIMEWAIT  PCIE_TIMEWAIT of tools/pcie_iterate.sh (pinned-revision kernels only; at the tip
#                put sf_quiet_wait=0 sf_rtc_wait=0 into TH_BOOTARGS instead: 30 s less boot)
#   TH_BEFORE, TH_AFTER   shell commands run inside the bench lock before the boot and after
#                the M1 is back in Linux (e.g. tools/linux_fan_state.sh)
#   TH_SUMMARY   option for tools/th_summary.py (--fan, --soak)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
N=${1:?name}
case "$N" in th*) ;; *) echo "boot names are prefixed th" >&2; exit 2;; esac
KC=${TH_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
RUNTIME=${TH_RUNTIME:-520}
if [ "$RUNTIME" -gt 600 ] && [ "${TH_LONG:-0}" != 1 ]; then echo "boots are capped at 600 s (TH_LONG=1 for the overnight soak)" >&2; exit 2; fi
HEAT=${TH_HEAT:-120}; FORCED=${TH_FORCED:-60}
S=$(mktemp)
sed -e "s/@HEAT@/$HEAT/g" -e "s/@FORCED@/$FORCED/g" -e "s/@BELOW@/${TH_BELOW:-8}/g" -e "s/@STRESS@/${TH_STRESS:-1000}/g" \
    -e "s/@SOAK@/${TH_SOAK:-1800}/g" -e "s/@TOTAL@/$((230 + HEAT + FORCED))/g" \
    -e "s/@AUTO@/${TH_AUTO:-210}/g" -e "s/@CONTROL@/${TH_CONTROL:-90}/g" \
    -e "s|@CONFIGS@|${TH_CONFIGS:-run default 1 100 300 3}|g" \
    "${TH_GUEST:-$HERE/guest/darwin-cpupower-suite.in}" > "$S"
LINK=runs/m1-desktop-010/bwfm-kc-build-001/result
export KC S N RUNTIME LINK TH_CPUS=${TH_CPUS:-8} TH_BOOTARGS=${TH_BOOTARGS:-}
export TH_BEFORE=${TH_BEFORE:-} TH_AFTER=${TH_AFTER:-}
flock "$ROOT/runs/bench.lock" bash -c '
  set -uo pipefail
  [ -n "$TH_BEFORE" ] && bash -c "$TH_BEFORE"
  old=$(readlink "$LINK" || true)
  ln -sfn "$KC" "$LINK"
  PCIE_TIMEWAIT="${TH_TIMEWAIT:-}" PCIE_BOOTARGS="cpus=$TH_CPUS $TH_BOOTARGS" PCIE_GUEST_TEMPLATE="$S" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 \
    PCIE_WIFI_JOIN=1 PCIE_EXTRA_FILES="" LINES_MAX=1 tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
    grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space" | cut -c1-240
  [ -n "$old" ] && ln -sfn "$old" "$LINK"
  [ -n "$TH_AFTER" ] && bash -c "$TH_AFTER"
  true
'
rm -f "$S"
python3 -B "$HERE/tools/th_summary.py" ${TH_SUMMARY:-} "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin"
