#!/bin/bash
# SPDX-License-Identifier: MIT
# One RAM boot through the SiliconForest stage (../DESIGN.md): the launcher uploads one image
# (the stage m1n1 + configuration + collection + RAM root), vectors m1n1 into it and then only
# listens on the UART; the stage boots the kernel by itself. RAM only, like every other boot:
# nothing is installed. Queues for the bench itself (tools/pcie_iterate.sh).
# Usage: sf_stage_boot.sh <name, prefix sfb> [upper bound in seconds, 120..600, default 175]
#   SF_PROFILE=shell (default)  ../../guest-shell.in, no GPU or display arguments
#   SF_PROFILE=desktop          the default desktop, as ../../tools/nb_boot.sh boots it
#   SF_CPUS       processors (default 1 for shell, 8 for desktop)
#   SF_SECONDS    desktop session length (default 30)
#   SF_KC         kernel collection (default: ../../build-kc.nix's, which knows sfcpuheld=1)
#   SF_WORDS      further words for native_mode.txt (default "vdm-serial noiolog"); stage words:
#                 usbkeep (keep the debug USB port's device-tree nodes), hostseed (boot seed
#                 from the controller instead of the SEP), wdt=<s>, nosprr, noamx
#   SF_SESSION=launchd  multi-user boot, the guest script started by launchd
#                 (../../guest/org.siliconforest.desktop.plist) instead of typed; no challenge,
#                 the result is NO_COMMAND_LINE by design (as NB_SESSION of ../../tools/nb_boot.sh)
#   SF_BOOTARGS   further boot arguments
#   SF_STAGE      the stage image (default ../out/m1n1-sf-stage.bin, built by ../build.sh)
# The whole script is one function, read completely before anything runs.
main() {
set -euo pipefail
B1=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); HERE=$(cd "$B1/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-stage}
# What this script stages from Nix besides the collection (tools, libraries, the audio server) are still
# the builds of the pinned revision, which run on the tip's kernel and libSystem (SF-V-rebase.md).
export SF_PD=pin
N=${1:?name}; LIMIT=${2:-175}
case "$N" in sfb*) ;; *) echo "boot names are prefixed sfb" >&2; exit 2;; esac
[ "$LIMIT" -ge 120 ] && [ "$LIMIT" -le 600 ] || { echo "the upper bound is 120..600 seconds" >&2; exit 2; }
[ ! -e "runs/m1-desktop-010/pcie-enum-attempt-$N" ] || { echo "boot $N exists" >&2; exit 2; }
[ -z "${PCIE_CHAIN:-}" ] || { echo "PCIE_CHAIN is not used in this stream" >&2; exit 2; }
case " ${SF_BOOTARGS:-} " in *sfnvme*) echo "the internal NVMe driver stays off" >&2; exit 2;; esac
PROFILE=${SF_PROFILE:-shell}
STAGE=${SF_STAGE:-$B1/out/m1n1-sf-stage.bin}
[ -f "$STAGE" ] || { echo "no stage image at $STAGE: run $B1/build.sh" >&2; exit 2; }
WORDS="native stage vdm-return ${SF_WORDS-vdm-serial noiolog}"
[ "${SF_SESSION:-}" = launchd ] && WORDS="$WORDS multiuser"
KC=${SF_KC:-$("$ROOT/tools/default_kc.sh" native)}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
B=integrations/siliconforest/m1-bringup
QD=$(mktemp -d); S=$(mktemp); trap 'rm -rf "$QD" "$S"' EXIT
echo "$WORDS" > "$QD/native_mode.txt"
python3 -B -c "import sys; sys.path.insert(0, '$HERE'); import native_boot; m = native_boot.Mode(open('$QD/native_mode.txt').read()); assert m.stage"
FILES="$HERE/native_boot.py=$B/native_boot.py;$HERE/native_stub.py=$B/native_stub.py;$QD/native_mode.txt=$B/native_mode.txt"
FILES="$FILES;$B1/native_stage.py=$B/native_stage.py;$B1/pdsf_pack.py=$B/pdsf_pack.py;$STAGE=$B/m1n1-sf-stage.bin"
WAITS="sf_quiet_wait=0 sf_rtc_wait=0"; X=
if [ "$PROFILE" = desktop ]; then
  NP=integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py
  T=$($NP m1-mesa-asahi agx-offscreen-test.nix)
  D=$ROOT/runs/m1-desktop-010/wm-darwin-scale-desktop-build-009/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop
  TERM_BIN=$ROOT/runs/m1-desktop-010/terminal-application-build-008/target/aarch64-apple-darwin/release/chonk-terminal-darwin
  [ -x "$D" ] && [ -x "$TERM_BIN" ] || { echo "no compositor or terminal binary" >&2; exit 2; }
  X="$D=/usr/bin/chonkstep-darwin-desktop;$TERM_BIN=/usr/bin/chonk-terminal-darwin"
  TI=$ROOT/integrations/siliconforest/chonkstep-darwin/terminfo/xterm-256color
  [ -r "$TI" ] && X="$X;$TI=/usr/share/terminfo/x/xterm-256color;$TI=/usr/share/terminfo/78/xterm-256color"
  BG=$ROOT/integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png
  [ -f "$BG" ] && X="$X;$BG=/usr/share/backgrounds/siliconforest/siliconforest.png"
  for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"; done
  A=$($NP m1-sound/sysaudio build-sysaudio.nix -A all); PLAY=$($NP m1-sound build-sfplay.nix)
  [ -x "$A/usr/sbin/coreaudiod" ] || { echo "sysaudio build failed" >&2; exit 2; }
  FW=System/Library/Frameworks
  X="$X;$A/usr/sbin/coreaudiod=/usr/sbin/coreaudiod;$A/usr/bin/sftone=/usr/bin/sftone;$A/usr/bin/sfvolume=/usr/bin/sfvolume;$PLAY/usr/bin/sfplay=/usr/bin/sfplay"
  for f in CoreAudio AudioToolbox AudioUnit; do X="$X;$A/$FW/$f.framework/Versions/A/$f=/$FW/$f.framework/Versions/A/$f"; done
  TBFW=$ROOT/private/firmware/apple/dfrmtfw-j293.bin
  [ -f "$TBFW" ] && X="$X;$TBFW=/Library/Firmware/apple/dfrmtfw-j293.bin"
  # Nobody listens: the speakers stay 20 dB under the protection model's limit.
  sed -e "s/@SECONDS@/${SF_SECONDS:-30}/g" -e "s/@HOLD@/1/g" -e "s/@VOLUME@/30/g" integrations/siliconforest/m1-boot-speed/guest/darwin-gpu-desktop-fast.in > "$S"
  ARGS="cpus=${SF_CPUS:-8} sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 $WAITS ${SF_BOOTARGS:-}"
