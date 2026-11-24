#!/bin/bash
# SPDX-License-Identifier: MIT
# One timed boot of the default desktop (issue #27, boot speed): staged as tools/gpu_desktop.sh
# stages it (Wi-Fi, GPU, display, sound, Touch Bar, the current compositor and terminal), nobody
# at the machine, a short session, then the segment table of ./bs_timeline.py.
# Queues for the bench itself.
# Usage: bs_boot.sh <name, prefix bs> [upper bound in seconds, 120..600, default 180]
#   BS_KC            kernel collection (default: ../build-kc.nix, built if it is not in the store)
#   BS_KC=stock      the "before" kernel: the default collection with a kernel that lacks
#                    ../kernel-patches/0001 (rebase/build-kc.nix with rebase/kernel.nix
#                    bootSpeed = false; built on first use). With SF_KC=pin PCIE_PIN=1: the
#                    pinned revision's collection of before the boot-speed work.
#   BS_GUEST         guest script template (default ../guest/darwin-gpu-desktop-fast.in)
#   BS_GUEST=stock   integrations/siliconforest/chonkstep-darwin/darwin-gpu-desktop-interactive.in
#   BS_SLIM=0        the RAM root as the stager makes it (default 1: ./stage_ramdisk_slim.py)
#   BS_RAMDISK_MIB   size of the slim RAM root (default 384)
#   BS_LAUNCHER=stock  the quiet boot's own launcher patch (default: ../launcher-fast-quiet.patch)
#   BS_VERBOSE=1     text boot, no launcher patch
#   BS_SECONDS       length of the desktop session (default 45)
#   BS_BOOTARGS      further kernel boot arguments
#   BS_WAITS=stock   leave the kernel's registry and clock waits as they are (default:
#                    sf_quiet_wait=0 sf_rtc_wait=0, which only ../build-kc.nix's kernel knows)
#   BS_NIX_CACHE=0   evaluate every recipe again (default: ./bs_nix_path.py's cache)
#   BS_USB_CHUNK     bytes per USB write of the RAM root (default 1048576: also times the phases)
#   BS_DESKTOP_BIN, BS_TERMINAL_BIN  as GPU_DESKTOP_BIN in tools/gpu_desktop.sh
# The whole script is one function, read completely before anything runs: the file may be
# edited while a boot of it is queued or running. No command after the last line.
main() {
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-bootspeed}
# What this script stages besides the collection comes from tools/staged.sh: built from the tip
# (SF_STAGED=pin: the builds of the pinned revision, as before the rebase; SF-V-rebase.md).
N=${1:?name}; LIMIT=${2:-180}
case "$N" in bs*) ;; *) echo "boot names are prefixed bs" >&2; exit 2;; esac
[ "$LIMIT" -ge 120 ] && [ "$LIMIT" -le 600 ] || { echo "the upper bound is 120..600 seconds" >&2; exit 2; }
DESK=${BS_SECONDS:-45}
[ "$DESK" -ge 11 ] && [ "$DESK" -le 500 ] || { echo "BS_SECONDS is 11..500" >&2; exit 2; }
T0=$(date +%s.%N)
# Store paths of the staged builds: cached while their inputs are unchanged (BS_NIX_CACHE=0: always evaluate).
NP="$HERE/tools/bs_nix_path.py"
STOCK_KC=/nix/store/wadn9g1r46g28gdhh0c4k3qcgnxxpvkr-puredarwin-kc-arm64-0.1
case "${BS_KC:-}" in
  stock) if [ "${SF_KC:-}" = pin ]; then KC=$STOCK_KC; else KC=$("$ROOT/tools/staged.sh" nix rebase build-kc.nix --arg kernel 'import ./kernel.nix { bootSpeed = false; }'); fi;;
  '') KC=$("$ROOT/tools/default_kc.sh");;
  *) KC=$BS_KC;;
esac
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
T=$("$ROOT/tools/staged.sh" mesa)
D=${BS_DESKTOP_BIN:-$ROOT/runs/m1-desktop-010/wm-darwin-scale-desktop-build-007/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop}
[ -x "$D" ] || { echo "no compositor binary at $D" >&2; exit 2; }
X="$D=/usr/bin/chonkstep-darwin-desktop"
TERM_BIN=${BS_TERMINAL_BIN:-$("$ROOT/tools/staged.sh" terminal)}
[ -x "$TERM_BIN" ] || { echo "no terminal binary at $TERM_BIN" >&2; exit 2; }
X="$X;$TERM_BIN=/usr/bin/chonk-terminal-darwin"
TI=$ROOT/integrations/siliconforest/chonkstep-darwin/terminfo/xterm-256color
[ -r "$TI" ] && X="$X;$TI=/usr/share/terminfo/x/xterm-256color;$TI=/usr/share/terminfo/78/xterm-256color"
BG=$ROOT/integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png
[ -f "$BG" ] && X="$X;$BG=/usr/share/backgrounds/siliconforest/siliconforest.png"
for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"; done
SOUND=$ROOT/integrations/siliconforest/m1-sound
A=$("$ROOT/tools/staged.sh" sysaudio)
[ -x "$A/usr/sbin/coreaudiod" ] || { echo "sysaudio build failed" >&2; exit 2; }
PLAY=$("$ROOT/tools/staged.sh" sfplay)
FW=System/Library/Frameworks
X="$X;$A/usr/sbin/coreaudiod=/usr/sbin/coreaudiod;$A/usr/bin/sftone=/usr/bin/sftone;$A/usr/bin/sfvolume=/usr/bin/sfvolume;$PLAY/usr/bin/sfplay=/usr/bin/sfplay"
for f in CoreAudio AudioToolbox AudioUnit; do X="$X;$A/$FW/$f.framework/Versions/A/$f=/$FW/$f.framework/Versions/A/$f"; done
TBFW=$ROOT/private/firmware/apple/dfrmtfw-j293.bin
[ -f "$TBFW" ] && X="$X;$TBFW=/Library/Firmware/apple/dfrmtfw-j293.bin"
QUIET=(); QD=
if [ "${BS_VERBOSE:-0}" != 1 ] && [ -f "$BG" ]; then
  Q=integrations/siliconforest/m1-quiet-boot
  QD=$(mktemp -d); python3 "$Q/boot_logo.py" pack "$BG" "$QD" > /dev/null
  B=integrations/siliconforest/m1-bringup
  LP=$HERE/launcher-fast-quiet.patch; [ "${BS_LAUNCHER:-}" = stock ] && LP=$ROOT/$Q/quiet-boot.patch
  QUIET=(PCIE_LAB_PATCH="$LP" PCIE_LAB_FILES="$QD/boot-logo.gz=$B/boot-logo.gz;$QD/boot-logo.json=$B/boot-logo.json")
