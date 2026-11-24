#!/bin/sh
# SPDX-License-Identifier: MIT
# SiliconForest: remove the startup-picker volume and the boot partition on macOS
# (INSTALL.md, "Uninstall"). It only PRINTS what it found and the two commands; with
# --execute it runs them after you type the container's name. The SiliconForest root
# partition (ext4) is not touched, and the freed space is left unallocated.
# First make another system the default (INSTALL.md step 8), or the machine starts into
# recovery's "no startup disk" screen at the next start.
set -e
STUB=$(diskutil list internal | awk '/Apple_APFS/ && /Container disk/ {print $NF, $(NF-3)}' | while read part ctnr; do
    diskutil apfs list "$ctnr" 2>/dev/null | grep -q 'Name: *SiliconForest ' && echo "$ctnr $part"; done | head -1)
[ -n "$STUB" ] || { echo "No APFS container with a volume named SiliconForest on the internal disk."; exit 1; }
CTNR=${STUB% *}; PART=${STUB#* }
BOOT=$(diskutil list internal | awk '/ EFI / && /SFBOOT/ {print $NF}' | head -1)
echo "SiliconForest's container: $CTNR (partition $PART)"; diskutil apfs list "$CTNR" | grep 'Name:\|Role' | sed 's/^ */    /'
echo "SiliconForest's boot partition: ${BOOT:-NOT FOUND (look for a 500 MB EFI partition named SFBOOT in: diskutil list)}"
echo "Default boot volume now: $(bless --getBoot 2>/dev/null || echo unknown)"
echo
echo "Commands:"
echo "    diskutil apfs deleteContainer $CTNR"
[ -z "$BOOT" ] || echo "    diskutil eraseVolume free free $BOOT"
echo "If the first leaves a partition where the container was (diskutil list), free it the same way: diskutil eraseVolume free free $PART"
[ "$1" = "--execute" ] || { echo; echo "Nothing was changed. Run with --execute to do it."; exit 0; }
[ "$(id -u)" = 0 ] || { echo "Run with sudo."; exit 1; }
printf 'Type the container (%s) to delete it and the boot partition: ' "$CTNR"; read answer
[ "$answer" = "$CTNR" ] || { echo "Not confirmed; nothing was changed."; exit 1; }
diskutil apfs deleteContainer "$CTNR"
[ -z "$BOOT" ] || diskutil eraseVolume free free "$BOOT"
diskutil list internal
