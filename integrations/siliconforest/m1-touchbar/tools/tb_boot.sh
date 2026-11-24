#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the M1 with the SFAppleTouchBar collection and a guest script, through
# tools/pcie_iterate.sh, which queues for the bench itself: do not wrap it. The collection is
# handed over with PCIE_KC; nothing shared is written. The digitizer's firmware comes from
# private/firmware/apple (this machine's own file, never committed) and goes into the RAM
# root as /Library/Firmware/apple/dfrmtfw-j293.bin, as the Wi-Fi firmware does.
# Usage: tb_boot.sh <name, prefix tb>
#   TB_KC        collection store path (default: build ../build-kc.nix)
#   TB_GUEST     guest script template (default guest/darwin-touchbar-test.in)
#   TB_RUNTIME   upper bound of the guest's run in seconds, 120 to 600 (default 240)
#   TB_BOOTARGS  further kernel boot arguments (sftouchbar=, sftouchbar_bright=, ...)
#   TB_CPUS      guest processors (default 8)
#   TB_NOFW=1    leave the firmware out (the display-only path)
#   TB_FILES     further files for the RAM root: "source=/destination;..."
#   TB_WAITS     boot-args for the kernel's two start-up waits (default "sf_quiet_wait=0 sf_rtc_wait=0":
#                neither is waited for; TB_WAITS=stock leaves the kernel's 30 s registry wait in)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-touchbar}
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
N=${1:?name}
case "$N" in tb*) ;; *) echo "boot names are prefixed tb" >&2; exit 2;; esac
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
KC=${TB_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { tail -30 "${TMPDIR:-/tmp}/tb-kc-build.$$.log" >&2; echo "no collection" >&2; exit 2; }
rm -f "${TMPDIR:-/tmp}/tb-kc-build.$$.log"
python3 "$ROOT/integrations/siliconforest/m1-rust-kext/tools/kc_overlap.py" "$KC/kernel" | tail -1
TEST=$("$ROOT/tools/staged.sh" sftbtest)
[ -x "$TEST/usr/bin/sftbtest" ] || { echo "sftbtest build failed" >&2; exit 2; }
RUNTIME=${TB_RUNTIME:-240}
[ "$RUNTIME" -ge 120 ] && [ "$RUNTIME" -le 600 ] || { echo "boots run 120 to 600 s" >&2; exit 2; }
W=$(mktemp -d)
# the pictures' checksums from the same renderer, built for this machine
"$HERE/host-test/run-host-tests.sh" "$W" > "$W/host.log" || { cat "$W/host.log" >&2; echo "host tests failed" >&2; exit 2; }
crc() { sed -n "s/^TBHOST picture $1 crc32=//p" "$W/host.log"; }
sed -e "s/@CRC_FN@/$(crc layer-fn)/g" -e "s/@CRC_MEDIA@/$(crc layer-media)/g" -e "s/@CRC_MUTE_HELD@/$(crc layer-media-mute-held)/g" \
    "${TB_GUEST:-$HERE/guest/darwin-touchbar-test.in}" > "$W/guest"
FILES="$TEST/usr/bin/sftbtest=/usr/bin/sftbtest${TB_FILES:+;$TB_FILES}"
FW=$ROOT/private/firmware/apple/dfrmtfw-j293.bin
if [ "${TB_NOFW:-0}" != 1 ]; then
  [ -f "$FW" ] || { echo "no digitizer firmware at $FW (copy this machine's /lib/firmware/apple/dfrmtfw-j293.bin there)" >&2; exit 2; }
  FILES="$FILES;$FW=/Library/Firmware/apple/dfrmtfw-j293.bin"
fi
# PCIE_TIMEWAIT (the instruction-pattern patch) does not match the tip kernel; its boot-args do the same.
WAITS=${TB_WAITS-sf_quiet_wait=0 sf_rtc_wait=0}; [ "$WAITS" = stock ] && WAITS=
PCIE_KC="$KC" PCIE_BOOTARGS="cpus=${TB_CPUS:-8} $WAITS ${TB_BOOTARGS:-}" \
  PCIE_GUEST_TEMPLATE="$W/guest" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 PCIE_EXTRA_FILES="$FILES" LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
  grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space\|BENCH_TIMES" | cut -c1-240 || true
rm -rf "$W"
"$HERE/tools/tb_summary.sh" "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin"
