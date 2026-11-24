#!/bin/bash
# Boot the Chonkstep compositor's GPU/KMS steps once. Usage:
#   tools/wm_gl_boot.sh <new-name> [kc-store-path] [extra boot-args]
# Stages the new desktop binary over the RAM root's own /usr/bin/chonkstep-darwin-desktop (and,
# for the session step, the new bare compositor over /usr/bin/chonkstep-darwin): entries with
# the same destination replace the base ones, so the old binaries' 38 MB are reclaimed and the
# new compositor is the default binary of the root. Adds the Mesa bundle (libEGL, libGLESv2,
# libgallium, libdrm under /usr/local/lib/mesa-asahi) and a guest script, then boots through
# tools/dcp_boot.sh with sfagx=1 sfdcp=2 (a collection with SFAgx and SFDcp is required).
#   WM_GL_STEP=probe (default)  guest script darwin-wm-gl-probe.in: the KMS backend alone,
#                               clear + page flip, WMGL lines
#   WM_GL_STEP=bench            darwin-wm-gl-bench.in: --bench with the GL renderer on KMS (pixel
#                               check, the four scenarios, 5 s animation), then GL off-screen and
#                               the CPU renderer for comparison; WMBENCH lines
#   WM_GL_STEP=session          darwin-wm-gl-session.in: the shared-shell desktop with two
#                               terminals on the GL renderer (or, without input nodes, the bare
#                               compositor with the SHM test client), driven unattended by the
#                               compositor's own synthetic input script; WMSTAT and WMAUTO lines
#   WM_GL_STEP=dmabuf           darwin-wm-gl-dmabuf.in: the bare compositor on GL with the dma-buf
#                               test client (integrations/.../client-dmabuf): a client's GPU
#                               buffers shown without a copy; WMSTAT, DMACLIENT and WMEXT lines
#   WM_GL_STEP=wlgpu            darwin-wm-gl-wlgpu.in: a GPU-rendered application. The session on
#                               GL plus the Mesa EGL Wayland demo client (agx-wl-demo and its
#                               Mesa bundle from m1-mesa-asahi/agx-wl-demo.nix, staged under
#                               /usr/local/lib/mesa-asahi-wl); AGXWL, WMDMABUF and WMSTAT lines
#   WM_GL_WL_BUNDLE=<store>     that bundle (default: nix-build of agx-wl-demo.nix)
#   WM_GL_BIN=<path>            desktop binary (default: the wm-darwin-gl desktop build)
#   WM_GL_BARE_BIN=<path>       bare compositor binary, staged for the session step
#   WM_GL_ASSEMBLE_ONLY=1       stop after the offline assemble (RAM root staged and checked,
#                               nothing delivered or booted); prints the free space
#   WM_GL_DRY_RUN=1             print the staging list only
#   DCP_RUNTIME=<seconds>       guest run time before capture (default 150; bench 180; session 340)
#   WM_GL_TEMPLATE=<path>       another guest script
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
N=${1:?name}
# The collection with the multitouch trackpad (two-finger click = right button, two-finger
# drag = wheel). sfm1tpmt=0 as an extra boot-arg falls back to the basic pointer mode.
KC=${2:-$(tools/default_kc.sh)}
EXTRA=${3:-}
BIN=${WM_GL_BIN:-$ROOT/runs/m1-desktop-010/wm-darwin-gl-desktop-build-001/target/aarch64-apple-darwin/release/chonkstep-darwin-desktop}
BARE=${WM_GL_BARE_BIN:-$ROOT/runs/m1-desktop-010/wm-darwin-gl-compositor-build-001/target/aarch64-apple-darwin/release/chonkstep-darwin}
STAGE_BARE=0
STAGE_DMABUF_CLIENT=0
STAGE_WL=0
DMABUF_CLIENT=${WM_GL_DMABUF_CLIENT:-$ROOT/integrations/siliconforest/chonkstep-darwin/client-dmabuf/target/aarch64-apple-darwin/release/darwin-wayland-dmabuf-client}
case "${WM_GL_STEP:-probe}" in
  probe) TEMPLATE=$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-probe.in ;;
  bench) TEMPLATE=$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-bench.in; export DCP_RUNTIME=${DCP_RUNTIME:-180} ;;
  dmabuf) TEMPLATE=$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-dmabuf.in; STAGE_BARE=1; STAGE_DMABUF_CLIENT=1; export DCP_RUNTIME=${DCP_RUNTIME:-150} ;;
  wlgpu) TEMPLATE=$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-wlgpu.in; STAGE_BARE=1; STAGE_WL=1; export DCP_RUNTIME=${DCP_RUNTIME:-200} ;;
  session) TEMPLATE=$ROOT/integrations/siliconforest/chonkstep-darwin/darwin-wm-gl-session.in; STAGE_BARE=1; export DCP_RUNTIME=${DCP_RUNTIME:-340} ;;
  *) echo "unknown WM_GL_STEP=${WM_GL_STEP}" >&2; exit 2 ;;
