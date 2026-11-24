#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the M1 (as a guest of the m1n1 hypervisor) with the SFAppleAVD collection and
# a guest script, through tools/pcie_iterate.sh, which queues for the bench itself: do not
# wrap it. Fast-boot settings of tools/gpu_desktop.sh. The decoder's firmware comes from
# private/firmware/apple/avd-fw-v2-t0.bin (copied from this machine's Linux, never
# committed) and goes into the RAM root as /Library/Firmware/apple/avd-fw-v2-t0.bin.
# Usage: avd_boot.sh <name, prefix av> [recording.pdaj ...]
#   AVD_KC        collection store path (default: build ../build-kc.nix)
#   AVD_GUEST     guest script template (default guest/darwin-avd-replay.in)
#   AVD_RUNTIME   upper bound of the guest's run in seconds, 120 to 600 (default 180)
#   AVD_BOOTARGS  further kernel boot arguments (default sfavd=2)
#   AVD_REPLAY_ARGS  arguments for sfavd-replay in the default guest script
#   AVD_EXTRA     shell text for the end of the default guest script
#   AVD_FILES     further files for the RAM root: "source=/destination;..."
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-avd}
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
N=${1:?name}; shift
case "$N" in av*) ;; *) echo "boot names are prefixed av" >&2; exit 2;; esac
NP=integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py
KC=${AVD_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { echo "no collection" >&2; exit 2; }
python3 "$ROOT/integrations/siliconforest/m1-rust-kext/tools/kc_overlap.py" "$KC/kernel" | tail -1
U=$("$ROOT/tools/staged.sh" avd-user)
[ -x "$U/usr/bin/sfavd-replay" ] || { echo "sfavd-replay build failed" >&2; exit 2; }
RUNTIME=${AVD_RUNTIME:-180}
[ "$RUNTIME" -ge 120 ] && [ "$RUNTIME" -le 600 ] || { echo "boots run 120 to 600 s" >&2; exit 2; }
FW=$ROOT/private/firmware/apple/avd-fw-v2-t0.bin
[ -f "$FW" ] || { echo "no decoder firmware at $FW (this machine's /lib/firmware/apple/avd-fw-v2-t0.bin)" >&2; exit 2; }
FILES="$U/usr/bin/sfavd-replay=/usr/bin/sfavd-replay;$FW=/Library/Firmware/apple/avd-fw-v2-t0.bin${AVD_FILES:+;$AVD_FILES}"
BYTES=0
for r in "$@"; do
  [ -f "$r" ] || { echo "no recording $r" >&2; exit 2; }
  FILES="$FILES;$(realpath "$r")=/private/var/avd/$(basename "$r")"
  BYTES=$((BYTES + $(stat -c %s "$r")))
done
[ "$BYTES" -le 60000000 ] || { echo "recordings are $BYTES bytes; the RAM root takes about 60 MB" >&2; exit 2; }
W=$(mktemp -d)
python3 - "${AVD_GUEST:-$HERE/guest/darwin-avd-replay.in}" "$W/guest" <<PY
import os, sys
text = open(sys.argv[1]).read()
text = text.replace('@REPLAY_ARGS@', os.environ.get('AVD_REPLAY_ARGS', '')).replace('@EXTRA@', os.environ.get('AVD_EXTRA', ''))
open(sys.argv[2], 'w').write(text)
PY
# the fast boot: boot picture and the launcher patch that types the challenge once the console is silent
BG=$ROOT/integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png
Q=integrations/siliconforest/m1-quiet-boot; B=integrations/siliconforest/m1-bringup
python3 "$Q/boot_logo.py" pack "$BG" "$W" > /dev/null
env -u PCIE_CHAIN PCIE_LAB_PATCH="$ROOT/integrations/siliconforest/m1-boot-speed/launcher-fast-quiet.patch" \
  PCIE_LAB_FILES="$W/boot-logo.gz=$B/boot-logo.gz;$W/boot-logo.json=$B/boot-logo.json" \
  PCIE_STAGER="$ROOT/integrations/siliconforest/m1-boot-speed/tools/stage_ramdisk_slim.py" PCIE_KC="$KC" \
  PCIE_BOOTARGS="cpus=8 sf_quiet_wait=0 sf_rtc_wait=0 ${AVD_BOOTARGS:-sfavd=2}" PCIE_EXTRA_FILES="$FILES" \
  PCIE_GUEST_TEMPLATE="$W/guest" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
  grep -a '"status"\|rror\|delivered\|No space' | cut -c1-240 || true
rm -rf "$W"
"$HERE/tools/avd_summary.sh" "$N"
