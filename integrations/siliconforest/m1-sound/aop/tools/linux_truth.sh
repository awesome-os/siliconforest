#!/bin/bash
# SPDX-License-Identifier: MIT
# Read-only facts about the built-in microphones from the M1's own Linux (the Omacom
# kernel): device tree nodes, kernel log, ALSA card and its hardware parameters. Nothing is
# recorded and no setting is changed. Run through the bench queue:
#   BENCH_WHO=sound tools/bench.sh run integrations/siliconforest/m1-sound/aop/tools/linux_truth.sh
exec ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=15 chrisk@10.1.1.211 '
echo "== uname"; uname -r
echo "== dmesg"; sudo -n dmesg | grep -iE "aop|admac|24a808000|24a408000|macaudio" | head -80
echo "== cards"; cat /proc/asound/cards; cat /proc/asound/pcm
C=$(grep -l aop_audio /proc/asound/card*/pcm*/info 2>/dev/null | head -1 | sed "s,/pcm.*,,")
echo "== card dir $C"; cat $C/id; cat $C/pcm0c/info; cat $C/pcm0c/sub0/hw_params; cat $C/pcm0c/sub0/status
echo "== hw params"; arecord --dump-hw-params -D hw:$(cat $C/id),0 2>&1 | head -40
echo "== dt"
cd /proc/device-tree/soc
for n in aop@24ac00000 iommu@24a808000 dma-controller@24a980000 mbox@24a408000; do
  echo "-- $n"; for p in $n/*; do [ -f "$p" ] && { printf "%s: " "$p"; head -c 96 "$p" | xxd -p | tr -d "\n"; echo; }; done
done
for n in aop@24ac00000/*/; do echo "-- $n"; for p in $n*; do [ -f "$p" ] && { printf "%s: " "$p"; head -c 96 "$p" | xxd -p | tr -d "\n"; echo; }; done; done
echo "== iommu groups/reserved"; for d in /sys/bus/platform/devices/24ac00000.aop /sys/bus/platform/devices/24a980000.dma-controller; do echo $d; cat $d/iommu_group/reserved_regions 2>/dev/null; cat $d/iommu_group/type 2>/dev/null; ls $d | head -30; done
echo "== modules"; lsmod | grep -iE "aop|admac|rtkit" ; cat /sys/module/snd_soc_apple_aop/parameters/* 2>/dev/null
echo "== pipewire/users of capture"; cat /proc/asound/card*/pcm*c/sub*/status 2>/dev/null | head
'
