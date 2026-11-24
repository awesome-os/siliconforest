#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the native-boot stream (docs/roadmap/issues/SF-T-native-boot.md), RAM-loaded
# over USB like every other boot. Queues for the bench itself.
# Usage: nb_boot.sh <name, prefix nb> [upper bound in seconds, 120..600, default 175]
#   NB_MODE=probe    the hypervisor boot, with the controller listening on the M1's real UART
#   NB_MODE=native   no hypervisor (../native_boot.py); needs NB_RETURN
#   NB_MODE=hv       the hypervisor boot with nothing added (for NB_REBOOT=1 and the reboot/
#                    collection: ../reboot/build-kc.nix)
#   NB_REBOOT=1      the guest script ends by restarting the M1 itself (`reboot`, through the
#                    SMC; needs the collection of ../reboot/build-kc.nix as NB_KC). The
#                    launcher gets ../reboot/launcher-self-reboot.diff on top of its patch
#                    and self_reboot_mode.txt: no controller reset when the M1's Linux comes
#                    back by itself. Never for power-off: a powered-off M1 needs a person.
#   NB_RETURN=vdm    native: the controller resets the M1 with its usual `macvdmtool reboot
#                    serial` when the capture is over (launcher-native.patch, word vdm-return)
#   NB_WDT=<s>       native: also arm the SoC watchdog for <s> seconds (30..170) before the
#                    kernel starts; with NB_RETURN unset it is the only way back, the capture
#                    then always runs the whole upper bound, which must be NB_WDT + 45 or more
#   NB_PROFILE=shell (default) one processor, no GPU or display arguments, ../guest-shell.in
#   NB_PROFILE=desktop  the default desktop as m1-boot-speed/tools/bs_boot.sh boots it
#   NB_CPUS          processors (default 1 for shell, 8 for desktop)
#   NB_SECONDS       desktop session length (default 30)
#   NB_KC            kernel collection (default: the default desktop collection; more than one
#                    processor natively needs ../build-kc.nix)
#   NB_SESSION=launchd  native: multi-user boot, the guest script started by launchd
#                    (../guest/org.siliconforest.desktop.plist) instead of typed; no challenge
#   NB_GUEST         shell profile: another guest script template (default ../guest-shell.in)
#   NB_EXTRA         further files for the RAM root, "source=/destination;..."
#   NB_BOOTARGS      further boot arguments;  NB_WORDS  further words for native_mode.txt
# The whole script is one function, read completely before anything runs.
main() {
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-native}
# What this script stages from Nix besides the collection (tools, libraries, the audio server) are still
# the builds of the pinned revision, which run on the tip's kernel and libSystem (SF-V-rebase.md).
export SF_PD=pin
N=${1:?name}; LIMIT=${2:-175}
case "$N" in nb*) ;; *) echo "boot names are prefixed nb" >&2; exit 2;; esac
[ "$LIMIT" -ge 120 ] && [ "$LIMIT" -le 600 ] || { echo "the upper bound is 120..600 seconds" >&2; exit 2; }
[ ! -e "runs/m1-desktop-010/pcie-enum-attempt-$N" ] || { echo "boot $N exists" >&2; exit 2; }
[ -z "${PCIE_CHAIN:-}" ] || { echo "PCIE_CHAIN is not used in this stream" >&2; exit 2; }
MODE=${NB_MODE:?NB_MODE=probe or native}; PROFILE=${NB_PROFILE:-shell}; DONE=marker
case "$MODE" in
  probe) WORDS=probe;;
  hv) WORDS=;;
  native)
    WORDS=native
    [ "${NB_RETURN:-}" = vdm ] && WORDS="$WORDS vdm-return"
    if [ -n "${NB_WDT:-}" ]; then
      WORDS="$WORDS wdt=$NB_WDT"
      if [ "${NB_RETURN:-}" != vdm ]; then
        [ "$LIMIT" -ge $((NB_WDT + 45)) ] || { echo "without NB_RETURN=vdm the upper bound must be NB_WDT + 45 or more" >&2; exit 2; }
        DONE=off
      fi
    fi
    [ "${NB_RETURN:-}" = vdm ] || [ -n "${NB_WDT:-}" ] || { echo "a native boot needs NB_RETURN=vdm, NB_WDT=<s>, or both" >&2; exit 2; };;
  *) echo "NB_MODE is probe, hv or native" >&2; exit 2;;
esac
WORDS="$WORDS ${NB_WORDS:-}"
PATCH="$HERE/launcher-native.patch"
if [ "${NB_REBOOT:-}" = 1 ]; then
  [ -n "${NB_KC:-}" ] || { echo "NB_REBOOT=1 needs NB_KC (the collection of reboot/build-kc.nix)" >&2; exit 2; }
  case " ${NB_BOOTARGS:-} " in *" sfreboot=0 "*) echo "NB_REBOOT=1 with sfreboot=0 cannot restart" >&2; exit 2;; esac
