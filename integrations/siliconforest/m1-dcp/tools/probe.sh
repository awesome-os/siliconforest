#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Syntax-checks the module's sources of drivers/gpu/drm/apple (the pin, unmodified) against
# SiliconForest's Linux layer with the flags the layer itself is compiled with, plus
#   - the headers listed in pin-headers.txt, taken from the pin's include/ as they are
#   - ../include/sf_dcp_compat.h force-included
# Prints the error count per file; logs go to <workdir>/err/. No object is produced: this is
# the M-a measurement, not the kext build.
#   probe.sh <workdir> [file ...]        (file without .c, e.g. dcp afk iomfb)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); top=$(cd "$here/.." && pwd); pd=$(cd "$top/.." && pwd)
root=$(cd "$pd/../.." && pwd); L=$root/sources/gpu-plan/linux-aurora
export PATH=$HOME/.nix-profile/bin:$PATH
N="--extra-experimental-features nix-command --extra-experimental-features flakes"
work=$(mkdir -p "${1:?workdir}" && cd "$1" && pwd); shift
ev() { nix-instantiate $N --eval --read-write-mode -E "toString (import $pd/m1-rust-kext/lkpi).$1" 2>/dev/null | tr -d '"'; }
BG=$(nix-build $N "$pd/m1-rust-kext/lkpi" -A bindingsGen --no-out-link 2>/dev/null)
FD=$(ev finalDrmkmod); export DARWIN_SDK_ROOT=$(ev cflags.sysroot)
CC=$(nix-build $N -E "(import $pd/m1-rust-kext/pins.nix).crossToolchain" --no-out-link 2>/dev/null)/bin/arm64-apple-darwin20.4-clang
rm -rf "$work/inc"; mkdir -p "$work/inc" "$work/err/epic"
while read -r h; do
	test -e "$L/include/$h" || { echo "not in the pin's sparse checkout: include/$h (git sparse-checkout add /include/$h)" >&2; exit 1; }
	mkdir -p "$work/inc/$(dirname "$h")"; cp "$L/include/$h" "$work/inc/$h"
done < "$here/pin-headers.txt"
# linux/gpio/consumer.h includes a private "defs.h" that only kernel builds have
: > "$work/inc/linux/gpio/defs.h"
mapfile -t CF < "$BG/c/cflags"
RT=$pd/m1-gpu-rtkit/src; D=$L/drivers/gpu/drm/apple
ALL="apple_drv apple_resume afk dcp dcp-fabric dcp_backlight dptxep iomfb parser systemep dcp-hdmi connector dcp-display-metadata dcp-180hz dcp-panel-edid ibootep dcpext_scanout dcpext_drm iomfb_v12_3 iomfb_v13_3 iomfb_v14_7_tpl iomfb_v14_7 iomfb_v14_7_link iomfb_h17p iomfb_v26_6 iomfb_v26_6_link j613_dcp_activate epic/dpavservep plane"
tot=0
for f in ${@:-$ALL}; do
	"$CC" -I"$work/inc" "${CF[@]}" -include "$top/include/sf_dcp_compat.h" -I"$RT/include" -I"$RT" \
		-I"$FD/drivers/gpu/drm" -I"$FD/include" -I"$FD/include/drm" -I"$FD/include/uapi" \
		-DKBUILD_MODNAME='"appledrm"' -Wno-everything -ferror-limit=0 -fsyntax-only "$D/$f.c" > "$work/err/$f.log" 2>&1 || true
	n=$(grep -c " error: " "$work/err/$f.log" || true); tot=$((tot + n)); printf "%-22s %s\n" "$f" "$n"
done
echo "total $tot"
