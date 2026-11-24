#!/bin/bash
# Build the SFAgx (GPU driver) collection and boot it once with the off-screen GL test staged.
# Usage: tools/agx_boot.sh <new-candidate-name> [extra boot-args]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"; export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; EXTRA=${2:-}
(cd integrations/siliconforest/m1-rust-kext && nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-agx-kc.nix --arg wifi ${AGX_WIFI:-true} -o "$ROOT/runs/m1-desktop-010/bwfm-kc-build-001/result" 2>&1 | tail -1)
T=$(tools/staged.sh mesa)
X="$T/usr/local/bin/agx-offscreen-test=/usr/local/bin/agx-offscreen-test"
[ -f "$T/usr/local/bin/agx-panel-demo" ] && X="$X;$T/usr/local/bin/agx-panel-demo=/usr/local/bin/agx-panel-demo"
for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"; done
[ -n "${AGX_EXTRA:-}" ] && X="$X;$AGX_EXTRA"
PCIE_BOOTARGS="sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 $EXTRA" PCIE_EXTRA_FILES="$X" \
PCIE_GUEST_TEMPLATE=${AGX_TEMPLATE:-$ROOT/integrations/siliconforest/m1-mesa-asahi/supervisor/darwin-agx-offscreen-test.in} \
PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" "${AGX_RUNTIME:-150}" 2>&1 | grep -a '"status"\|Error' | cut -c1-200
strings -n 6 runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin | grep -a "SFAgx\|asahi\|pd platform\|panic\|DL4[0-9]\|agx-offscreen\|AGX[A-Z_]*\|agx-panel\|sf_dri\|rtkit" | grep -v "agx-offscreen-test:   " | cut -c1-260
