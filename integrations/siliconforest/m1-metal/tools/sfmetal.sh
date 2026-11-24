#!/bin/bash
# SPDX-License-Identifier: MIT
# The Metal experiment on the M1's installed SiliconForest, over SSH (nothing of the system replaced;
# everything in /private/var/db/sfmetal):
#   sfmetal.sh push [stage.nix args]   build ../stage.nix, copy it (checksums compared), sfmetal.prev kept
#   sfmetal.sh careful <name> <cmd>    run <cmd> there through ../../m1-vulkan/tools/sfvk-careful.sh (gui.lock,
#                                      UART on the M5, kernel log streamed): any run that submits GPU work
# The commands find Vulkan as any program does (system loader and ICD, ../../m1-vulkan/system.nix);
# SFMETAL_STAGE_VK=1 uses VK's stage in /private/var/db/sfvk instead.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd)
SSH=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest
     -o StrictHostKeyChecking=yes -o ConnectTimeout=15 "${SFVK_HOST:-root@10.1.1.211}")
D=/private/var/db/sfmetal
case "${1:-}" in
push)
  shift
  export PATH=$HOME/.nix-profile/bin:$PATH
  out=${SFMETAL_STAGE:-$(nix-build --extra-experimental-features 'nix-command flakes' "$HERE/../stage.nix" --no-out-link "$@")}
  echo "stage $out"
  (cd "$out" && find . -type f | xargs sha256sum) | LC_ALL=C sort -k2 > /tmp/sfmetal.$$.sums
  tar -C "$out" -cf - . | "${SSH[@]}" "rm -rf $D.new && mkdir -p $D.new && cd $D.new && tar xf - && chmod -R u+w ."
  "${SSH[@]}" "cd $D.new && find . -type f | xargs shasum -a 256" | LC_ALL=C sort -k2 > /tmp/sfmetal.$$.there
  diff /tmp/sfmetal.$$.sums /tmp/sfmetal.$$.there || { echo "the copy differs; left in $D.new"; rm -f /tmp/sfmetal.$$.*; exit 1; }
  echo "$(wc -l < /tmp/sfmetal.$$.sums) files equal on the M1"; rm -f /tmp/sfmetal.$$.*
  "${SSH[@]}" "rm -rf $D.prev; [ -d $D ] && mv $D $D.prev; mv $D.new $D; echo $out > $D/STORE_PATH"
  ;;
careful)
  name=${2:?name}; shift 2
  env_vk='unset VK_ICD_FILENAMES DYLD_LIBRARY_PATH;'
  [ "${SFMETAL_STAGE_VK:-0}" = 1 ] && env_vk=''
  "$ROOT/integrations/siliconforest/m1-vulkan/tools/sfvk-careful.sh" "$name" "$env_vk cd $D && export PATH=$D/bin:\$PATH && $*"
  ;;
*) sed -n 3,10p "$0"; exit 2;;
esac
