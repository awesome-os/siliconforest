#!/bin/sh
# SPDX-License-Identifier: MIT
# Lay the files of ../build-macos-apps-root.nix that no running program depends on for its
# start onto a running SiliconForest (frameworks of the application stack, the Swift runtime,
# the small new libraries): on the target, from a directory ../tools/sf_terminal.sh filled.
#   sh sf-apps-install.sh <stage directory> [new|all]
#       <stage>/manifest   "<sha256> <path>" per file, <stage>/links "<target><TAB><path>",
#       <stage>/wrapped    the framework paths that are wrappers (their wrapped library stays
#                          beside them as <path>.pd), <stage>/files/<path>
#       new: only the paths the system does not have (nothing that exists is touched);
#       all (default): those, and the files that replace an existing one.
#   sh sf-apps-install.sh --revert
# Every staged file is hashed before anything is moved (and before anything has loaded it); one
# wrong hash and nothing is done. An existing file is kept: as <path>.pd where the new file is
# its wrapper, as <path>.old-sfy otherwise (a hard link, or a link of the same content); the
# new file takes the name by rename(2). What was done is appended to
# /private/var/db/sf-macos-apps.installed ("new|replaced|wrapped <path>"); --revert undoes that
# list from its end (removes the new, renames the kept files back).
LIST=/private/var/db/sf-macos-apps.installed
if [ "$1" = --revert ]; then
    [ -s "$LIST" ] || { echo "SFAPPS nothing recorded in $LIST"; exit 0; }
    n=0
    for line in $(awk '{print NR}' "$LIST" | sort -rn); do
        what=$(sed -n "${line}p" "$LIST" | cut -d' ' -f1); p=$(sed -n "${line}p" "$LIST" | cut -d' ' -f2-)
        case "$what" in
            new) rm -f "$p";;
            replaced) [ -e "$p.old-sfy" ] || [ -L "$p.old-sfy" ] && mv -f "$p.old-sfy" "$p";;
            wrapped) [ -e "$p.pd" ] && mv -f "$p.pd" "$p";;
        esac
        n=$((n + 1))
    done
    mv "$LIST" "$LIST.reverted"; sync
    echo "SFAPPS reverted $n entries (the list is $LIST.reverted)"; exit 0
fi
S=$1; MODE=${2:-all}
[ -s "$S/manifest" ] && [ -d "$S/files" ] || { echo "SFAPPS usage: <stage directory> [new|all] | --revert"; exit 2; }
wrong=0; n=0
while read -r want p; do
    n=$((n + 1)); [ "$(sha256sum "$S/files/$p" 2>/dev/null | cut -c1-64)" = "$want" ] || { wrong=$((wrong + 1)); echo "SFAPPS differs: $p"; }
done < "$S/manifest"
echo "SFAPPS $n staged files hashed, $wrong differ"
[ "$wrong" = 0 ] && [ "$n" -gt 0 ] || { echo "SFAPPS FAIL nothing was changed"; exit 1; }
mkdir -p "$(dirname "$LIST")"; new=0; replaced=0; wrapped=0; skipped=0
while read -r want p; do
    d=/$p
    if [ -e "$d" ] && [ "$(sha256sum "$d" 2>/dev/null | cut -c1-64)" = "$want" ]; then skipped=$((skipped + 1)); continue; fi
    case "$p" in *.pd) w=${d%.pd}
        # a wrapped library from the recipe: only where the system has neither it nor its wrapper
        if [ -e "$w" ] || [ -L "$w" ] || [ -e "$d" ]; then skipped=$((skipped + 1)); continue; fi;;
    esac
    exists=0; { [ -e "$d" ] || [ -L "$d" ]; } && exists=1
    if [ "$MODE" = new ] && [ "$exists" = 1 ]; then continue; fi
    mkdir -p "$(dirname "$d")" || { echo "SFAPPS FAIL mkdir for $d"; exit 1; }
    chown 0:0 "$S/files/$p"; chmod 755 "$S/files/$p"
    case "$p" in */Resources/*|*.plist|*.nib|*.txt) chmod 644 "$S/files/$p";; esac
    if [ "$exists" = 1 ]; then
        if grep -qx "/$p" "$S/wrapped" 2>/dev/null; then
            if [ ! -e "$d.pd" ]; then ln "$d" "$d.pd" || { echo "SFAPPS FAIL could not keep $d as $d.pd"; exit 1; }; echo "wrapped $d" >> "$LIST"; wrapped=$((wrapped + 1))
            else echo "replaced $d" >> "$LIST.note" 2>/dev/null; replaced=$((replaced + 1)); fi
        else
            if [ ! -e "$d.old-sfy" ] && [ ! -L "$d.old-sfy" ]; then
                if [ -L "$d" ]; then ln -s "$(readlink "$d")" "$d.old-sfy"; else ln "$d" "$d.old-sfy"; fi || { echo "SFAPPS FAIL could not keep $d"; exit 1; }
                echo "replaced $d" >> "$LIST"
            fi
            replaced=$((replaced + 1))
        fi
    else echo "new $d" >> "$LIST"; new=$((new + 1)); fi
    mv -f "$S/files/$p" "$d" || { echo "SFAPPS FAIL rename to $d"; exit 1; }
done < "$S/manifest"
links=0
if [ -s "$S/links" ]; then
    while IFS="$(printf '\t')" read -r target p; do
        d=/$p; [ -e "$d" ] || [ -L "$d" ] && continue
        mkdir -p "$(dirname "$d")" && ln -s "$target" "$d" && { echo "new $d" >> "$LIST"; links=$((links + 1)); }
    done < "$S/links"
fi
sync
echo "SFAPPS done ($MODE): $new new, $replaced replaced (kept as .old-sfy), $wrapped wrapped (kept as .pd), $links links, $skipped as they were"