fi
[ "${NB_SESSION:-}" = launchd ] && { [ "$MODE" = native ] || { echo "NB_SESSION=launchd is for native boots" >&2; exit 2; }; WORDS="$WORDS multiuser"; }
KC=${NB_KC:-$("$ROOT/tools/default_kc.sh" native)}
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
B=integrations/siliconforest/m1-bringup
QD=$(mktemp -d); S=$(mktemp); trap 'rm -rf "$QD" "$S"' EXIT
echo "$WORDS" > "$QD/native_mode.txt"
python3 -B -c "import sys; sys.path.insert(0, '$HERE'); import native_boot; native_boot.Mode(open('$QD/native_mode.txt').read())"
FILES="$HERE/native_boot.py=$B/native_boot.py;$HERE/native_stub.py=$B/native_stub.py;$QD/native_mode.txt=$B/native_mode.txt"
case " $WORDS " in *" rechain "*) FILES="$FILES;$HERE/b1/native_stage.py=$B/native_stage.py;$HERE/b1/pdsf_pack.py=$B/pdsf_pack.py";; esac
if [ "${NB_REBOOT:-}" = 1 ]; then
  PATCH="$QD/launcher-native-self-reboot.patch"
  "$HERE/reboot/combine_patch.sh" "$PATCH"
  echo "the guest of boot $N restarts the M1 itself" > "$QD/self_reboot_mode.txt"
  FILES="$FILES;$QD/self_reboot_mode.txt=$B/self_reboot_mode.txt"
fi
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
  if [ -f "$BG" ]; then
    python3 integrations/siliconforest/m1-quiet-boot/boot_logo.py pack "$BG" "$QD" > /dev/null
    FILES="$FILES;$QD/boot-logo.gz=$B/boot-logo.gz;$QD/boot-logo.json=$B/boot-logo.json"
  fi
  # Nobody listens: the speakers stay 20 dB under the protection model's limit.
  sed -e "s/@SECONDS@/${NB_SECONDS:-30}/g" -e "s/@HOLD@/1/g" -e "s/@VOLUME@/30/g" integrations/siliconforest/m1-boot-speed/guest/darwin-gpu-desktop-fast.in > "$S"
  ARGS="cpus=${NB_CPUS:-8} sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 $WAITS ${NB_BOOTARGS:-}"
else
  cp "${NB_GUEST:-$HERE/guest-shell.in}" "$S"
  ARGS="cpus=${NB_CPUS:-1} $WAITS ${NB_BOOTARGS:-}"
fi
[ "${NB_SESSION:-}" = launchd ] && X="${X:+$X;}$HERE/guest/org.siliconforest.desktop.plist=/System/Library/LaunchDaemons/org.siliconforest.desktop.plist"
# NB_REBOOT=1: announce on the console, then restart. The script's own end marker goes:
# stopping the capture there would, under the hypervisor, stop the guest before it restarts.
# The capture ends when the M1 resets (hypervisor: m1n1's USB device disappears) or ten
# seconds after the announcement (native), and at the upper bound if nothing happens.
if [ "${NB_REBOOT:-}" = 1 ]; then
  [ "$(tail -n 1 "$S")" = "printf '\\nPD_BENCH_DONE\\n'" ] || { echo "the guest script does not end with the end marker" >&2; exit 2; }
  sed -i '$ d' "$S"
  printf '%s\n' "printf '\\nPD_BENCH_REBOOT\\n'; reboot; printf 'SFNB_REBOOT_RC=%s\\n' \"\$?\"; sleep 60" >> "$S"
fi
[ -n "${NB_EXTRA:-}" ] && X="${X:+$X;}$NB_EXTRA"
# More than one processor natively: m1n1 starts the secondary cores and the EL2 page holds
# them; the kernel releases them only with sfcpuheld=1 (./build-kc.nix has the platform kext
# that knows it).
case "$MODE:$ARGS" in native:cpus=1\ *) ;; native:*) ARGS="$ARGS sfcpuheld=1";; esac
R=runs/m1-desktop-010/pcie-enum-attempt-$N
env PCIE_LAB_PATCH="$PATCH" PCIE_LAB_FILES="$FILES" \
  PCIE_STAGER="$ROOT/integrations/siliconforest/m1-boot-speed/tools/stage_ramdisk_slim.py" \
  PCIE_KC="$KC" PCIE_BOOTARGS="$ARGS" PCIE_EXTRA_FILES="$X" PCIE_GUEST_TEMPLATE="$S" PCIE_DONE=$DONE \
  PCIE_USB_CHUNK=1048576 PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "$LIMIT" 2>&1 | grep -a '"status"\|rror\|delivered\|No space\|BENCH_TIMES' | cut -c1-240 || true
[ -f "$R/times.txt" ] || { echo "no boot was recorded in $R" >&2; exit 1; }
printf 'mode=%s\nprofile=%s\nkc=%s\nbootargs=%s\nlimit=%s\n' "$WORDS" "$PROFILE" "$KC" "$ARGS" "$LIMIT" > "$R/nb-config.txt"
cat "$R/result.json" 2>/dev/null || true
grep -a 'NATIVE\|UART_TAP\|UART_MARKER\|GUEST_CPUS' "$R/guest/worker.log" | cut -c1-400 || true
grep -a -c '^UART> ' "$R/guest/worker.log" | sed 's/^/UART lines in worker.log: /' || true
strings -n 4 "$R/guest/console.bin" | grep -a 'SFNB\|panic\|Darwin Kernel\|DL9_\|GPUDESK\|WMSTAT\|SFReboot\|SF_BENCH_REBOOT\|MACH Reboot' | cut -c1-240 | head -60 || true
}
main "$@"; exit $?