else
  cp "$HERE/guest-shell.in" "$S"
  ARGS="cpus=${SF_CPUS:-1} $WAITS ${SF_BOOTARGS:-}"
fi
[ "${SF_SESSION:-}" = launchd ] && X="${X:+$X;}$HERE/guest/org.siliconforest.desktop.plist=/System/Library/LaunchDaemons/org.siliconforest.desktop.plist"
# More than one processor: the stage starts the secondary cores and parks them in the EL2
# page; the kernel releases them only with sfcpuheld=1.
case "$ARGS" in cpus=1\ *) ;; *) ARGS="$ARGS sfcpuheld=1";; esac
R=runs/m1-desktop-010/pcie-enum-attempt-$N
env PCIE_LAB_PATCH="$HERE/launcher-native.patch" PCIE_LAB_FILES="$FILES" \
  PCIE_STAGER="$ROOT/integrations/siliconforest/m1-boot-speed/tools/stage_ramdisk_slim.py" \
  PCIE_KC="$KC" PCIE_BOOTARGS="$ARGS" PCIE_EXTRA_FILES="$X" PCIE_GUEST_TEMPLATE="$S" PCIE_DONE=marker \
  PCIE_USB_CHUNK=1048576 PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "$LIMIT" 2>&1 | grep -a '"status"\|rror\|delivered\|No space\|BENCH_TIMES' | cut -c1-240 || true
[ -f "$R/times.txt" ] || { echo "no boot was recorded in $R" >&2; exit 1; }
printf 'mode=%s\nprofile=%s\nkc=%s\nbootargs=%s\nlimit=%s\nstage=%s\n' "$WORDS" "$PROFILE" "$KC" "$ARGS" "$LIMIT" "$(sha256sum "$STAGE" | cut -d' ' -f1)" > "$R/sf-config.txt"
cat "$R/result.json" 2>/dev/null || true
grep -a 'NATIVE\|BQ_T stage\|BQ_T native\|Error\|Traceback' "$R/guest/worker.log" | cut -c1-900 || true
# without lowmem the stage must enter the kernel exactly where the launcher would have
grep -a -o '"expected": {[^}]*}' "$R/guest/worker.log" | head -1 || true
# the stage's own lines, the EL2 page's, then the kernel's
strings -n 4 "$R/guest/console.bin" | grep -a 'PDSF\|SFNB\|m1n1 \|Checking for payloads\|No valid payload\|Running proxy\|Starting CPU\|panic\|Darwin Kernel\|DL9_\|GPUDESK\|WMSTAT' | cut -c1-240 | head -80 || true
}
main "$@"; exit $?
