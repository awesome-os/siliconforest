#!/bin/bash
# SPDX-License-Identifier: MIT
# The decoder test on the INSTALLED SiliconForest (disk root, kernel with sfavd=1), over SSH:
#   avd_disk_test.sh push              sfavd-replay, the decoder firmware and three recordings to
#                                      /private/var/db/sfavd on the M1 (checksums compared); the
#                                      firmware also to /Library/Firmware/apple/ when it is not there
#   avd_disk_test.sh run <name>        each recording decoded by the hardware through /dev/sfavd under
#                                      m1-vulkan/tools/sfvk-careful.sh (gui.lock, UART on the M5, kernel
#                                      log streamed), then every picture's hash compared with the
#                                      workstation's software decode (avd_compare.py)
# Clips: Big Buck Bunny cuts of runs/avd (H.264 High, HEVC Main, VP9 profile 0, 1080p, 300 frames each).
set -uo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd); cd "$ROOT"
CLIPS="bbb-1080p-h264-10s:mp4 bbb-1080p-hevc-10s:mp4 bbb-1080p-vp9-10s:webm"
SSH=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest
     -o StrictHostKeyChecking=yes -o ConnectTimeout=15 root@10.1.1.211)
case "${1:-}" in
push)
  W=$(mktemp -d); mkdir -p "$W/bin" "$W/rec"
  cp "$("$ROOT/tools/staged.sh" avd-user)/usr/bin/sfavd-replay" "$W/bin/"
  for c in $CLIPS; do cp "runs/avd/rec/${c%%:*}.pdaj" "$W/rec/"; done
  cp private/firmware/apple/avd-fw-v2-t0.bin "$W/"; chmod -R u+w "$W"
  (cd "$W" && find . -type f | xargs sha256sum | LC_ALL=C sort -k2) > "$W.sums"
  tar -C "$W" -cf - . | "${SSH[@]}" 'rm -rf /private/var/db/sfavd && mkdir -p /private/var/db/sfavd && cd /private/var/db/sfavd && tar xf - &&
    { [ -e /Library/Firmware/apple/avd-fw-v2-t0.bin ] || { cp avd-fw-v2-t0.bin /Library/Firmware/apple/ && chmod 644 /Library/Firmware/apple/avd-fw-v2-t0.bin; }; } &&
    find . -type f | xargs shasum -a 256 | LC_ALL=C sort -k2' > "$W.there"
  diff "$W.sums" "$W.there" && echo "$(wc -l < "$W.sums") files equal on the M1"
  rm -rf "$W" "$W.sums" "$W.there"
  ;;
run)
  N=${2:?name}
  WATCH_AFTER=${WATCH_AFTER:-20} MAX_RUN=${MAX_RUN:-240} integrations/siliconforest/m1-vulkan/tools/sfvk-careful.sh "$N" \
    'cd /private/var/db/sfavd; ls -l /dev/sfavd /Library/Firmware/apple/avd-fw-v2-t0.bin; echo AVD_STATE $(sysctl -n kern.sf_avd.state);
     for r in rec/*.pdaj; do echo AVD_CLIP_BEGIN $(basename $r .pdaj); bin/sfavd-replay $r; echo AVD_CLIP_END rc=$?; done;
     echo AVD_STATS $(sysctl -n kern.sf_avd.stats); echo AVD_STATE $(sysctl -n kern.sf_avd.state)'
  # the command's complete output (the streamed copy in runs/sfvk/$N.txt is for watching)
  T=runs/sfvk/$N-full.txt; rc=0
  "${SSH[@]}" "cat /private/tmp/sfvk-careful-$N.txt" > "$T" || { echo "no output on the M1"; exit 1; }
  for c in $CLIPS; do
    name=${c%%:*}
    awk -v n="$name" '/AVD_CLIP_BEGIN/ {on = index($0, n) > 0} on {print} /AVD_CLIP_END/ {on = 0}' "$T" > "runs/sfvk/$N-$name.clip"
    order=; [ -f "runs/avd/rec/$name.order" ] && order=runs/avd/rec/$name.order
    python3 "$HERE/avd_compare.py" "runs/sfvk/$N-$name.clip" "runs/avd/clips/$name.${c##*:}.framemd5" "$name" $order || rc=1
  done
  grep -a -E 'AVD_STATE|AVD_STATS|sfavd' "$T" | grep -v SFAVD_FRAME | cut -c1-200 | head -8
  exit $rc
  ;;
*) sed -n 3,11p "$0"; exit 2;;
esac