fi
SLIM=()
[ "${BS_SLIM:-1}" = 1 ] && SLIM=(PCIE_STAGER="$HERE/tools/stage_ramdisk_slim.py")
G=${BS_GUEST:-$HERE/guest/darwin-gpu-desktop-fast.in}
[ "$G" = stock ] && G=$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-gpu-desktop-interactive.in
[ -f "$G" ] || { echo "no guest template at $G" >&2; exit 2; }
S=$(mktemp)
# Nobody listens: the speakers stay 20 dB under the protection model's limit.
sed -e "s/@SECONDS@/$DESK/g" -e "s/@HOLD@/1/g" -e "s/@VOLUME@/30/g" "$G" > "$S"
# The stock script has no end marker: the capture then ends when the prompt returns.
grep -q SF_BENCH_DONE "$S" && DONE=marker || DONE=prompt
R=runs/m1-desktop-010/pcie-enum-attempt-$N
WAITS="sf_quiet_wait=0 sf_rtc_wait=0"; [ "${BS_WAITS:-}" = stock ] && WAITS=
T1=$(date +%s.%N)
env "${QUIET[@]}" "${SLIM[@]}" PCIE_KC="$KC" PCIE_BOOTARGS="cpus=8 sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 $WAITS ${BS_BOOTARGS:-}" \
  PCIE_EXTRA_FILES="$X" PCIE_GUEST_TEMPLATE="$S" PCIE_DONE=$DONE PCIE_USB_CHUNK="${BS_USB_CHUNK:-1048576}" \
  PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" "$LIMIT" 2>&1 |
  grep -a '"status"\|rror\|delivered\|No space\|BENCH_TIMES' | cut -c1-240 || true
rm -rf "$S" ${QD:+"$QD"}
[ -f "$R/times.txt" ] || { echo "no boot was recorded in $R" >&2; exit 1; }
printf 'wrapper_start %s\nwrapper_staged %s\n' "$T0" "$T1" >> "$R/times.txt"
printf 'kc=%s\nguest=%s\nslim=%s\nlauncher=%s\nbootargs=%s\n' "$KC" "$G" "${BS_SLIM:-1}" "${BS_LAUNCHER:-fast}" "$WAITS ${BS_BOOTARGS:-}" > "$R/bs-config.txt"
# The controller's own phase times for this cycle: file times in its cycle directory, read only.
# The cycle is the one whose inputs were verified between this boot's cycle_begin and cycle_end.
W=/Users/chriskearney/darwin-lab-firstboot-007
BEGIN=$(awk '$1=="cycle_begin"{print $2}' "$R/times.txt"); END=$(awk '$1=="cycle_end"{print $2}' "$R/times.txt")
if [ -n "$BEGIN" ] && [ -n "$END" ]; then
  M5_HOST=10.1.1.70
  ssh -o HostKeyAlias=10.1.1.93 -o BatchMode=yes -o ConnectTimeout=2 chriskearney@10.1.1.127 true 2>/dev/null && M5_HOST=10.1.1.127
  ssh -o HostKeyAlias=10.1.1.93 -o BatchMode=yes -o ConnectTimeout=10 "chriskearney@$M5_HOST" \
    "sudo -n sh -c 'cd $W/pcie-m2-cycles && for d in \$(ls -td cycle-*/001 | head -12); do t=\$(stat -f %Fm \$d/verified-inputs.json 2>/dev/null) || continue; if awk -v t=\$t -v a=$BEGIN -v b=$END \"BEGIN{exit !(t>=a && t<=b)}\"; then find \$d -maxdepth 1 -type f ! -name \"*ioreg*\" ! -name \"source-*\" -exec stat -f \"%Fm %N\" {} + | sed \"s|\$d/||\" | sort -n; break; fi; done'" \
    > "$R/controller-times.txt" 2>/dev/null || echo "controller phase times not fetched" >&2
fi
python3 -B "$HERE/tools/bs_timeline.py" "$R"
}
main "$@"; exit $?
