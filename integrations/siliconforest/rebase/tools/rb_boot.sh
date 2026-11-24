#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the M1 with the collection and RAM root built at the rebase target, through
# tools/pcie_iterate.sh unchanged (which queues for the bench). Everything a pin boot takes
# from its defaults is named here instead; nothing shared is repointed.
# Usage: rb_boot.sh <name, prefix rb> <guest script template> [upper bound in seconds, 120..600, default 180]
#   RB_KC          collection (default: SF_PD=tip nix-build ../build-kc.nix)
#   RB_CPUS        guest processors (default 1)
#   RB_BOOTARGS    further kernel boot arguments (sfnvme is refused)
#   RB_EXTRA       further files, "source=/destination;..." (PCIE_EXTRA_FILES)
#   RB_QUIET=1     the quiet fast boot: boot picture, m1-boot-speed/launcher-fast-quiet.patch,
#                  sf_quiet_wait=0 sf_rtc_wait=0 (default: text boot, no launcher patch)
#   RB_QUIET=stock the quiet boot with the kernel's own waits (m1-quiet-boot/quiet-boot.patch), as the
#                  suites that predate the fast boot run it
#   RB_DONE        marker or prompt, as PCIE_DONE (default: marker when the template prints SF_BENCH_DONE)
#   RB_SED         further sed expressions for the template (e.g. "-e s/@SECONDS@/45/g")
#   RB_STAGE=desktop  also stage what tools/gpu_desktop.sh stages: the compositor
#                  (wm-darwin-scale-desktop-build-009) and terminal (terminal-application-build-008),
#                  terminfo, the desk picture, Mesa, the audio server with its frameworks and tools,
#                  sfplay, the Touch Bar firmware. All of these are the pin's builds, the same files a
#                  pin boot stages (RB_DESKTOP_BIN, RB_TERMINAL_BIN: other binaries).
# Every boot gets /usr/sbin/ioreg (the tip's) and the guest prologue guest/rb-prologue.sh,
# which stops the session at once when a driver is attached to the NVMe controller's nub.
main() {
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=rebase
N=${1:?name}; G=${2:?guest script template}; LIMIT=${3:-180}
case "$N" in rb[0-9]*) ;; *) echo "boot names are rb<number>" >&2; exit 2;; esac
[ "$LIMIT" -ge 120 ] && [ "$LIMIT" -le 600 ] || { echo "the upper bound is 120..600 seconds" >&2; exit 2; }
[ -f "$G" ] || { echo "no guest template at $G" >&2; exit 2; }
case " ${RB_BOOTARGS:-} " in *sfnvme*) echo "sfnvme is never passed by this stream" >&2; exit 2;; esac
[ ! -e "runs/m1-desktop-010/pcie-enum-attempt-$N" ] || { echo "boot name $N is used" >&2; exit 2; }
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
KC=${RB_KC:-$(cd "$HERE" && SF_PD=tip $NIX build-kc.nix | tail -1)}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
IOREG=$(cd "$HERE" && SF_PD=tip $NIX -E '(import ./pd.nix).pd.packages.x86_64-linux.ioreg-arm64' | tail -1)
[ -x "$IOREG/usr/sbin/ioreg" ] || { echo "no ioreg build" >&2; exit 2; }
X="$IOREG/usr/sbin/ioreg=/usr/sbin/ioreg;$HERE/guest/rb-prologue.sh=/usr/bin/rb-prologue${RB_EXTRA:+;$RB_EXTRA}"
if [ "${RB_STAGE:-}" = desktop ]; then
  # the pin's builds, by the pin's recipes (no SF_PD), as tools/gpu_desktop.sh finds them
  NP=integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py
  T=$(env -u SF_PD $NP m1-mesa-asahi agx-offscreen-test.nix)
  A=$(env -u SF_PD $NP m1-sound/sysaudio build-sysaudio.nix -A all)
  PLAY=$(env -u SF_PD $NP m1-sound build-sfplay.nix)
  D=${RB_DESKTOP_BIN:-$ROOT/runs/m1-desktop-010/wm-darwin-scale-desktop-build-009/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop}
  TB=${RB_TERMINAL_BIN:-$ROOT/runs/m1-desktop-010/terminal-application-build-008/target/aarch64-apple-darwin/release/chonk-terminal-darwin}
  [ -x "$D" ] && [ -x "$TB" ] && [ -x "$A/usr/sbin/coreaudiod" ] || { echo "desktop binaries missing" >&2; exit 2; }
  TI=$ROOT/integrations/siliconforest/chonkstep-darwin/terminfo/xterm-256color
  BGP=$ROOT/integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png
  FW=System/Library/Frameworks
  X="$X;$D=/usr/bin/chonkstep-darwin-desktop;$TB=/usr/bin/chonk-terminal-darwin"
  X="$X;$TI=/usr/share/terminfo/x/xterm-256color;$TI=/usr/share/terminfo/78/xterm-256color;$BGP=/usr/share/backgrounds/siliconforest/siliconforest.png"
  for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"; done
  X="$X;$A/usr/sbin/coreaudiod=/usr/sbin/coreaudiod;$A/usr/bin/sftone=/usr/bin/sftone;$A/usr/bin/sfvolume=/usr/bin/sfvolume;$PLAY/usr/bin/sfplay=/usr/bin/sfplay"
  for f in CoreAudio AudioToolbox AudioUnit; do X="$X;$A/$FW/$f.framework/Versions/A/$f=/$FW/$f.framework/Versions/A/$f"; done
  TBFW=$ROOT/private/firmware/apple/dfrmtfw-j293.bin
  [ -f "$TBFW" ] && X="$X;$TBFW=/Library/Firmware/apple/dfrmtfw-j293.bin"