esac
TEMPLATE=${WM_GL_TEMPLATE:-$TEMPLATE}
[ -x "$BIN" ] || { echo "no compositor binary at $BIN" >&2; exit 2; }
[ -r "$TEMPLATE" ] || { echo "no guest script at $TEMPLATE" >&2; exit 2; }
[ -d "$KC" ] || { echo "no kernel collection at $KC" >&2; exit 2; }
export PATH=$HOME/.nix-profile/bin:$PATH
# What this script stages from Nix besides the collection (tools, libraries, the audio server) are still
# the builds of the pinned revision, which run on the tip's kernel and libSystem (SF-V-rebase.md).
export SF_PD=pin
# The same Mesa bundle the zero-copy demo runs on (DCP_TEST=kms in tools/dcp_boot.sh).
T=$(tools/staged.sh mesa)
X="$BIN=/usr/bin/chonkstep-darwin-desktop"
# The terminal built for sessions longer than two minutes, over the root's own.
# (build-005: xterm emulation, scrollback, TERM=xterm-256color, selection and clipboard.)
TERM_BIN=${WM_GL_TERMINAL:-$ROOT/runs/m1-desktop-010/terminal-application-build-005/target/aarch64-apple-darwin/release/chonk-terminal-darwin}
[ -x "$TERM_BIN" ] && X="$X;$TERM_BIN=/usr/bin/chonk-terminal-darwin"
# The terminfo entry for the TERM the terminal sets (the RAM root has none), under both
# directory spellings ncurses uses (first letter, and its hex code as on Darwin).
TI=$ROOT/integrations/siliconforest/chonkstep-darwin/terminfo/xterm-256color
[ -r "$TI" ] && X="$X;$TI=/usr/share/terminfo/x/xterm-256color;$TI=/usr/share/terminfo/78/xterm-256color"
if [ "$STAGE_BARE" = 1 ]; then
  [ -x "$BARE" ] || { echo "no bare compositor binary at $BARE" >&2; exit 2; }
  X="$X;$BARE=/usr/bin/chonkstep-darwin"
fi
for l in libEGL.1.dylib libGLESv2.2.dylib libgallium-26.3.0-devel.dylib libdrm.2.dylib; do
  [ -r "$T/usr/local/lib/mesa-asahi/$l" ] || { echo "Mesa bundle has no $l (in $T)" >&2; exit 2; }
  X="$X;$T/usr/local/lib/mesa-asahi/$l=/usr/local/lib/mesa-asahi/$l"
done
if [ "$STAGE_DMABUF_CLIENT" = 1 ]; then
  [ -x "$DMABUF_CLIENT" ] || { echo "no dma-buf client at $DMABUF_CLIENT (build: see client-dmabuf/README.md)" >&2; exit 2; }
  X="$X;$DMABUF_CLIENT=/usr/bin/darwin-wayland-dmabuf-client"
fi
if [ "$STAGE_WL" = 1 ]; then
  # The GPU client and the Mesa it runs on (Wayland EGL platform), in its own prefix.
  WL=${WM_GL_WL_BUNDLE:-$(tools/staged.sh mesa-wl)}
  [ -x "$WL/usr/local/bin/agx-wl-demo" ] || { echo "no agx-wl-demo bundle (got '$WL')" >&2; exit 2; }
  X="$X;$WL/usr/local/bin/agx-wl-demo=/usr/local/bin/agx-wl-demo"
  for l in "$WL"/usr/local/lib/mesa-asahi-wl/*.dylib; do X="$X;$l=/usr/local/lib/mesa-asahi-wl/$(basename "$l")"; done
fi
# The guest script replaces the supervisor directly, as the other DCP tests do.
export PCIE_EXTRA_FILES="$X;$TEMPLATE=/usr/bin/darwin-desktop-test"
if [ "${WM_GL_DRY_RUN:-0}" = 1 ]; then
  echo "kc: $KC"; echo "runtime: ${DCP_RUNTIME:-150}"; echo "boot-args: sfgpudt=2 sfgpudt_stringprops=1 sfdispdt=2 sfdcp=2 sfagx=1 $EXTRA"
  echo "$PCIE_EXTRA_FILES" | tr ';' '\n'; exit 0
fi
if [ "${WM_GL_ASSEMBLE_ONLY:-0}" = 1 ]; then
  # The same assemble step tools/pcie_iterate.sh runs, and nothing after it.
  python3 -B tools/pcie_assemble.py "pcie-enum-candidate-$N" | tail -2
  grep -h '"free_bytes"' "artifacts/m1-desktop-010/pcie-enum-candidate-$N/ramdisk.record.json"
  exit 0
fi
DCP_MODE=${DCP_MODE:-2} tools/dcp_boot.sh "$N" "$KC" "sfagx=1 $EXTRA"
strings -n 6 "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" | grep -a 'WMGL\|WMAUTO\|WMPOWER\|WMEXT\|WMDMABUF\|DMACLIENT\|AGXWL\|agx-wl-demo:\|CODE SIGNING.*chonk\|chonkstep-gl:\|chonkstep-darwin-desktop:\|chonkstep-darwin:\|WMSTAT\|WMBENCH\|DL10_CLIENT_DONE\|CHONKDESKTOP\|panic' | cut -c1-${WM_GL_COLS:-330}
