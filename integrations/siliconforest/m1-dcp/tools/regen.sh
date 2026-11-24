#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Regenerates ../patches from an edited copy of the pin's drivers/gpu/drm/apple.
#   regen.sh setup <workdir>   drv.orig/ = the pin, drv/ = the pin + current patches; edit drv/
#   regen.sh split <workdir>   0001 = the `jiffies` hunk of apple_drv.c (kept as it is), 0002 = everything else
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); top=$(cd "$here/.." && pwd); P=$top/patches
pin=$(cd "$top/../../.." && pwd)/sources/gpu-plan/linux-aurora/drivers/gpu/drm/apple
cmd=${1:?setup|split}; work=$(mkdir -p "${2:?workdir}" && cd "$2" && pwd)
norm() { sed -E 's|^--- drv.orig/([^\t]*).*|--- a/\1|; s|^\+\+\+ drv/([^\t]*).*|+++ b/\1|' | grep -v '^diff -u'; }
case $cmd in
setup)
	rm -rf "$work/drv.orig" "$work/drv"; cp -R "$pin" "$work/drv.orig"; chmod -R u+w "$work/drv.orig"
	cp -R "$work/drv.orig" "$work/drv"
	for p in "$P"/*.patch; do patch -d "$work/drv" -p1 --no-backup-if-mismatch < "$p" > /dev/null; done;;
split)
	cd "$work"
	# 0001 is one fixed hunk: apply it to a copy of the pin, and diff the rest against that
	rm -rf mid; cp -R drv.orig mid
	patch -d mid -p1 --no-backup-if-mismatch < "$P/0001-local-variable-not-named-jiffies.patch" > /dev/null
	{ cat "$here/0002.msg"; for f in $(diff -rq mid drv | awk '{print $2}' | sed 's|^mid/||' | sort); do
		diff -u "mid/$f" "drv/$f" | sed -E 's|^--- mid/([^\t]*).*|--- a/\1|; s|^\+\+\+ drv/([^\t]*).*|+++ b/\1|' | grep -v '^diff -u' || true; done; } > "$P/0002-sfdcp-stage-lines.patch"
	rm -rf mid
	for p in "$P"/*.patch; do echo "$(basename "$p"): +$(grep -c '^+[^+]' "$p") -$(grep -c '^-[^-]' "$p")"; done;;
esac
