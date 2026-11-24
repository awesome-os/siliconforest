#!/bin/bash
# SPDX-License-Identifier: MIT
# How often does a collection hang before the shell? N chained boots (the M1 is not returned
# to Linux in between; the bench keeper returns it when the series ends), each bounded by
# LA_RUNTIME seconds, with a guest script that only reports the processors. A boot counts as
# up when the console has its LA_UP line with active=<cpus>; anything else is a hang, and
# worker.log then has one SF_VCPU line per guest processor (pc and frame chain, from
# m1-bringup/host_progress.py). The bench is held for the whole series.
# Usage: la_hangrate.sh <name prefix, la...> <count> [first index, default 1]
#   LA_KC        collection store path (required)
#   LA_CPUS      guest processors (default 8)
#   LA_BOOTARGS  further kernel boot arguments
#   LA_CHAIN     1 = chained boots (default 0: back through Linux after every boot)
#   LA_LAB_PATCH launcher patch for each boot, e.g. host/no-video-mirror.patch or host/vcpu-dump.patch
#   LA_RUNTIME   upper bound of one boot in seconds (default 120, the least the controller accepts)
# Prints one LA_HANGRATE_BOOT line per boot and LA_HANGRATE prefix=.. boots=.. up=.. hung=..
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
P=${1:?name prefix}; COUNT=${2:?count}; FIRST=${3:-1}
case "$P" in la*) ;; *) echo "boot names are prefixed la" >&2; exit 2;; esac
KC=${LA_KC:?LA_KC}; [ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
export LA_CHAIN=${LA_CHAIN:-0} LA_LAB_PATCH=${LA_LAB_PATCH:-}
export ROOT P COUNT FIRST KC CPUS=${LA_CPUS:-8} RUNTIME=${LA_RUNTIME:-120} LA_BOOTARGS=${LA_BOOTARGS:-}
export S="$HERE/guest/darwin-la-up.in" LINK=runs/m1-desktop-010/bwfm-kc-build-001/result
BENCH_WHO=${BENCH_WHO:-smp-audit} tools/bench.sh run bash -c '
  set -uo pipefail
  old=$(readlink "$LINK" || true)
  ln -sfn "$KC" "$LINK"
  up=0; hung=0
  for i in $(seq "$FIRST" $((FIRST + COUNT - 1))); do
    N=$P$(printf %02d "$i"); R=runs/m1-desktop-010/pcie-enum-attempt-$N/guest
    PCIE_CHAIN=${LA_CHAIN:-0} PCIE_DONE=marker PCIE_LAB_PATCH="${LA_LAB_PATCH:-}" \
      PCIE_BOOTARGS="cpus=$CPUS maxmem=4096 sf_quiet_wait=0 sf_rtc_wait=0 sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 $LA_BOOTARGS" \
      PCIE_GUEST_TEMPLATE="$S" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 \
      tools/pcie_iterate.sh "$N" "$RUNTIME" > "runs/m1-desktop-010/la-hangrate-$N.log" 2>&1
    if [ ! -s "$R/console.bin" ]; then
      # no console at all: the bench failed (controller refused, M1 not in the proxy), not the
      # guest. Stop the series: every further boot would only reset the M1 again.
      echo "LA_HANGRATE_BENCH_FAILED $N: $(grep -a -m1 "\"status\"" "runs/m1-desktop-010/la-hangrate-$N.log" | cut -c1-300)"
      break
    fi
    if strings -n 6 "$R/console.bin" 2>/dev/null | grep -aq "^LA_UP_.* active=$CPUS\$"; then
      up=$((up + 1)); v=up
    else
      hung=$((hung + 1)); v=HUNG
    fi
    echo "LA_HANGRATE_BOOT $N $v vcpus=$(grep -ac "^SF_VCPU " "$R/worker.log" 2>/dev/null) last=\"$(strings -n 6 "$R/console.bin" 2>/dev/null | tail -1 | cut -c1-80)\""
  done
  [ -n "$old" ] && ln -sfn "$old" "$LINK"
  echo "LA_HANGRATE prefix=$P kc=$KC cpus=$CPUS boots=$((up + hung)) up=$up hung=$hung"
'
if [ -e runs/bench.finisher.failed ]; then echo "the return to Linux failed: tools/bench.sh run tools/m1_return_linux.sh" >&2; fi