fi
ARGS="cpus=${RB_CPUS:-1} ${RB_BOOTARGS:-}"
LAB=(); QD=
if [ "${RB_QUIET:-0}" != 0 ]; then
  BG=integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png
  QD=$(mktemp -d); python3 integrations/siliconforest/m1-quiet-boot/boot_logo.py pack "$BG" "$QD" > /dev/null
  B=integrations/siliconforest/m1-bringup
  LP=m1-boot-speed/launcher-fast-quiet.patch; [ "$RB_QUIET" = stock ] && LP=m1-quiet-boot/quiet-boot.patch
  LAB=(PCIE_LAB_PATCH="$HERE/../$LP" PCIE_LAB_FILES="$QD/boot-logo.gz=$B/boot-logo.gz;$QD/boot-logo.json=$B/boot-logo.json")
  [ "$RB_QUIET" = stock ] || ARGS="$ARGS sf_quiet_wait=0 sf_rtc_wait=0"
fi
# The template with the prologue as its first command after the shebang line.
S=$(mktemp)
{ head -1 "$G"; echo '. /usr/bin/rb-prologue'; tail -n +2 "$G"; } | sed ${RB_SED:--e s/@RB@/1/g} > "$S"
# A script without the end marker ends the capture when the prompt returns.
DONE=${RB_DONE:-$(grep -q SF_BENCH_DONE "$G" && echo marker || echo prompt)}
R=runs/m1-desktop-010/pcie-enum-attempt-$N
env -u PCIE_CHAIN -u SF_PD ${LAB[@]+"${LAB[@]}"} PCIE_STAGER="$HERE/../m1-boot-speed/tools/stage_ramdisk_slim.py" PCIE_KC="$KC" PCIE_BOOTARGS="$ARGS" \
  PCIE_EXTRA_FILES="$X" PCIE_GUEST_TEMPLATE="$S" PCIE_DONE="$DONE" \
  PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" "$LIMIT" 2>&1 |
  grep -a '"status"\|rror\|delivered\|No space\|BENCH_TIMES\|REQUIRES_REVIEW\|not booting' | cut -c1-240 || true
rm -rf "$S" ${QD:+"$QD"}
[ -f "$R/result.json" ] || { echo "no result in $R" >&2; exit 1; }
printf 'kc=%s\nguest=%s\nbootargs=%s\nquiet=%s\n' "$KC" "$G" "$ARGS" "${RB_QUIET:-0}" > "$R/rb-config.txt"
python3 -c "import json,sys; r=json.load(open('$R/result.json')); print('RB_RESULT', '$N', r.get('status'))"
strings -n 4 "$R/guest/console.bin" | grep -a 'RB_NVME\|RB_STOP\|panic(' | cut -c1-200 | head -12 || true
}
main "$@"; exit $?
