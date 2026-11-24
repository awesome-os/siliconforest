#!/bin/bash
# SPDX-License-Identifier: MIT
# The one place that says which build of a staged user-space piece a boot gets: what the
# wrappers put into the RAM root besides the kernel collection (tools/default_kc.sh) and the
# base image. Since the rebase every piece is built from upstream's tip (rebase/pd.nix)
# against the tip's SDK and the lab's default libSystem (PIN_ONLY below is empty).
#
# Usage: tools/staged.sh <piece> [nix-build arguments]      prints a store path or a file
#   mesa        Mesa/AGX bundle: EGL, GLES, gallium, libdrm, the off-screen tests (m1-mesa-asahi/agx-offscreen-test.nix)
#   mesa-wl     the Wayland GL demo bundle (m1-mesa-asahi/agx-wl-demo.nix)
#   dcp-flip    the display flip test (m1-dcp/dcp-flip-test.nix)
#   drm-storm   the DRM stress tool (m1-smp-audit/drm-storm.nix)
#   sysaudio    the audio server, its frameworks, sftone, sfvolume (m1-sound/sysaudio/build-sysaudio.nix -A all)
#   sfplay      the WAVE player on the kernel device (m1-sound/build-sfplay.nix)
#   sftbtest    the Touch Bar test tool (m1-touchbar/build-sftbtest.nix)
#   netd        sf-netd, sf-dhcp, sf-net-up and the launchd job (m1-bwfm/netd/build-pd-netd.nix)
#   avd-user    the video decoder's user tools (m1-avd/build-user.nix)
#   sfvideo     the player with FFmpeg and the hardware decoder (m1-avd/build-sfvideo-sfavd.nix; --arg gl true)
#   compositor  the desktop binary (file)       terminal   the terminal binary (file)
#   nix <directory below integrations/siliconforest> <file.nix> [arguments]    any other recipe
#
# SF_STAGED=pin   every piece as it was staged before the rebase: the builds of the pinned
#                 revision e7aa00a6 (they run on the tip's kernel and libSystem), compositor
#                 build-009, terminal build-008. tools/gpu_desktop.sh sets it for GPU_DESKTOP_PIN=1.
# SF_STAGED_PIN="<piece> ..."   only these pieces from the pin (besides PIN_ONLY).
# Store paths are remembered by m1-boot-speed/tools/bs_nix_path.py while the recipes are unchanged.
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
NP=integrations/siliconforest/m1-boot-speed/tools/bs_nix_path.py
RUNS=runs/m1-desktop-010
# Pieces staged from the pin's build whatever SF_STAGED says. Empty since the rename: the audio
# server and sfplay (the last two) build at the tip with the SiliconForest names, and the pin's
# builds look for the device nodes and sysctls by their old names, which the renamed kernel does not have (SF-I-sound.md,
# "The renamed audio server"). The pin itself builds only from the commit before the rename.
PIN_ONLY=""
PIECE=${1:?piece}; shift
from=tip
if [ "${SF_STAGED:-tip}" = pin ]; then from=pin; fi
case " $PIN_ONLY ${SF_STAGED_PIN:-} " in *" $PIECE "*) from=pin;; esac
nix() { if [ "$from" = pin ]; then SF_PD=pin "$NP" "$@"; else env -u SF_PD "$NP" "$@"; fi; }
file() { [ -x "$1" ] || { echo "staged.sh: no $PIECE binary at $1" >&2; exit 2; }; echo "$ROOT/$1"; }
case "$PIECE" in
  mesa) nix m1-mesa-asahi agx-offscreen-test.nix "$@";;
  mesa-wl) nix m1-mesa-asahi agx-wl-demo.nix "$@";;
  dcp-flip) nix m1-dcp dcp-flip-test.nix "$@";;
  drm-storm) nix m1-smp-audit drm-storm.nix "$@";;
  sysaudio) nix m1-sound/sysaudio build-sysaudio.nix -A all "$@";;
  sfplay) nix m1-sound build-sfplay.nix "$@";;
  sftbtest) nix m1-touchbar build-sftbtest.nix "$@";;
  netd) nix m1-bwfm/netd build-pd-netd.nix "$@";;
  avd-user) nix m1-avd build-user.nix "$@";;
  sfvideo) nix m1-avd build-sfvideo-sfavd.nix "$@";;
  nix) nix "$@";;
  compositor)
    if [ "$from" = pin ]; then file $RUNS/wm-darwin-scale-desktop-build-009/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop
    else file $RUNS/wm-darwin-tip-desktop-build-001/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop; fi;;
  terminal)
    if [ "$from" = pin ]; then file $RUNS/terminal-application-build-008/target/aarch64-apple-darwin/release/chonk-terminal-darwin
    else file $RUNS/terminal-application-tip-build-001/target/aarch64-apple-darwin/release/chonk-terminal-darwin; fi;;
  *) echo "staged.sh: unknown piece $PIECE" >&2; exit 2;;
esac
