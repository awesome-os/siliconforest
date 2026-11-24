#!/bin/bash
# Regenerates lkpi/patches-layer/ (all but the hand-written 0007) from an edited copy of the
# SiliconForest tree slice.
#   regen.sh setup <workdir>   a/ = the pristine slice (lkpi.pristineTree), b/ = a/ with the
#                              current patches applied (0007 excluded: it only names a file
#                              that lkpi/default.nix copies in). Edit files under b/.
#   regen.sh split <workdir>   rewrite the patch files from `diff a b`. mk.py assigns each
#                              hunk to a patch by file name and, where one file is shared,
#                              by marker words in the hunk; a new topic needs an entry there.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd); top=$(cd "$here/../../.." && pwd); P=$top/lkpi/patches-layer
export PATH=$HOME/.nix-profile/bin:$PATH
cmd=${1:?setup|split}; work=$(mkdir -p "${2:?workdir}" && cd "$2" && pwd)
case $cmd in
setup)
	# a flake source path, not a derivation: evaluate it
	src=$(nix-instantiate --extra-experimental-features nix-command --extra-experimental-features flakes \
		--eval --read-write-mode -E "toString (import $top/lkpi).pristineTree" 2>/dev/null | tr -d '"')
	test -d "$src/src/Kernel/fbsdkpi"
	rm -rf "$work/a" "$work/b"; mkdir -p "$work/a/src/Kernel" "$work/b/src/Kernel"
	mkdir -p "$work/a/src/Kernel/Extensions" "$work/b/src/Kernel/Extensions"
	cp -R "$src/src/Kernel/fbsdkpi" "$work/a/src/Kernel/"
	# SFDRMDri (the /dev/dri devices) is patched too (0020)
	cp -R "$src/src/Kernel/Extensions/SFDRMDri" "$work/a/src/Kernel/Extensions/"; chmod -R u+w "$work/a"
	cp -R "$work/a/src/Kernel/fbsdkpi" "$work/b/src/Kernel/"
	cp -R "$work/a/src/Kernel/Extensions/SFDRMDri" "$work/b/src/Kernel/Extensions/"
	for p in "$P"/*.patch; do
		case $p in *0007-*) continue;; esac
		patch -d "$work/b" -p1 --no-backup-if-mismatch < "$p" > /dev/null
	done
	echo "edit $work/b, then: $0 split $work";;
split)
	keep=$(mktemp); cp "$P"/0007-*.patch "$keep"
	name=$(basename "$P"/0007-*.patch)
	rm -f "$P"/*.patch; cp "$keep" "$P/$name"; rm -f "$keep"
	python3 "$here/mk.py" "$P" "$work";;
esac
