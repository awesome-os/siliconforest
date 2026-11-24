#!/bin/bash
# SPDX-License-Identifier: MIT
# Build the macOS-compatibility additions and lay them out for the QEMU rehearsal
# (sf_compat_qemu.sh) and for the running M1 (sf_compat_push.sh):
#   runs/sf-compat/stage/replace/<leaf>.new + replace.list   libraries the root has, to be replaced
#       ("<installed path> <sha256> <probe on the target> [program ...]" per line)
#   runs/sf-compat/stage/new/                                 files the root does not have, rooted at /
#   runs/sf-compat/stage/new.sha256                           "<sha256> <path>" of each
#   runs/sf-compat/stage/test/                                probes (to /usr/share/sf-test)
# What goes in is decided here and nowhere else (docs/roadmap/issues/SF-S-homebrew.md, "Framework gaps").
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
C=integrations/siliconforest/macos-compat; ST=runs/sf-compat/stage
nb() { (cd "$C" && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link "$1" 2>/dev/null | tail -1); }
chmod -R u+w "$ST" 2>/dev/null || true; rm -rf "$ST"; mkdir -p "$ST/replace" "$ST/new" "$ST/test"
F=System/Library/Frameworks; BASE0=${SF_BASE:-runs/sf-gaps/m1root}
# ---- replaced: CoreServices (text-encoding calls)
CS=$(nb build-coreservices.nix); PR=$(nb build-probe.nix)
cp "$CS/$F/CoreServices.framework/Versions/A/CoreServices" "$ST/replace/CoreServices.new"
cp "$PR"/usr/share/sf-test/* "$ST/test/"
echo "/$F/CoreServices.framework/Versions/A/CoreServices $(sha256sum "$ST/replace/CoreServices.new" | cut -c1-64) /usr/share/sf-test/sf-compat-probe /opt/homebrew/bin/bash /opt/homebrew/bin/git /opt/homebrew/bin/ggrep /opt/homebrew/bin/python3 /opt/homebrew/bin/nvim" > "$ST/replace.list"
# ---- replaced: libexpat (exported nothing), libncurses (the $NCURSES60 names; nothing asks libpanel for them)
SL=$(cd integrations/siliconforest/homebrew && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-syslibs.nix 2>/dev/null | tail -1)
for e in libexpat.1.dylib:sf-expat-probe libncurses.5.4.dylib:sf-curses-probe; do
  l=${e%%:*}; pr=${e##*:}
  if cmp -s "$SL/usr/lib/$l" "$BASE0/usr/lib/$l"; then echo "replace: the target's /usr/lib/$l is this build already"; continue; fi
  cp "$SL/usr/lib/$l" "$ST/replace/$l.new"
  echo "/usr/lib/$l $(sha256sum "$ST/replace/$l.new" | cut -c1-64) /usr/share/sf-test/$pr /opt/homebrew/bin/python3 /opt/homebrew/bin/git /opt/homebrew/bin/htop /opt/homebrew/bin/nano" >> "$ST/replace.list"
done
# ---- replaced: libSystem (strxfrm in a UTF-8 locale, timingsafe_bcmp, ualarm)
LS=$(cd integrations/siliconforest/libsystem-default && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-libsystem.nix 2>/dev/null | tail -1)
if cmp -s "$LS/usr/lib/libSystem.B.dylib" "$BASE0/usr/lib/libSystem.B.dylib"; then echo "replace: the target's libSystem is this build already"
else
  cp "$LS/usr/lib/libSystem.B.dylib" "$ST/replace/libSystem.B.dylib.new"
  echo "/usr/lib/libSystem.B.dylib $(sha256sum "$ST/replace/libSystem.B.dylib.new" | cut -c1-64) /usr/share/sf-test/sf-strxfrm-probe /opt/homebrew/bin/node /usr/local/bin/claude /opt/homebrew/bin/python3 /opt/homebrew/bin/bash /opt/homebrew/bin/git /opt/homebrew/bin/vim /opt/homebrew/bin/nvim /opt/homebrew/bin/tmux" >> "$ST/replace.list"
fi
# ---- new files. A path the target has already (SF_BASE: a copy of its trees, default the one
# sf_gapscan.py reads) stays the target's: it is named here and left out.
BASE=${SF_BASE:-runs/sf-gaps/m1root}
[ -d "$BASE/usr/lib" ] || { echo "no copy of the target's root at $BASE (SF_BASE)" >&2; exit 2; }
lay() {  # <tree rooted at />: its files and links that the target does not have, into stage/new
  local tree=$1 f kept=0 put=0
  while IFS= read -r -d '' f; do
    if [ -e "$BASE/$f" ] || [ -L "$BASE/$f" ]; then kept=$((kept + 1)); echo "  the target has /$f: kept"
    else mkdir -p "$ST/new/$(dirname "$f")"; cp -P --no-preserve=ownership "$tree/$f" "$ST/new/$f"; put=$((put + 1)); fi
  done < <(cd "$tree" && find . -mindepth 1 \( -type f -o -type l \) -not -path './nix-support/*' -printf '%P\0')
  echo "  $tree: $put new, $kept the target has"
}
# AppKit and Cocoa: the Cocoa stream's stack, the part a program linking AppKit loads (build-appkit.nix)
AK=$(nb build-appkit.nix); echo "appkit: $AK"; lay "$AK"
# the system Perl 5.34 (build-perl.nix): /usr/bin/perl, /System/Library/Perl/5.34, libperl.dylib
if [ "${SF_COMPAT_PERL:-1}" = 1 ]; then PL=$(nb build-perl.nix); echo "perl: $PL"; [ -d "$PL/System/Library/Perl" ] && lay "$PL" || echo "  perl did not build: left out"; fi
chmod -R u+w "$ST"
(cd "$ST/new" && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum | sed 's|  \./| |') > "$ST/new.sha256"
(cd "$ST/new" && find . -type l -printf '%l\t%P\n' | LC_ALL=C sort) > "$ST/new.links"
echo "stage: $(grep -c . "$ST/replace.list") to replace; new: $(grep -c . "$ST/new.sha256" || true) files, $(grep -c . "$ST/new.links" || true) links, $(du -sm "$ST/new" | cut -f1) MB"
