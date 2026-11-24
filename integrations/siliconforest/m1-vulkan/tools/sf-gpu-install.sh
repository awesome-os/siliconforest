#!/bin/bash
# SPDX-License-Identifier: MIT
# The graphics of the default install (../system.nix: GL Mesa b7b03c5 with the clock and ETIME fixes,
# Honeykrisp, the Vulkan loader, vulkaninfo, vkcube) on the M1's installed SiliconForest, over SSH:
#   sf-gpu-install.sh push              build ../system.nix, copy its tree to /private/var/db/sfgpu.new
#                                       on the M1 and compare every file's SHA-256 there
#   sf-gpu-install.sh install <tag>     put each file at its path: identical ones are left alone; a file
#                                       that is there and differs is kept beside as <name>.old-<tag>
#                                       (never overwritten if that exists); each new one goes in by a
#                                       rename (running programs keep the file they mapped). Record:
#                                       /private/var/db/sfgpu/installed-<tag>.txt
#   sf-gpu-install.sh revert <tag>      the files kept as .old-<tag> back, the ones that were new removed
#   sf-gpu-install.sh status            what is installed against the tree on the M1
# Programs already running keep the libraries they loaded: restart the compositor (in gui.lock) to
# take the new GL. Also the userland installer's part `gpu` (../../m1-storage/tools/sf_userland_push.sh).
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
SSH=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest
     -o StrictHostKeyChecking=yes -o ConnectTimeout=15 "${SFVK_HOST:-root@10.1.1.211}")
N=/private/var/db/sfgpu.new; D=/private/var/db/sfgpu
case "${1:-}" in
push)
  export PATH=$HOME/.nix-profile/bin:$PATH
  out=${SFGPU_TREE:-$(nix-build --extra-experimental-features 'nix-command flakes' "$HERE/../system.nix" --no-out-link)}
  echo "tree $out"
  (cd "$out/root" && find . -type f | xargs sha256sum) | LC_ALL=C sort -k2 > /tmp/sfgpu.$$.sums
  tar -C "$out" -cf - root FILES.txt | "${SSH[@]}" "rm -rf $N && mkdir -p $N && cd $N && tar xf - && echo $out > STORE_PATH"
  "${SSH[@]}" "cd $N/root && find . -type f | xargs shasum -a 256" | LC_ALL=C sort -k2 > /tmp/sfgpu.$$.there
  if ! diff /tmp/sfgpu.$$.sums /tmp/sfgpu.$$.there; then echo "the copy on the M1 differs"; rm -f /tmp/sfgpu.$$.*; exit 1; fi
  echo "$(wc -l < /tmp/sfgpu.$$.sums) files equal on the M1 in $N"; rm -f /tmp/sfgpu.$$.*
  ;;
install|revert|status)
  op=$1; tag=${2:-}
  [ "$op" = status ] || [[ "$tag" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "tag: letters, digits, . _ -"; exit 2; }
  "${SSH[@]}" "OP=$op TAG=$tag N=$N D=$D sh -s" <<'REMOTE'
set -u
mkdir -p "$D"; REC="$D/installed-$TAG.txt"
case "$OP" in
install)
  [ -e "$REC" ] && { echo "already installed under tag $TAG ($REC): revert it first"; exit 3; }
  : > "$REC.part"
  while read -r f; do
    p=${f#.}; src="$N/root$p"
    if [ -L "$src" ]; then
      if [ -L "$p" ] && [ "$(readlink "$p")" = "$(readlink "$src")" ]; then echo "same $p"; continue; fi
      [ -e "$p" ] || [ -L "$p" ] && { echo "refusing to replace $p by a link"; continue; }
      ln -s "$(readlink "$src")" "$p" && echo "new $p" >> "$REC.part" && echo "new $p (link)"; continue
    fi
    mkdir -p "$(dirname "$p")"
    if [ -e "$p" ] && cmp -s "$src" "$p"; then echo "same $p"; continue; fi
    cp -p "$src" "$p.new-$TAG" && chown root:wheel "$p.new-$TAG" || { echo "FAIL copy $p"; exit 4; }
    case "$p" in *.json) chmod 644 "$p.new-$TAG";; *) chmod 755 "$p.new-$TAG";; esac
    cmp -s "$src" "$p.new-$TAG" || { echo "FAIL compare $p"; exit 4; }
    if [ -e "$p" ]; then
      [ -e "$p.old-$TAG" ] || ln "$p" "$p.old-$TAG" || cp -p "$p" "$p.old-$TAG"
      mv -f "$p.new-$TAG" "$p" && echo "replaced $p" >> "$REC.part" && echo "replaced $p (previous: $p.old-$TAG)"
    else
      mv -f "$p.new-$TAG" "$p" && echo "new $p" >> "$REC.part" && echo "new $p"
    fi
  done < "$N/FILES.txt"
  mv "$REC.part" "$REC"; sync; echo "record $REC: $(wc -l < "$REC") files"
  ;;
revert)
  [ -e "$REC" ] || { echo "nothing installed under tag $TAG"; exit 3; }
  while read -r what p; do
    case "$what" in
    replaced) [ -e "$p.old-$TAG" ] && mv -f "$p.old-$TAG" "$p" && echo "restored $p";;
    new) rm -f "$p" && echo "removed $p";;
    esac
  done < "$REC"
  mv "$REC" "$REC.reverted.$(date +%s)"; sync
  ;;
status)
  while read -r f; do p=${f#.}
    if [ -L "$N/root$p" ]; then [ -L "$p" ] && echo "link $p" || echo "missing $p"
    elif [ -e "$p" ] && cmp -s "$N/root$p" "$p"; then echo "installed $p"
    elif [ -e "$p" ]; then echo "differs $p"; else echo "missing $p"; fi
  done < "$N/FILES.txt"; ls "$D" 2>/dev/null
  ;;
esac
REMOTE
  ;;
*) sed -n 3,15p "$0"; exit 2;;
esac
