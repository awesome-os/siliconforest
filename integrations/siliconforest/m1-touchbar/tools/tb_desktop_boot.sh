#!/bin/bash
# SPDX-License-Identifier: MIT
# One unattended boot of the whole desktop with the Touch Bar: the collection of
# ../build-kc.nix (the desktop collection with sound plus SFAppleTouchBar), Wi-Fi, GPU,
# display, sound, the compositor driving itself including its `touchbar` phase. Staging
# follows ../../m1-sound/tools/sd_desktop_boot.sh; the guest script is
# ../guest/darwin-touchbar-desktop.in. Queues for the bench itself.
# Usage: tb_desktop_boot.sh <name, prefix tb>
#   TB_KC           collection store path (default: build ../build-kc.nix)
#   TB_DESKTOP_BIN  compositor binary (default: the build with the touchbar autopilot phase,
#                   runs/m1-desktop-010/wm-darwin-touchbar-desktop-build-002)
#   TB_VERBOSE=1    text boot instead of the quiet boot (no launcher patch, no boot picture)
#   TB_BOOTARGS     further kernel boot arguments
#   SD_VOLUME, SD_TONEDB, SD_SPKDB   as in sd_desktop_boot.sh (30, 20, 22 and not louder)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-touchbar}
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
N=${1:?name}
case "$N" in tb*) ;; *) echo "boot names are prefixed tb" >&2; exit 2;; esac
SOUND=$ROOT/integrations/siliconforest/m1-sound
NB="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
KC=${TB_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { echo "no collection" >&2; exit 2; }
SPKDB=${SD_SPKDB:-22}
[ "$SPKDB" -ge 22 ] || { echo "SD_SPKDB: 22 dB below full scale or quieter" >&2; exit 2; }
A=$("$ROOT/tools/staged.sh" sysaudio)
[ -x "$A/usr/sbin/coreaudiod" ] || { echo "sysaudio build failed" >&2; exit 2; }
PLAY=$("$ROOT/tools/staged.sh" sfplay)
T=$("$ROOT/tools/staged.sh" mesa)
D=${TB_DESKTOP_BIN:-$ROOT/runs/m1-desktop-010/wm-darwin-touchbar-desktop-build-002/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop}
[ -x "$D" ] || { echo "no compositor binary at $D" >&2; exit 2; }
X="$D=/usr/bin/chonkstep-darwin-desktop"
TERM_BIN=$ROOT/runs/m1-desktop-010/terminal-application-build-005/target/aarch64-apple-darwin/release/chonk-terminal-darwin
[ -x "$TERM_BIN" ] || { echo "no terminal binary at $TERM_BIN" >&2; exit 2; }
X="$X;$TERM_BIN=/usr/bin/chonk-terminal-darwin"
TI=$ROOT/integrations/siliconforest/chonkstep-darwin/terminfo/xterm-256color
[ -r "$TI" ] && X="$X;$TI=/usr/share/terminfo/x/xterm-256color;$TI=/usr/share/terminfo/78/xterm-256color"
BG=$ROOT/integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png
[ -f "$BG" ] && X="$X;$BG=/usr/share/backgrounds/siliconforest/siliconforest.png"
for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"; done
# sound: the server, the frameworks, the tools
FW=System/Library/Frameworks
X="$X;$A/usr/sbin/coreaudiod=/usr/sbin/coreaudiod;$A/usr/bin/sftone=/usr/bin/sftone;$A/usr/bin/sfvolume=/usr/bin/sfvolume;$PLAY/usr/bin/sfplay=/usr/bin/sfplay"
for f in CoreAudio AudioToolbox AudioUnit; do X="$X;$A/$FW/$f.framework/Versions/A/$f=/$FW/$f.framework/Versions/A/$f"; done
# the Touch Bar: the digitizer's firmware (this machine's own, never committed) and the test tool
FW=$ROOT/private/firmware/apple/dfrmtfw-j293.bin
[ -f "$FW" ] || { echo "no digitizer firmware at $FW" >&2; exit 2; }
TEST=$("$ROOT/tools/staged.sh" sftbtest)
X="$X;$FW=/Library/Firmware/apple/dfrmtfw-j293.bin;$TEST/usr/bin/sftbtest=/usr/bin/sftbtest"
W=$(mktemp -d)
"$HERE/host-test/run-host-tests.sh" "$W" > "$W/host.log" || { cat "$W/host.log" >&2; echo "host tests failed" >&2; exit 2; }
CRC=$(sed -n "s/^TBHOST picture layer-media crc32=//p" "$W/host.log")
QUIET=()
QD=
if [ "${TB_VERBOSE:-0}" != 1 ] && [ -f "$BG" ]; then
  Q=integrations/siliconforest/m1-quiet-boot
  QD=$(mktemp -d); python3 "$Q/boot_logo.py" pack "$BG" "$QD" > /dev/null
  B=integrations/siliconforest/m1-bringup
  QUIET=(PCIE_LAB_PATCH="$ROOT/$Q/quiet-boot.patch" PCIE_LAB_FILES="$QD/boot-logo.gz=$B/boot-logo.gz;$QD/boot-logo.json=$B/boot-logo.json")
fi
S=$(mktemp)
sed -e "s/@VOLUME@/${SD_VOLUME:-30}/g" -e "s/@TONEDB@/${SD_TONEDB:-20}/g" -e "s/@SPKDB@/$SPKDB/g" -e "s/@CRC_MEDIA@/$CRC/g" "$HERE/guest/darwin-touchbar-desktop.in" > "$S"
env "${QUIET[@]}" PCIE_KC="$KC" PCIE_BOOTARGS="cpus=8 sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 ${TB_BOOTARGS:-}" PCIE_EXTRA_FILES="$X" \
  PCIE_GUEST_TEMPLATE="$S" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" 420 2>&1 |
  grep -a '"status"\|rror\|delivered\|No space' | cut -c1-240 || true
rm -rf "$S" "$W" ${QD:+"$QD"}
strings -n 6 "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" |
  grep -a 'TBDESK\|SFTouchBar:\|SFZ2:\|SFAdp:\|SFSpi:\|WMAUTO\|WMSTAT\|sftone\|SFSpeakers:\|SFSound: \(stopped\|jack\|CS42\)\|panic\|hw\.\(ncpu\|activecpu\)\|kern\.pd_' | cut -c1-700 | tail -130
