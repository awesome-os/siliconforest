#!/bin/bash
# SPDX-License-Identifier: MIT
# The Vulkan stage on the M1's installed SiliconForest, over SSH (no reboot, no file of the
# installed system replaced; everything lives in /private/var/db/sfvk).
#   sfvk.sh push            build ../stage.nix, copy it to sfvk.new on the M1, check every file's
#                           SHA-256 there against this side, then move the old stage to
#                           sfvk.prev and sfvk.new to sfvk
#   sfvk.sh run <command>   run a shell command on the M1 with the stage's environment
#                           (VK_ICD_FILENAMES, DYLD_LIBRARY_PATH, PATH), e.g. `sfvk.sh run vulkaninfo --summary`
#   sfvk.sh revert          sfvk.prev back to sfvk (sfvk goes to sfvk.bad)
# SSH: the M1's SiliconForest host key (~/.ssh/known_hosts_m1_siliconforest), strict checking.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
M1=${SFVK_HOST:-root@10.1.1.211}
SSH=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest
     -o StrictHostKeyChecking=yes -o ConnectTimeout=15 "$M1")
D=/private/var/db/sfvk
ENV="export VK_ICD_FILENAMES=$D/icd.d/asahi_icd.json DYLD_LIBRARY_PATH=$D/lib PATH=$D/bin:\$PATH;"
case "${1:-}" in
push)
  export PATH=$HOME/.nix-profile/bin:$PATH
  # SFVK_STAGE=<store path>: push a stage built earlier instead of building ../stage.nix now
  out=${SFVK_STAGE:-$(nix-build --extra-experimental-features nix-command --extra-experimental-features flakes \
        "$HERE/../stage.nix" --no-out-link)}
  echo "stage $out"
  (cd "$out" && find . -type f | xargs sha256sum) | LC_ALL=C sort -k2 > /tmp/sfvk.$$.sums
  tar -C "$out" -cf - . | "${SSH[@]}" "rm -rf $D.new && mkdir -p $D.new && cd $D.new && tar xf - && chmod -R u+w ."
  "${SSH[@]}" "cd $D.new && find . -type f | xargs shasum -a 256" | LC_ALL=C sort -k2 > /tmp/sfvk.$$.there
  if ! diff /tmp/sfvk.$$.sums /tmp/sfvk.$$.there; then
    echo "sfvk: the copy on the M1 differs; left in $D.new"; rm -f /tmp/sfvk.$$.*; exit 1
  fi
  echo "$(wc -l < /tmp/sfvk.$$.sums) files equal on the M1"
  rm -f /tmp/sfvk.$$.*
  "${SSH[@]}" "rm -rf $D.prev; [ -d $D ] && mv $D $D.prev; mv $D.new $D; echo $out > $D/STORE_PATH; ls $D"
  ;;
run)
  shift
  "${SSH[@]}" "$ENV $*"
  ;;
revert)
  "${SSH[@]}" "[ -d $D.prev ] && rm -rf $D.bad && mv $D $D.bad && mv $D.prev $D && cat $D/STORE_PATH"
  ;;
*)
  sed -n '3,12p' "$0"; exit 2;;
esac
