#!/bin/bash
# SPDX-License-Identifier: MIT
# One multi-core stress boot of the M1 (guest/darwin-la-stress.in) through
# tools/pcie_iterate.sh, unchanged. Queues for the bench itself (tools/bench.sh run; never
# wrap this script in another flock) and puts the shared collection link back.
# Usage: la_boot.sh <name, prefix la> [stages, default all] [scale seconds, default 20]
#   LA_KC        collection store path (default: build ../m1-smp/build-kc-desktop.nix, which
#                carries the audit's fixes)
#   LA_CPUS      guest processors (default 8)
#   LA_BOOTARGS  further kernel boot arguments
#   LA_RUNTIME   seconds the guest may run (default: computed from the stages and the scale;
#                boots above 10 minutes need the coordinator's go-ahead)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
N=${1:?name}; STAGES=${2:-all}; SCALE=${3:-20}; CPUS=${LA_CPUS:-8}
case "$N" in la*) ;; *) echo "boot names are prefixed la" >&2; exit 2;; esac
KC=${LA_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
T=$("$ROOT/tools/staged.sh" mesa)
ST=$("$ROOT/tools/staged.sh" drm-storm)
# what the desktop session stages (its dry run names the files), plus the tests
X=$(WM_GL_STEP=session WM_GL_DRY_RUN=1 tools/wm_gl_boot.sh dry "$KC" | grep '=/' | grep -v '=/usr/bin/darwin-desktop-test$' | paste -sd ';')
X="$X;$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-session.in=/usr/local/bin/wm-gl-session"
X="$X;$T/usr/local/bin/agx-offscreen-test=/usr/local/bin/agx-offscreen-test;$ST/usr/local/bin/drm-storm=/usr/local/bin/drm-storm"
# 150 s of boot and Wi-Fi join, then the stages' own limits
RUNTIME=${LA_RUNTIME:-$((150 + 60 + SCALE * 18 + 630))}
case ",$STAGES," in *,all,*|*,session,*) ;; *) RUNTIME=${LA_RUNTIME:-$((150 + 60 + SCALE * 15 + 300))};; esac
S=$(mktemp); sed -e "s/@STAGES@/$STAGES/g" -e "s/@SCALE@/$SCALE/g" "$HERE/guest/darwin-la-stress.in" > "$S"
LINK=runs/m1-desktop-010/bwfm-kc-build-001/result
export KC S N RUNTIME CPUS LINK X LA_BOOTARGS=${LA_BOOTARGS:-}
echo "la_boot: $N stages=$STAGES scale=$SCALE cpus=$CPUS runtime=${RUNTIME}s kc=$KC"
tools/bench.sh run bash -c '
  set -uo pipefail
  old=$(readlink "$LINK" || true)
  ln -sfn "$KC" "$LINK"
  PCIE_BOOTARGS="cpus=$CPUS sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 $LA_BOOTARGS" \
    PCIE_GUEST_TEMPLATE="$S" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 PCIE_EXTRA_FILES="$X" LINES_MAX=1 \
    tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 | grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space" | cut -c1-240
  [ -n "$old" ] && ln -sfn "$old" "$LINK"
'
rm -f "$S"
strings -n 6 "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" |
  grep -a 'LA_\|DRMSTORM\|drm-storm: UNEXPECTED\|hw\.\|WMGL3_\|WMSTAT.*final\|WMSTAT selected\|packet loss\|panic\|Panic\|assert\|Text page\|sf_dri' | cut -c1-260 | tail -150
