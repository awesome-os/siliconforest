#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only OR MIT
# One bench boot with the m1-dcp guest tools staged (dcp-flip-test, its libdrm, sf-backlight)
# and one of this directory's guest scripts as the session, through tools/dcp_boot.sh
# unchanged, serialised with the other agents by runs/bench.lock.
#   bl_boot.sh <name> <collection> <supervisor/script.in> [extra boot-args]
# Runtime: DCP_RUNTIME (seconds, default 180). The collection needs SFDcp; sfagx=1 is added
# so a collection with SFAgx brings the GPU up as the desktop does.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$here/../../.." && pwd)
N=${1:?name}; KC=${2:?collection}; S=${3:?guest script}; EXTRA=${4:-}
export PATH=$HOME/.nix-profile/bin:$PATH
T=$(cd "$here" && nix-build --extra-experimental-features nix-command --extra-experimental-features flakes dcp-flip-test.nix --no-out-link)
X="$T/usr/local/bin/dcp-flip-test=/usr/local/bin/dcp-flip-test;$T/usr/local/bin/sf-backlight=/usr/local/bin/sf-backlight"
X="$X;$T/usr/local/lib/mesa-asahi/libdrm.2.dylib=/usr/local/lib/mesa-asahi/libdrm.2.dylib"
export PCIE_EXTRA_FILES="$X;$(cd "$(dirname "$S")" && pwd)/$(basename "$S")=/usr/bin/darwin-desktop-test"
export DCP_MODE=2 DCP_RUNTIME=${DCP_RUNTIME:-180}
cd "$ROOT"
exec flock "$ROOT/runs/bench.lock" tools/dcp_boot.sh "$N" "$KC" "sfagx=1 $EXTRA"
