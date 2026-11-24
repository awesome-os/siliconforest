#!/bin/bash
# SPDX-License-Identifier: MIT
# A dEQP-VK smoke subset on the M1 (../cts.nix, vulkan_headless; the system's Vulkan: loader in
# /usr/local/lib, Honeykrisp through /etc/vulkan/icd.d), everything in /private/var/db/sfcts:
#   sfvk-cts.sh push                    deqp-vk, its data archive and the group case lists
#   sfvk-cts.sh run <name> <group>...   the groups one after another through sfvk-careful.sh (gui.lock, UART
#                                       on the M5, kernel log streamed): each with a time cap (CTS_CAP, 240 s);
#                                       a group that hits the cap, or after which the kernel reported a GPU
#                                       timeout, is recorded as such and the run stops there
#   sfvk-cts.sh groups                  the groups and their case counts
# Per group: <group>.qpa (the CTS log) and the totals line, here in runs/sfvk/<name>-*.
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd)
SSH=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest
     -o StrictHostKeyChecking=yes -o ConnectTimeout=15 "${SFVK_HOST:-root@10.1.1.211}")
D=/private/var/db/sfcts
# group name = the case-name prefix in the mustpass lists
CTS_GROUPS="info api.smoke api.object_management.single memory.allocation.basic synchronization.basic
 compute.pipeline.basic draw.renderpass.simple_draw api.copy_and_blit.core.image_to_image.simple_tests
 glsl.arrays.constructor texture.filtering.2d.formats.r8g8b8a8_unorm api.external.memory.dma_buf
 api.external.semaphore.sync_fd api.external.fence.sync_fd"
cts() { export PATH=$HOME/.nix-profile/bin:$PATH; nix-build --extra-experimental-features 'nix-command flakes' "$HERE/../cts.nix" --no-out-link; }
case "${1:-}" in
groups|push)
  C=$(cts); W=$(mktemp -d); mkdir -p "$W/lists"
  for g in $CTS_GROUPS; do cat "$C"/vk-default/*.txt "$C"/vk-default/*/*.txt 2>/dev/null | grep "^dEQP-VK\.${g//./\\.}\." | LC_ALL=C sort -u > "$W/lists/$g.txt" || true
    [ "$1" = groups ] && printf '%-60s %s\n' "$g" "$(wc -l < "$W/lists/$g.txt")"; done
  if [ "$1" = push ]; then
    cp "$C/bin/deqp-vk" "$W/"; cp -r "$C/archive-dir" "$W/archive"; chmod -R u+w "$W"
    cat > "$W/run-group.sh" <<'EOF'
#!/bin/sh
# run-group.sh <group> <cap seconds>: one group of dEQP-VK, killed at the cap
D=/private/var/db/sfcts; g=$1; cap=$2; cd $D
rm -f out/$g.qpa out/$g.txt; mkdir -p out
./deqp-vk --deqp-archive-dir=$D/archive --deqp-caselist-file=lists/$g.txt --deqp-log-filename=out/$g.qpa \
  --deqp-log-images=disable --deqp-log-shader-sources=disable > out/$g.txt 2>&1 & p=$!
t=0; hit=0; while kill -0 $p 2>/dev/null; do sleep 1; t=$((t+1)); [ $t -ge $cap ] && { kill -9 $p; echo "CTS_GROUP $g HIT_CAP ${cap}s"; hit=1; break; }; done
# a process stuck in the kernel does not die of kill -9 (15 November: dma_buf fd_properties): no wait then
if [ $hit = 0 ]; then wait $p; rc=$?; else rc=124; echo "CTS_GROUP $g last case: $(grep '^Test case' out/$g.txt | tail -1)"; fi
echo "CTS_GROUP $g rc=$rc seconds=$t $(grep -E 'Passed:|Failed:|Not supported:|Warnings:|Waived:' out/$g.txt | tr -s ' ' | tr '\n' ' ')"
grep -B1 -E "^  (Fail|Crash|InternalError|Timeout|ResourceError)" out/$g.txt | grep -o "dEQP-VK[^ ]*\|  [A-Z][A-Za-z]* (.*" | paste - - | head -12 | sed "s/^/CTS_BAD /"
EOF
    chmod +x "$W/run-group.sh"
    (cd "$W" && find . -type f | xargs sha256sum) | LC_ALL=C sort -k2 > "$W.sums"
    tar -C "$W" -cf - . | "${SSH[@]}" "rm -rf $D && mkdir -p $D && cd $D && tar xf -"
    "${SSH[@]}" "cd $D && find . -type f | xargs shasum -a 256" | LC_ALL=C sort -k2 > "$W.there"
    diff "$W.sums" "$W.there" > /dev/null && echo "$(wc -l < "$W.sums") files equal on the M1 in $D" || { echo "copy differs"; exit 1; }
    rm -f "$W.sums" "$W.there"
  fi
  rm -rf "$W"
  ;;
run)
  name=${2:?name}; shift 2; cap=${CTS_CAP:-240}
  cmd="unset VK_ICD_FILENAMES DYLD_LIBRARY_PATH; for g in $*; do n0=\$(dmesg | grep -c 'GPU timeout'); $D/run-group.sh \$g $cap;
    n1=\$(dmesg | grep -c 'GPU timeout'); [ \$n1 -gt \$n0 ] && { echo \"CTS_GROUP \$g GPU_TIMEOUTS \$((n1-n0)): stopping here\"; break; }; done"
  WATCH_AFTER=${WATCH_AFTER:-30} MAX_RUN=${MAX_RUN:-$(( cap * $# + 60 ))} "$HERE/sfvk-careful.sh" "$name" "$cmd"
  "${SSH[@]}" "cd $D/out && tar cf - ." > "$ROOT/runs/sfvk/$name-out.tar" 2>/dev/null || true
  "${SSH[@]}" "cat /private/tmp/sfvk-careful-$name.txt" | grep -E 'CTS_' > "$ROOT/runs/sfvk/$name-summary.txt" || true
  cat "$ROOT/runs/sfvk/$name-summary.txt"
  ;;
*) sed -n 3,13p "$0"; exit 2;;
esac
