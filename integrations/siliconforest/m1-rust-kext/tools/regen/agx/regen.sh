#!/bin/bash
# Regenerates agx/patches/ from edited copies of drivers/gpu/drm/asahi.
#   regen.sh setup <workdir>   src.orig/ = the driver as in sources/gpu-plan/linux-aurora,
#                              src/ = + patches 0001 and 0002, src3/ = + 0003 (diagnostics).
#                              Edit src/ for 0001/0002 (and repeat the edit in src3/), src3/
#                              for 0003.
#   regen.sh split <workdir>   rewrite the three patch files (mkagx.sh says which file
#                              belongs to which patch).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); top=$(cd "$here/../../.." && pwd); P=$top/agx/patches
drv=$(cd "$top/../../.." && pwd)/sources/gpu-plan/linux-aurora/drivers/gpu/drm/asahi
cmd=${1:?setup|split}; work=$(mkdir -p "${2:?workdir}" && cd "$2" && pwd)
case $cmd in
setup)
	rm -rf "$work/src.orig" "$work/src" "$work/src3"
	cp -R "$drv" "$work/src.orig"; chmod -R u+w "$work/src.orig"; cp -R "$work/src.orig" "$work/src"
	for p in "$P"/0001-*.patch "$P"/0002-*.patch; do patch -d "$work/src" -p1 --no-backup-if-mismatch < "$p" > /dev/null; done
	cp -R "$work/src" "$work/src3"
	patch -d "$work/src3" -p1 --no-backup-if-mismatch < "$P"/0003-*.patch > /dev/null
	cp "$here/mkagx.sh" "$work/"
	echo "edit $work/src and $work/src3, then: $0 split $work";;
split)
	cp "$here/mkagx.sh" "$work/"; bash "$work/mkagx.sh" "$P";;
esac
