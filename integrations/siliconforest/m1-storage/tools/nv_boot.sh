#!/bin/bash
# SPDX-License-Identifier: MIT
# One boot of the M1 with the SFAppleNVMe collection and a guest script, through
# tools/pcie_iterate.sh, which queues for the bench itself: do not wrap it. The collection is
# handed over with PCIE_KC; nothing shared is written.
# Usage: nv_boot.sh <name, prefix nv> <level 1|2|3>
#   level 1: observe only (guest/darwin-nvme-observe.in); 2: controller + /dev/sfnvme0;
#   3: also the write-protected block device. Levels 2 and 3 START THE CONTROLLER: the
#   first such boot needs the reviewer's go (docs/roadmap/issues/SF-R-storage.md).
#   NV_KC        collection store path (default: build ../build-kc.nix)
#   NV_RANGES    "<lba> <blocks>" lines for the hashes (levels 2, 3; from tools/linux_ranges.sh)
#   NV_RUNTIME   upper bound of the guest's run in seconds, 120 to 600 (default 300)
#   NV_TTL       seconds after which the driver shuts the controller down by itself
#                (default 120: a boot reaches the guest script about 12 s after the kernel starts)
#   NV_SEQ_LBA / NV_SEQ_MIB   start and size of the sequential read (default 262144 / 512)
#   NV_PARTS     partitions the disk must show at level 3, all of them guarded (default 7)
#   NV_SECOND=1  after the clean stop, start the controller once more and stop it again
#   NV_BOOTARGS  further kernel boot arguments
#   NV_CPUS      guest processors (default 8)
#   NV_GUEST     another guest template for levels 2 and 3 (default guest/darwin-nvme-test.in)
#   NV_PART=1    (level 3) name the SiliconForest partition to the driver: sfnvme_part,
#                sfnvme_part_lba, sfnvme_part_blocks from private/disk-root/sf-part.env (the
#                GUID is in no file under git); the guard then leaves that one partition alone.
#                NV_PARTS then counts all partitions (default 8), one of them not guarded
# Boot names: nv01.. (the read-only driver's own boots), dr01.. (the disk-root stream).
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH BENCH_WHO=${BENCH_WHO:-storage}
# What this script stages from Nix besides the collection (tools, libraries, the audio server) are still
# the builds of the pinned revision, which run on the tip's kernel and libSystem (SF-V-rebase.md).
export SF_PD=pin
N=${1:?name}; LEVEL=${2:?level 1, 2 or 3}
case "$N" in nv[0-9]*|dr[0-9]*) ;; *) echo "boot names are nv01, nv02, ... or dr01, dr02, ..." >&2; exit 2;; esac
case "$LEVEL" in 1|2|3) ;; *) echo "level is 1, 2 or 3" >&2; exit 2;; esac
[ ! -e "runs/m1-desktop-010/pcie-enum-attempt-$N" ] || { echo "boot name $N was used already" >&2; exit 2; }
[ ! -e runs/bench.finisher.failed ] || { echo "runs/bench.finisher.failed exists: the M1 did not return to Linux; stop" >&2; exit 3; }
[ ! -e runs/m1-storage/STOP ] || { echo "runs/m1-storage/STOP exists: $(cat runs/m1-storage/STOP)" >&2; exit 3; }
NIX="nix-build --extra-experimental-features nix-command --extra-experimental-features flakes --no-out-link"
KC=${NV_KC:-$("$ROOT/tools/default_kc.sh" storage)}
[ -f "$KC/kernel" ] || { tail -30 "${TMPDIR:-/tmp}/nv-kc-build.$$.log" >&2; echo "no collection" >&2; exit 2; }
rm -f "${TMPDIR:-/tmp}/nv-kc-build.$$.log"
python3 "$ROOT/integrations/siliconforest/m1-rust-kext/tools/kc_overlap.py" "$KC/kernel" | tail -1
"$HERE/host-test/run-host-tests.sh" > /dev/null || { echo "host tests of the read-only gate failed; not booting" >&2; exit 2; }
TEST=$(cd "$HERE" && $NIX build-sfnvme-test.nix 2>/dev/null | tail -1)
[ -x "$TEST/usr/bin/sfnvme-test" ] || { echo "sfnvme-test build failed" >&2; exit 2; }
RUNTIME=${NV_RUNTIME:-300}
[ "$RUNTIME" -ge 120 ] && [ "$RUNTIME" -le 600 ] || { echo "boots run 120 to 600 s" >&2; exit 2; }
TTL=${NV_TTL:-120}
W=$(mktemp -d)
FILES="$TEST/usr/bin/sfnvme-test=/usr/bin/sfnvme-test"
if [ "$LEVEL" = 1 ]; then
  cp "$HERE/guest/darwin-nvme-observe.in" "$W/guest"
