#!/bin/bash
# Boot the GPU desktop on the M1 for a person to use: Wi-Fi + GPU + display drivers, the
# compositor on GL with page flips, real keyboard and trackpad, two terminals.
# Usage: tools/gpu_desktop.sh <new-name> [minutes, default 30, max 235]
# End early from this machine: tools/gpu_desktop_stop.sh   (the M1 returns to Linux in about a minute)
# Quiet boot (default): the desk picture is put on the panel before the guest starts and the
# kernel draws nothing on it (graphics boot, console on the UART only), so the picture stays
# until the compositor's first frame. The serial log is the same as before.
#   GPU_DESKTOP_VERBOSE=1       the text boot: kernel log on the panel as well as on serial
#   GPU_DESKTOP_SPLASH=<png>    another boot picture (default: the desk picture)
# Sound and the Touch Bar are on: coreaudiod runs, F10-F12 on the strip are mute and volume.
#   GPU_DESKTOP_SPEAKER_HOLD=1  keep the speakers 20 dB under the protection model's limit
#   GPU_DESKTOP_VOLUME=<n>      headphone attenuation at start (default 30)
# The session also ends by itself after the given time, and the M1 is returned to Linux.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"; export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; MIN=${2:-30}
[ "$MIN" -ge 2 ] && [ "$MIN" -le 235 ] || { echo "minutes must be 2..235" >&2; exit 2; }
[ "${GPU_DESKTOP_VERBOSE:-0}" = 1 ] && BOOT=140 || BOOT=20                                    # seconds from hypervisor start to the guest script (measured 5.7; was 140 for 66.6)
RUNTIME=$((MIN * 60 + BOOT)); DESK=$((MIN * 60))
# The default collection: the desktop collection with sound and the Touch Bar and the kernel
# that does not wait a minute for a clock and a daemon this system does not have
# (m1-boot-speed/build-kc.nix: m1-touchbar/build-kc.nix with m1-boot-speed/kernel.nix; boots bs02, bs03).
# It needs sf_quiet_wait=0 sf_rtc_wait=0 in the boot-args and the launcher patch below (which waits
# for a silent console before it types): without the patch the harness fails its console proof.
# Since 12 November 2026 it is built from upstream's tip (tools/default_kc.sh, SF-V-rebase.md), with
# the RAM root made from the tip's image. GPU_DESKTOP_PIN=1: the last collection and the RAM root
# of the pinned revision e7aa00a6, as this script booted them before.
PIN=${GPU_DESKTOP_PIN:-0}
[ "$PIN" = 1 ] && export SF_KC=pin PCIE_PIN=1 SF_STAGED=pin
KC=${GPU_DESKTOP_KC:-$(tools/default_kc.sh)}
# What is staged below (Mesa, the compositor, the terminal, the audio server and its frameworks,
# sfplay) comes from tools/staged.sh: built from the tip against its SDK and libSystem, except
# what that script still takes from the pinned revision. SF_STAGED=pin (set by GPU_DESKTOP_PIN=1):
# everything as it was staged before the rebase. Store paths are remembered while the recipes
# are unchanged (BS_NIX_CACHE=0 evaluates always).
[ -f "$KC/kernel" ] || { echo "no collection at $KC" >&2; exit 2; }
T=$(tools/staged.sh mesa)
# The compositor with display scaling, the settings menu, volume keys and the Touch Bar phase (wm-darwin merge, boot ds06).
D=${GPU_DESKTOP_BIN:-$(tools/staged.sh compositor)}; D=${D#$ROOT/}
[ -x "$D" ] || { echo "no compositor binary at $D" >&2; exit 2; }
X="$ROOT/$D=/usr/bin/chonkstep-darwin-desktop"
# The terminal that accepts lifetimes above 120 s (build-003: --seconds up to 14400, 8 MiB of
# output and 1 MiB of input per terminal). The desktop passes each terminal the session's
# remaining time, which the RAM root's older terminal refuses.
# build-004 added the xterm emulation (full-screen programs, scrollback, TERM=xterm-256color),
# build-005 selection by dragging and copy/paste.
TERM_BIN=${GPU_DESKTOP_TERMINAL:-$(tools/staged.sh terminal)}; TERM_BIN=${TERM_BIN#$ROOT/}
[ -x "$TERM_BIN" ] || { echo "no terminal binary at $TERM_BIN" >&2; exit 2; }
X="$X;$ROOT/$TERM_BIN=/usr/bin/chonk-terminal-darwin"
# The terminfo entry for that TERM; the RAM root has none.
TI=integrations/siliconforest/chonkstep-darwin/terminfo/xterm-256color
[ -r "$TI" ] && X="$X;$ROOT/$TI=/usr/share/terminfo/x/xterm-256color;$ROOT/$TI=/usr/share/terminfo/78/xterm-256color"
# The desk picture; the guest script points CHONKSTEP_BACKGROUND_DIR at its directory.
BG=${GPU_DESKTOP_BACKGROUND:-integrations/siliconforest/chonkstep-darwin/backgrounds/siliconforest.png}
[ -f "$BG" ] && X="$X;$ROOT/$BG=/usr/share/backgrounds/siliconforest/siliconforest.png"
for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"; done
# Sound: the audio server, its frameworks and the tools (as m1-sound/tools/sd_desktop_boot.sh).
SOUND=integrations/siliconforest/m1-sound
A=$(tools/staged.sh sysaudio)
[ -x "$A/usr/sbin/coreaudiod" ] || { echo "sysaudio build failed" >&2; exit 2; }
PLAY=$(tools/staged.sh sfplay)
FW=System/Library/Frameworks
X="$X;$A/usr/sbin/coreaudiod=/usr/sbin/coreaudiod;$A/usr/bin/sftone=/usr/bin/sftone;$A/usr/bin/sfvolume=/usr/bin/sfvolume;$PLAY/usr/bin/sfplay=/usr/bin/sfplay"
for f in CoreAudio AudioToolbox AudioUnit; do X="$X;$A/$FW/$f.framework/Versions/A/$f=/$FW/$f.framework/Versions/A/$f"; done
# The Touch Bar digitizer's firmware (this machine's own, never committed); without it the
# strip shows its keys but takes no touches.
TBFW=private/firmware/apple/dfrmtfw-j293.bin
[ -f "$TBFW" ] && X="$X;$ROOT/$TBFW=/Library/Firmware/apple/dfrmtfw-j293.bin" || echo "no Touch Bar firmware at $TBFW: the strip will not take touches" >&2
# Speakers are unrestricted: the kernel protection model alone sets the limit (owner decision,
# 2026-11-11; no person has listened yet). GPU_DESKTOP_SPEAKER_HOLD=1 keeps them 20 dB under it.
HOLD=${GPU_DESKTOP_SPEAKER_HOLD:-0}; VOL=${GPU_DESKTOP_VOLUME:-30}
QUIET=()
# The kernel's two boot waits are switched off only together with the launcher patch below (it
# types the console challenge once the console is silent); the text boot keeps the old waits.
FASTARGS=
if [ "${GPU_DESKTOP_VERBOSE:-0}" != 1 ]; then
  FASTARGS="sf_quiet_wait=0 sf_rtc_wait=0"
  # The launcher patch (graphics boot, no mirror, paints boot-logo.gz) and the picture in the
  # framebuffer's format, both for this boot's private copy of the launcher tree only.
  Q=integrations/siliconforest/m1-quiet-boot; SPLASH=${GPU_DESKTOP_SPLASH:-$BG}
  [ -f "$SPLASH" ] || { echo "no boot picture at $SPLASH" >&2; exit 2; }
  QD=$(mktemp -d); python3 "$Q/boot_logo.py" pack "$SPLASH" "$QD" > /dev/null
  B=integrations/siliconforest/m1-bringup
  # the quiet boot's launcher patch, with the boot picture painted in 0.2 s instead of 4.7 s and
  # the challenge typed only once the console is silent
  QUIET=(PCIE_LAB_PATCH="$ROOT/integrations/siliconforest/m1-boot-speed/launcher-fast-quiet.patch" PCIE_LAB_FILES="$QD/boot-logo.gz=$B/boot-logo.gz;$QD/boot-logo.json=$B/boot-logo.json")
fi
S=$(mktemp); sed -e "s/@SECONDS@/$DESK/g" -e "s/@HOLD@/$HOLD/g" -e "s/@VOLUME@/$VOL/g" integrations/siliconforest/chonkstep-darwin/darwin-gpu-desktop-interactive.in > "$S"
echo "GPU desktop: $MIN minutes; the desktop appears about 50 seconds after the M1 reboots"
# The RAM root without the unused kernel archive, 384 MiB, free blocks zeroed: 92 MB on the wire instead of 150.
env "${QUIET[@]}" PCIE_STAGER="$ROOT/integrations/siliconforest/m1-boot-speed/tools/stage_ramdisk_slim.py" PCIE_KC="$KC" PCIE_BOOTARGS="cpus=${GPU_DESKTOP_CPUS:-8} sfgpudt=2 sfgpudt_stringprops=1 sfagx=1 sfdispdt=2 sfdcp=2 $FASTARGS" PCIE_EXTRA_FILES="$X" PCIE_GUEST_TEMPLATE="$S" \
PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 LINES_MAX=1 tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 | grep -a '"status"\|rror\|delivered' | cut -c1-240
rm -rf "$S" ${QD:+"$QD"}
strings -n 6 "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" | grep -a 'GPUDESK\|WMSTAT\|panic\|SFSpeakers:\|SFTouchBar:' | cut -c1-260 | tail -40
