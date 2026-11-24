#!/bin/bash
# SPDX-License-Identifier: MIT
# tools/qemu_desktop.sh for a collection and root built at the rebase target, without touching
# the QEMU stream's files or its runs/qemu-dev. The stream's script is taken as it is at run
# time and three things in it are redirected: the run directory (runs/qemu-dev-rebase/<name>),
# the lab root (it finds it from its own path) and the image tool (rb_qd_image.py, which is
# qd_image.py with another output root). Options are that script's.
#   RB_BASE_IMAGE=<.img>   the base image (default: SF_PD=tip nix-build ../qemu-image.nix)
#   RB_LIBSYSTEM=<dir>     libSystem to stage (default: SF_PD=tip nix-build ../build-libsystem.nix);
#                          RB_LIBSYSTEM=image keeps the image's own
# Usage: rb_qemu.sh [qemu_desktop.sh options]      e.g. --name rq01 --only settle,settings
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
R=integrations/siliconforest/rebase
if [ -z "${RB_BASE_IMAGE:-}" ]; then
  out=$(cd $R && SF_PD=tip $NIX qemu-image.nix | tail -1); RB_BASE_IMAGE=$(ls "$out"/*.img | head -1)
fi
[ -f "$RB_BASE_IMAGE" ] || { echo "no base image at $RB_BASE_IMAGE" >&2; exit 2; }
EXTRA=()
if [ "${RB_LIBSYSTEM:-}" != image ]; then
  [ -n "${RB_LIBSYSTEM:-}" ] || RB_LIBSYSTEM=$(cd $R && SF_PD=tip $NIX build-libsystem.nix | tail -1)
  [ -f "$RB_LIBSYSTEM/usr/lib/libSystem.B.dylib" ] || { echo "no libSystem in $RB_LIBSYSTEM" >&2; exit 2; }
  EXTRA=(--file "$RB_LIBSYSTEM/usr/lib/libSystem.B.dylib=/usr/lib/libSystem.B.dylib")
fi
mkdir -p runs/qemu-dev-rebase
S=runs/qemu-dev-rebase/.qemu_desktop.rebase.sh
sed -e "s|^ROOT=\$(cd \"\$(dirname \"\$0\")/..\" && pwd); cd \"\$ROOT\"|ROOT=$ROOT; cd \"\$ROOT\"|" \
    -e 's|runs/qemu-dev/|runs/qemu-dev-rebase/|g' \
    -e "s|python3 -B \"\$Q/qd_image.py\"|python3 -B \"$HERE/rb_qd_image.py\"|" tools/qemu_desktop.sh > "$S"
grep -q "^ROOT=$ROOT; cd" "$S" && grep -q rb_qd_image.py "$S" && ! grep -q 'runs/qemu-dev/' "$S" \
  || { echo "tools/qemu_desktop.sh changed shape: rb_qemu.sh must be adapted" >&2; exit 2; }
echo "rb_qemu: base $RB_BASE_IMAGE"
env -u SF_PD QD_BASE_IMAGE="$RB_BASE_IMAGE" bash "$S" "${EXTRA[@]}" "$@"