else
  [ -f "${NV_RANGES:-}" ] || { echo "NV_RANGES=<range file> is needed for levels 2 and 3" >&2; exit 2; }
  PARTARGS=""; PARTS=${NV_PARTS:-7}; SF_PART_LBA=0; SF_PART_BLOCKS=0
  if [ "${NV_PART:-0}" = 1 ]; then
    [ "$LEVEL" = 3 ] || { echo "NV_PART=1 needs level 3" >&2; exit 2; }
    . "$ROOT/private/disk-root/sf-part.env" || { echo "no private/disk-root/sf-part.env" >&2; exit 2; }
    [ -n "$SF_PARTUUID" ] && [ "$SF_PART_LBA" -gt 0 ] && [ "$SF_PART_BLOCKS" -gt 0 ] || { echo "sf-part.env is incomplete" >&2; exit 2; }
    PARTARGS="sfnvme_part=$SF_PARTUUID sfnvme_part_lba=$SF_PART_LBA sfnvme_part_blocks=$SF_PART_BLOCKS"; PARTS=${NV_PARTS:-8}
  fi
  sed -e "s/@SEQ_LBA@/${NV_SEQ_LBA:-262144}/g" -e "s/@SEQ_MIB@/${NV_SEQ_MIB:-512}/g" -e "s/@PARTS@/$PARTS/g" -e "s/@GUARDED@/$((PARTS - 1))/g" \
    -e "s/@SECOND@/${NV_SECOND:-0}/g" -e "s/@PART_LBA@/$SF_PART_LBA/g" -e "s/@PART_BLOCKS@/$SF_PART_BLOCKS/g" -e "s/@NAME@/$N/g" -e "s/@WMODE@/${NV_WMODE:-}/g" "${NV_GUEST:-$HERE/guest/darwin-nvme-test.in}" > "$W/guest"
  FILES="$FILES;$NV_RANGES=/etc/sfnvme-ranges"
fi
PCIE_KC="$KC" PCIE_TIMEWAIT="${NV_TIMEWAIT:-1,5}" PCIE_BOOTARGS="cpus=${NV_CPUS:-8} sfnvme=$LEVEL sfnvme_ttl=$TTL ${PARTARGS:-} ${NV_BOOTARGS:-}" \
  PCIE_GUEST_TEMPLATE="$W/guest" PCIE_SKIP_BUILD=1 PCIE_WIFI=1 PCIE_WIFI_JOIN=1 PCIE_EXTRA_FILES="$FILES" LINES_MAX=1 \
  tools/pcie_iterate.sh "$N" "$RUNTIME" 2>&1 |
  grep -a "\"status\"\|rror\|delivered\|GUEST_CPUS\|No space\|BENCH_TIMES" | cut -c1-240 || true
rm -rf "$W"
grep -o '"status": *"[^"]*"' "runs/m1-desktop-010/pcie-enum-attempt-$N/result.json" 2>/dev/null || echo "no result.json"
"$HERE/tools/nv_summary.sh" "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin"
