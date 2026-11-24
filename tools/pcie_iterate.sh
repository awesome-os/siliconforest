#!/bin/bash
# Build kext + KC, assemble a packet, deliver it to the M5, boot it once on the M1 and fetch the
# console. Usage: tools/pcie_iterate.sh <candidate-number> [runtime]   (options: end of this file)
#
# The whole script is one function, read completely before anything runs, and it leaves through
# `exit`: bash never comes back to this file for a running boot, so the file may be edited while
# boots are queued or running. Keep it that way: no command after the closing brace line.
main() {
set -euo pipefail
N=${1:?candidate number}; RUNTIME=${2:-120}
export PATH=$HOME/.nix-profile/bin:$PATH
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
SRC=integrations/siliconforest/m1-apcie-host; RUN=runs/m1-desktop-010
NAME=pcie-enum-candidate-$N; W=/Users/chriskearney/darwin-lab-firstboot-007
SSH="ssh -o HostKeyAlias=10.1.1.93 -o BatchMode=yes"
if [ -z "${M5_HOST:-}" ]; then
  M5_HOST=10.1.1.70
  # the controller answers on the dock Ethernet (.127), or on Wi-Fi as .70 or .93, whichever it has
  for h in 10.1.1.55 10.1.1.93 10.1.1.70 10.1.1.127; do $SSH -o ConnectTimeout=2 chriskearney@$h true 2>/dev/null && M5_HOST=$h; done
fi
export M5_HOST; M5=chriskearney@$M5_HOST
R=$RUN/pcie-enum-attempt-$N
now() { date +%s.%N; }

if [ "${PCIE__STAGE:-}" = cycle ]; then
  # Inside the bench: the launcher tree, one boot cycle, the console.
  LSHA=$(sha256sum $R/lab/integrations/siliconforest/m1-bringup/host_progress.py | cut -d' ' -f1)
  tar -C $R -cf - lab | $SSH $M5 "sudo -n sh -c 'rm -rf $W/lab-pcie-m2-001 && mkdir -m 700 $W/lab-pcie-m2-001 && tar -xf - -C $W/lab-pcie-m2-001 --strip-components 1 && chown -R root:staff $W/lab-pcie-m2-001'"
  echo "delivered $NAME; booting"
  echo "cycle_begin $(now)" >> $R/times.txt
  CHAIN=
  if [ "${PCIE_CHAIN:-0}" = 1 ] && [ -n "${BENCH_FINISHER:-}" ]; then
    CHAIN=--chain; echo "$ROOT/tools/m1_return_linux.sh" > "$BENCH_FINISHER"
  fi
  $SSH -o ServerAliveInterval=15 $M5 "sudo -n sh -c 'cd $W/lab-pcie-m2-001 && exec caffeinate -dimsu $W/venv/bin/python -B tools/m1_bringup_cycle.py --execute --confirm-recovery J293_RECOVERY_READY --cycles 1 --candidate $W/pcie-m2/$NAME/manifest.json --launcher-sha256 $LSHA --inspector-sha256 339bc14a5d6c025b0acd2422eef1b3ebd6f0fdcf544cfae9e3a9470077b3b8d5 --runtime $RUNTIME $CHAIN --inventory $W/lab/reports/sprint-007/ssh-target-inventory.json --private-root $W/pcie-m2-cycles'" | tail -2 || true
  echo "cycle_end $(now)" >> $R/times.txt
  if [ -n "$CHAIN" ]; then
    # Nothing pending on the controller (it returned the M1 to Linux itself): no finisher needed.
    PENDING=$($SSH $M5 "sudo -n sh -c 'ls $W/m1-bench-handoff.json $W/m1-kernel-cycle-incomplete.json 2>/dev/null | wc -l'" || echo 1)
    if [ "$PENDING" -eq 0 ]; then rm -f "$BENCH_FINISHER"; else echo "M1 left in the m1n1 proxy; the bench keeper hands it to the next queued boot or returns it to Linux (tools/bench.sh status)"; fi
  fi
  KEEP=${PCIE_KEEP_PACKET:-0}
  $SSH $M5 "sudo -n sh -c 'cd $W/pcie-m2-cycles && d=\$(ls -td cycle-*/001 | head -1) && tar -cf - -C \$d guest/console.bin guest/worker.log result.json \$(cd \$d && ls guest/console-times.txt 2>/dev/null); [ $KEEP = 1 ] || rm -rf $W/pcie-m2/$NAME'" | tar -xf - -C $R
  exit 0
fi

# Builds write shared links and files: with a build, the whole script runs inside the bench.
if [ "${PCIE_SKIP_BUILD:-0}" != 1 ] && ! tools/bench.sh held; then exec tools/bench.sh run "$0" "$@"; fi
T_START=$(now)
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes"
if [ "${PCIE_SKIP_BUILD:-0}" = 1 ]; then
  echo "reusing the existing kernel collection and tools"
elif [ "${PCIE_FROM_PATCH:-0}" = 1 ]; then
  # Build everything from the SiliconForest tree patches instead of the lab directories.
  # (At the tip siliconforest-patches/build-kc.nix is the default collection with the bridge and
  # the Wi-Fi pair from the patch build; SF_PD=pin: the pin's collection chain as before.)
  tools/pcie_export_patch.sh
  P=integrations/siliconforest/siliconforest-patches
  mkdir -p $RUN/bwfm-kc-build-001 $RUN/sf-dhcp-build-001
  (cd $P && $NIX build-kc.nix -o "$ROOT/$RUN/bwfm-kc-build-001/result" > "$ROOT/$RUN/bwfm-kc-build-001/build.log" 2>&1) || { grep -n ' error: ' -A4 $RUN/bwfm-kc-build-001/build.log | cut -c1-240 | head -40; exit 1; }
  (cd $P && $NIX build-userspace-from-patch.nix -o "$ROOT/$RUN/sf-dhcp-build-001/result" > "$ROOT/$RUN/sf-dhcp-build-001/build.log" 2>&1) || { tail -20 $RUN/sf-dhcp-build-001/build.log; exit 1; }
elif [ "${SF_PD:-tip}" != pin ]; then
  # The tip: one recipe builds the collection from the lab directories (rebase/build-kc.nix, what
  # tools/default_kc.sh names), with and without PCIE_WIFI; sf-netd is built with it. The two
  # shared links are set as the pin's build path below sets them.
  mkdir -p $RUN/apcie-host-kc-build-001 $RUN/bwfm-kc-build-001 $RUN/sf-netd-build-001
  (cd integrations/siliconforest/rebase && $NIX build-kc.nix -o "$ROOT/$RUN/bwfm-kc-build-001/result" > "$ROOT/$RUN/bwfm-kc-build-001/build.log" 2>&1) || { grep -n ' error: ' -A4 $RUN/bwfm-kc-build-001/build.log | cut -c1-240 | head -40; exit 1; }
  ln -sfn "$(readlink -f $RUN/bwfm-kc-build-001/result)" $RUN/apcie-host-kc-build-001/result
  (cd integrations/siliconforest/m1-bwfm/netd && $NIX build-pd-netd.nix -o "$ROOT/$RUN/sf-netd-build-001/result" > "$ROOT/$RUN/sf-netd-build-001/build.log" 2>&1) || { grep -n ' error: ' -A4 $RUN/sf-netd-build-001/build.log | head -20; exit 1; }
else
# SF_PD=pin: the pinned revision's collection chain (boot it with PCIE_PIN=1: the pin's RAM root)
mkdir -p $RUN/apcie-host-build-001 $RUN/apcie-host-kc-build-001
(cd $SRC && $NIX build-kext.nix -o "$ROOT/$RUN/apcie-host-build-001/result" > "$ROOT/$RUN/apcie-host-build-001/build.log" 2>&1) || { grep -n 'error' -A6 $RUN/apcie-host-build-001/build.log | head -40; exit 1; }
K=$(readlink -f $RUN/apcie-host-build-001/result)
sed -i "s|bridge = /nix/store/[^;]*;|bridge = $K/System/Library/Extensions/SFApcieT8103Bridge.kext;|" $SRC/build-kc.nix
(cd $SRC && $NIX build-kc.nix -o "$ROOT/$RUN/apcie-host-kc-build-001/result" > "$ROOT/$RUN/apcie-host-kc-build-001/build.log" 2>&1) || { tail -30 $RUN/apcie-host-kc-build-001/build.log; exit 1; }
if [ "${PCIE_WIFI:-0}" = 1 ]; then
  mkdir -p $RUN/sf-netd-build-001
  (cd integrations/siliconforest/m1-bwfm/netd && $NIX build-pd-netd.nix -o "$ROOT/$RUN/sf-netd-build-001/result" > "$ROOT/$RUN/sf-netd-build-001/build.log" 2>&1) || { grep -n ' error: ' -A4 $RUN/sf-netd-build-001/build.log | head -20; exit 1; }
  mkdir -p $RUN/bwfm-kc-build-001
  (cd integrations/siliconforest/m1-bwfm && $NIX build-kc.nix -o "$ROOT/$RUN/bwfm-kc-build-001/result" > "$ROOT/$RUN/bwfm-kc-build-001/build.log" 2>&1) || { grep -n ' error: ' -A4 $RUN/bwfm-kc-build-001/build.log | cut -c1-240 | head -40; exit 1; }
fi
fi
# A private copy of the launcher tree for this boot: its mode files are per boot, and the
# shared tree is not written while other boots are queued or running.
mkdir -p $R; rm -rf $R/lab
tar -C $RUN/pcie-m2-host-001 --exclude=__pycache__ -cf - lab | tar -C $R -xf -
LAB=$R/lab
# PCIE_LAB_PATCH=<patch file>: applied to this boot's private copy of the launcher tree only
if [ -n "${PCIE_LAB_PATCH:-}" ]; then patch -s -d $LAB -p1 < "$PCIE_LAB_PATCH" || { echo "PCIE_LAB_PATCH does not apply" >&2; exit 2; }; fi
# PCIE_LAB_FILES="<file>=<path below the tree>;...": files added to that private copy
if [ -n "${PCIE_LAB_FILES:-}" ]; then
  IFS=';' read -ra LAB_FILES <<< "$PCIE_LAB_FILES"
  for item in "${LAB_FILES[@]}"; do
    [ -f "${item%%=*}" ] && [ -d "$LAB/$(dirname "${item#*=}")" ] && cp "${item%%=*}" "$LAB/${item#*=}" || { echo "PCIE_LAB_FILES: cannot place $item" >&2; exit 2; }
  done
fi
echo "${PCIE_BOOTARGS:-}" > $LAB/integrations/siliconforest/m1-bringup/extra_bootargs.txt
PCIE_LAB=$LAB PCIE_ASSEMBLE_STAGE=build python3 -B tools/pcie_assemble.py $NAME | tail -2
cp integrations/siliconforest/m1-apcie-bridge/proxy_linkup.py $LAB/integrations/siliconforest/m1-bringup/pcie_linkup.py
echo "${PCIE_LINK:-none}" > $LAB/integrations/siliconforest/m1-bringup/pcie_linkup_mode.txt
echo "${PCIE_CPUFREQ:-on}" > $LAB/integrations/siliconforest/m1-bringup/cpufreq_mode.txt
echo "${PCIE_GPU_PROBE:-off}" > $LAB/integrations/siliconforest/m1-bringup/gpu_probe_mode.txt
echo "${PCIE_DCP_PROBE:-off}" > $LAB/integrations/siliconforest/m1-bringup/dcp_probe_mode.txt
echo "${PCIE_DONE:-marker}" > $LAB/integrations/siliconforest/m1-bringup/done_mode.txt
echo "${PCIE_TIMEWAIT:-off}" > $LAB/integrations/siliconforest/m1-bringup/time_wait_mode.txt
echo "${PCIE_USB_CHUNK:-off}" > $LAB/integrations/siliconforest/m1-bringup/usb_chunk_mode.txt
T_ASSEMBLED=$(now)
# The offline load check of the packet runs while it is delivered; a failure stops before the bench.
(PCIE_LAB=$LAB PCIE_ASSEMBLE_STAGE=check python3 -B tools/pcie_assemble.py $NAME > $R/assemble-check.log 2>&1) & CHECK=$!
# The packet is private to this boot: deliver it before queueing for the bench.
tar -C artifacts/m1-desktop-010 -cf - $NAME/manifest.json $NAME/build-record.json $NAME/source.patch $NAME/kernel.macho $NAME/ramdisk.img $NAME/ramdisk.record.json | $SSH $M5 "sudo -n sh -c 'mkdir -p -m 700 $W/pcie-m2 && rm -rf $W/pcie-m2/$NAME && tar -xf - -C $W/pcie-m2 && chown -R root:staff $W/pcie-m2/$NAME'"
wait $CHECK || { tail -20 $R/assemble-check.log; echo "packet check failed; not booting" >&2; exit 1; }
T_DELIVERED=$(now)
printf 'start %s\nassembled %s\npacket_delivered %s\n' $T_START $T_ASSEMBLED $T_DELIVERED > $R/times.txt
PCIE__STAGE=cycle BENCH_CHAIN=${PCIE_CHAIN:-0} tools/bench.sh run "$0" "$@"
T_END=$(now)
echo "end $T_END" >> $R/times.txt
awk -v n=$N '{t[$1]=$2} END {printf "BENCH_TIMES %s assemble=%.1fs deliver=%.1fs queued=%.1fs cycle=%.1fs total=%.1fs\n", n, t["assembled"]-t["start"], t["packet_delivered"]-t["assembled"], t["cycle_begin"]-t["packet_delivered"], t["cycle_end"]-t["cycle_begin"], t["end"]-t["start"]}' $R/times.txt >&2
grep -a 'PCIE_LINKUP\|CPUFREQ\|gpu probe\|dcp probe' $R/guest/worker.log || true
strings -n 6 $R/guest/console.bin | grep -E 'SFApcieT8103|SFBCM4378|SFBwfm|SFDart|DL3[2-9]|DL4[0-9]|agx-offscreen|WMBENCH|WMSTAT|SFRust|SFGpuDT|SFRtkit|SFAgx|SFDrm|sf-dhcp|sf-netd|SF_DHCP|ping|bytes from|HTTP/|netsetup|Connected|Couldn|wifiutil|SSID|interface:|power:|address:|state:|panic\(|IOPCI' | cut -c1-330 | head -${LINES_MAX:-80} || true
exit 0
}
main "$@"; exit $?

# Options (environment)
# The bench: this script queues for the shared bench itself (tools/bench.sh, first come first
# served). Callers need no wrapper; a caller that already holds it (bench.sh or a plain
# `flock runs/bench.lock`) is recognised and nothing is taken twice. With PCIE_SKIP_BUILD=1 the
# packet is assembled and delivered while waiting, from a private copy of the launcher tree
# (runs/.../pcie-enum-attempt-<n>/lab), and the bench is held only for the boot itself.
#   PCIE_DONE=marker|prompt|off  when the capture ends before <runtime> (the upper bound):
#                         marker (default): the guest script prints a line SF_BENCH_DONE;
#                         prompt: also when the shell prompt returns after the guest command;
#                         off: always the full runtime
#   PCIE_TIMEWAIT=<rtc>,<quiet>  seconds (1..30 each) for the two 30 s waits of XNU's
#                         IOKitInitializeTime (58 s of every boot); e.g. 1,5. Default off.
#                         An instruction-pattern patch that matches the pinned revision's kernels
#                         only; the tip's kernel boots unpatched (worker.log: applied=false).
#                         There, pass sf_quiet_wait=0 sf_rtc_wait=0 in PCIE_BOOTARGS instead.
#   PCIE_CHAIN=1          chained boots: do not wait for the M1 to return to Linux; see below
#   PCIE_KEEP_PACKET=1    keep the delivered packet on the M5 after the boot
#   PCIE_LAB_PATCH=<file> a patch (-p1) for this boot's private copy of the launcher tree, e.g.
#                         integrations/siliconforest/m1-smp-audit/host/vcpu-dump.patch (worker.log
#                         then has one SF_VCPU line per guest processor when the capture stops)
#   PCIE_KC=<directory>     with PCIE_SKIP_BUILD=1 and PCIE_WIFI=1: the collection to boot, instead of
#                         the shared link runs/m1-desktop-010/bwfm-kc-build-001/result
#   PCIE_LAB_FILES="<file>=<path>;..."  files copied into that private copy after the patch
#                         (path relative to the tree's root). The quiet boot uses both:
#                         integrations/siliconforest/m1-quiet-boot/quiet-boot.patch and the boot
#                         picture (tools/gpu_desktop.sh shows how)
#   M5_HOST=<address>     controller address (default: 10.1.1.127 dock Ethernet when it
#                         answers, else 10.1.1.70 Wi-Fi)
#
# Chained boots
# PCIE_CHAIN=1, when this script took the bench through tools/bench.sh: the cycle ends with the
# M1 left in the m1n1 proxy. The bench keeper then gives it straight to the next queued
# PCIE_CHAIN=1 boot (no Linux boot in between) or, with nobody queued, returns it to verified
# Linux (tools/m1_return_linux.sh) before releasing the bench; either way after this script
# has its console. Registered before the boot so that no outcome can leave it unreturned.
