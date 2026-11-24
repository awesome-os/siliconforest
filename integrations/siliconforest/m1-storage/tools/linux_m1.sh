#!/bin/bash
# SPDX-License-Identifier: MIT
# READ-ONLY fact gathering on the M1's Linux for the storage work, in ONE ssh session (the
# M1's sshd refused further connections after a handful in quick succession). The remote
# script arrives on standard input (sudo -n bash -s): no file is created on the M1, the
# disk is opened read-only (dd iflag=direct piped to sha256sum; Identify and SMART through
# the kernel's passthrough on a read-only open), nothing is mounted, written, trimmed or
# formatted; it keeps its range list in a shell variable. Results go to runs/m1-storage/<tag>/ (never committed: serial numbers,
# partition identities).
#   linux_m1.sh all <tag> [rangefile]   facts, ranges (made here unless given) and their SHA-256
#   linux_m1.sh compare <a> <b>         two hash files: equal / different ranges
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../../.." && pwd)
M1="ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=8 -o ServerAliveInterval=15 chrisk@10.1.1.211"
cmd=${1:?all|compare}
if [ "$cmd" = compare ]; then
  join -j1 <(awk '/^NVT_HASH [0-9]/ {print $2"+"$3, $4}' "$2" | sort) <(awk '/^NVT_(MEDIA_)?HASH [0-9]/ {print $2"+"$3, $4}' "$3" | sort) |
    awk '{ if ($2 == $3) e++; else { d++; print "DIFFERENT", $1 } } END { printf "ranges equal=%d different=%d\n", e, d }'
  exit 0
fi
[ "$cmd" = all ] || { echo "all|compare" >&2; exit 2; }
OUT=$ROOT/runs/m1-storage/${2:?tag}; mkdir -p "$OUT"
RANGES=${3:-}
for i in $(seq 1 36); do $M1 true 2>/dev/null && break; sleep 5; done
{
  echo "GIVEN_RANGES=\$(cat <<'RANGES_END'"; [ -n "$RANGES" ] && cat "$RANGES"; echo "RANGES_END"; echo ")"
  cat <<'REMOTE'
D=nvme0n1
sec() { echo "@@ $1"; }
systemctl is-system-running --wait >/dev/null 2>&1
sec uname.txt; uname -r; cat /proc/sys/kernel/random/boot_id; uptime
sec lsblk.txt; lsblk -b -o NAME,SIZE,TYPE,FSTYPE,RO,MOUNTPOINTS,PHY-SEC,LOG-SEC,PARTTYPENAME /dev/nvme0n1 /dev/nvme0n2 /dev/nvme0n3 2>&1
sec sysfs.txt; for p in /sys/block/$D /sys/block/$D/${D}p*; do echo $(basename $p) start=$(cat $p/start 2>/dev/null || echo 0) sectors=$(cat $p/size) ro=$(cat $p/ro); done
echo lbs=$(cat /sys/block/$D/queue/logical_block_size) max_hw_kb=$(cat /sys/block/$D/queue/max_hw_sectors_kb) nr_requests=$(cat /sys/block/$D/queue/nr_requests)
sec nvme-sysfs.txt; for f in model firmware_rev state queue_count sqsize; do echo "$f=$(cat /sys/class/nvme/nvme0/$f 2>&1)"; done; grep -i nvme /proc/interrupts
sec dmesg.txt; dmesg 2>&1 | grep -iE 'nvme|sart|rtkit|EXT4|BTRFS|FAT-fs|recover|replay|journal|orphan|unclean|I/O error'
sec journal-fs.txt; journalctl -k -b --no-pager 2>/dev/null | grep -iE 'recover|replay|orphan|unclean|not cleanly|corrupt|I/O error|nvme|BTRFS|EXT4|FAT-fs' | tail -80
sec mounts.txt; findmnt -rn -o TARGET,SOURCE,FSTYPE,OPTIONS | grep -E 'nvme'; cat /proc/swaps
sec devicetree.txt; for n in /proc/device-tree/soc/nvme@* /proc/device-tree/soc/iommu@27bc50000 /proc/device-tree/soc/mbox@277408000; do echo "== $n"; for f in compatible reg reg-names interrupts power-domain-names; do [ -e $n/$f ] && { printf '%s: ' $f; od -An -tx1 $n/$f | tr -d '\n'; echo; }; done; done
sec nvme-id.txt
python3 - <<'PY' 2>&1
REMOTE
  cat "$HERE/linux_nvme_id.py"
  cat <<'REMOTE'
PY
sec ranges
R=$(if [ -n "$GIVEN_RANGES" ]; then echo "$GIVEN_RANGES"; else
for p in /sys/block/$D /sys/block/$D/${D}p*; do echo $(cat $p/start 2>/dev/null || echo 0) $(cat $p/size); done |
  awk 'NR == 1 { total = $2 / 8; print 0, 256; for (i = 1; i < 8; i++) print int(total * i / 8 / 256) * 256 + 4096, 256; print total - 256, 256; next }
       { s = $1 / 8; n = $2 / 8; if (n >= 768) { print s, 256; print s + int(n / 2 / 256) * 256, 256; print s + n - 256, 256 } else print s, n }' | sort -n -u
fi)
echo "$R"
sec hashes
while read lba blocks; do h=$(dd if=/dev/$D bs=4096 skip=$lba count=$blocks iflag=direct status=none | sha256sum | cut -d' ' -f1); echo NVT_HASH $lba $blocks $h; done <<< "$R"
sec smart-after.txt
python3 - <<'PY' 2>&1 | grep '^NV_SMART'
REMOTE
  cat "$HERE/linux_nvme_id.py"
  cat <<'REMOTE'
PY
sec end
REMOTE
} | $M1 'sudo -n bash -s' > "$OUT/session.txt" 2> "$OUT/session.err" || echo "ssh session ended with $?" >&2
awk -v out="$OUT" '/^@@ / { f = out "/" $2; printf "" > f; next } f { print > f }' "$OUT/session.txt"
grep -h "^NV_" "$OUT/nvme-id.txt" | cut -c1-500 || true
echo "ranges $(wc -l < "$OUT/ranges") hashes $(grep -c '^NVT_HASH [0-9]* [0-9]* [0-9a-f]\{64\}$' "$OUT/hashes" || true) $(cat "$OUT/session.err" | head -3)"
