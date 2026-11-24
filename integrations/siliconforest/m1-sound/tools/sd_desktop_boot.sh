#!/bin/bash
# SPDX-License-Identifier: MIT
# One unattended boot of the whole desktop with sound: the collection of ../build-kc-desktop.nix
# (the default desktop collection plus SFAppleSound), Wi-Fi, GPU, display, the compositor driving
# itself, coreaudiod and the audio frameworks. Staging follows tools/gpu_desktop.sh; the guest
# script is ../guest/darwin-sound-desktop.in. Queues for the bench itself.
# Usage: sd_desktop_boot.sh <name, prefix sd>
#   SD_KC           collection store path (default: build ../build-kc-desktop.nix)
#   SD_DESKTOP_BIN  compositor binary (default: the wm-darwin-gl desktop build)
#   SD_VERBOSE=1    text boot instead of the quiet boot (no launcher patch, no boot picture)
#   SD_WAITS        fast (default): sf_quiet_wait=0 sf_rtc_wait=0 and, for the quiet boot, the launcher
#                   patch of m1-boot-speed; stock: the kernel's waits and m1-quiet-boot's patch
#   SD_AUDIO        tip or pin: the source the audio server, tools and sfplay are built from
#   SD_VOLUME, SD_TONEDB   headphone attenuation and tone level (30, 20)
#   SD_SPKDB        tone level on the speakers, dB below full scale (default 22, not louder; the
#                   guest script keeps the amplifiers another 20 dB down)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-sound}
# Mesa is still the build of the pinned revision, which runs on the tip's kernel and libSystem
# (SF-V-rebase.md). The audio server, frameworks, tools and sfplay are built from the source
# ../rebase/pd.nix selects (the tip); SD_AUDIO=pin stages the pinned revision's builds of those too.
export SF_PD=pin
AUDIO_PD=${SD_AUDIO:-tip}; [ "$AUDIO_PD" = tip ] && AUDIO_PD=
N=${1:?name}
case "$N" in sd*) ;; *) echo "boot names are prefixed sd" >&2; exit 2;; esac
NB="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
KC=${SD_KC:-$("$ROOT/tools/default_kc.sh")}
[ -f "$KC/kernel" ] || { echo "no collection" >&2; exit 2; }
SPKDB=${SD_SPKDB:-22}
[ "$SPKDB" -ge 22 ] || { echo "SD_SPKDB: 22 dB below full scale or quieter" >&2; exit 2; }
A=$(cd "$HERE/sysaudio" && SF_PD=$AUDIO_PD $NB build-sysaudio.nix -A all 2>/dev/null | tail -1)
[ -x "$A/usr/sbin/coreaudiod" ] || { echo "sysaudio build failed" >&2; exit 2; }
PLAY=$(cd "$HERE" && SF_PD=$AUDIO_PD $NB build-sfplay.nix 2>/dev/null | tail -1)
T=$(cd integrations/siliconforest/m1-mesa-asahi && $NB agx-offscreen-test.nix 2>/dev/null | tail -1)
D=${SD_DESKTOP_BIN:-$ROOT/runs/m1-desktop-010/wm-darwin-gl-desktop-build-001/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop}
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
QUIET=()
QD=
FAST="sf_quiet_wait=0 sf_rtc_wait=0"; LP=integrations/siliconforest/m1-boot-speed/launcher-fast-quiet.patch
[ "${SD_WAITS:-fast}" = stock ] && { FAST=; LP=integrations/siliconforest/m1-quiet-boot/quiet-boot.patch; }
if [ "${SD_VERBOSE:-0}" != 1 ] && [ -f "$BG" ]; then
  Q=integrations/siliconforest/m1-quiet-boot
  QD=$(mktemp -d); python3 "$Q/boot_logo.py" pack "$BG" "$QD" > /dev/null
  B=integrations/siliconforest/m1-bringup
  QUIET=(PCIE_LAB_PATCH="$ROOT/$LP" PCIE_LAB_FILES="$QD/boot-logo.gz=$B/boot-logo.gz;$QD/boot-logo.json=$B/boot-logo.json")
fi
S=$(mktemp)
sed -e "s/@VOLUME@/${SD_VOLUME:-30}/g" -e "s/@TONEDB@/${SD_TONEDB:-20}/g" -e "s/@SPKDB@/$SPKDB/g" "$HERE/guest/darwin-sound-desktop.in" > "$S"
env "${QUIET[@]}" PCIE_KC="$KC" PCIE_BOOTARGS="cpus=8 $FAST sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2" PCIE_EXTRA_FILES="$X" \
  PCIE_GUEST_TEMPLATE="$S" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" 420 2>&1 |
  grep -a '"status"\|rror\|delivered\|No space' | cut -c1-240 || true
rm -rf "$S" ${QD:+"$QD"}
strings -n 6 "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" |
  grep -a 'SDDESK\|WMAUTO\|WMSTAT\|sftone\|SFSpeakers:\|SFSound: \(stopped\|jack\|CS42\)\|panic\|hw\.\(ncpu\|activecpu\)\|kern\.pd_' | cut -c1-300 | tail -90
