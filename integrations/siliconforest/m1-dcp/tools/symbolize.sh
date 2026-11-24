#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Names kext addresses from a panic. The kexts are linked without local symbols (-x); the
# link map kept beside each build ($out/nix-support/<Kext>.map) has them all.
#   symbolize.sh <collection-or-kext-store-path> <kext-load-address> <pc> [<lr> ...]
#   symbolize.sh <collection-or-kext-store-path> +0x102980 [+0x...]        (offsets in the kext)
# With a collection path the SFDcp (or, with KEXT=SFAgx, the SFAgx) kext that went into it is
# found through the nix derivation. Example, boot dp02:
#   symbolize.sh /nix/store/asbynrig...-siliconforest-kc-arm64-0.1 0xfffffe00130e4000 0xfffffe00131e6980 0xfffffe00131e6958
set -euo pipefail
export PATH=$HOME/.nix-profile/bin:$PATH
kext=${KEXT:-SFDcp}; path=${1:?store path}; shift
map=$(ls "$path"/nix-support/$kext.map 2>/dev/null || true)
if [ -z "$map" ]; then
	drv=$(nix-store -q --deriver "$path")
	for d in $(nix-store -q --references "$drv" | grep '\.drv$'); do
		for o in $(nix-store -q --outputs "$d" 2>/dev/null); do
			if [ -e "$o/nix-support/$kext.map" ]; then map=$o/nix-support/$kext.map; fi
		done
	done
fi
test -n "$map" || { echo "no $kext.map found from $path" >&2; exit 1; }
echo "map: $map"
base=0
case ${1:?address} in +*) ;; *) base=$(($1)); shift;; esac
for a in "$@"; do
	case $a in +*) off=$(( ${a#+} ));; *) off=$(( a - base ));; esac
	awk -v off="$off" -v shown="$a" -v kext="$kext" '/^# Symbols/{f=1;next}
		f && $1 ~ /^0x/ { a=strtonum($1); s=strtonum($2);
			if (off >= a && off < a + s) { n=$0; sub(/^[^\]]*\] */, "", n); printf "%s = %s+0x%x  %s + 0x%x\n", shown, kext, off, n, off - a; hit=1 } }
		END { if (!hit) printf "%s = +0x%x: not in the map\n", shown, off }' "$map"
done
