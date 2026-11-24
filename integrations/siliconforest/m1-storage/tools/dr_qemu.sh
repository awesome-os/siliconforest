#!/bin/bash
# SPDX-License-Identifier: MIT
# QEMU rehearsal for the disk-root work (no M1, no bench): upstream's arm64 `virt` system from
# ../qemu-dev (unchanged) with a second virtio disk of 4096-byte sectors made by dr_qemu_disk.py.
# Usage: dr_qemu.sh <name> <guest script> <data image> [ro|rw] [timeout s]
#   output in runs/m1-storage/qemu-dr/<name>/ (serial.log, result.json); the system image is
#   runs/qemu-dev/disk-root/<name>.img. The guest script runs as `sh /usr/bin/dr-guest` and
#   frames its output with QDDESK_BEGIN / QDDESK_PASS / QDDESK_END.
#   DR_QEMU_FILES="src=/dest ..." further files for the system image
#   DR_QEMU_BOOTARGS   further kernel boot arguments (e.g. "rd=uuid boot-uuid=<fs UUID> launchdsuffix=sfro")
#   DR_QEMU_SINGLE=1     the boot is single-user (give -s in DR_QEMU_BOOTARGS)
#   DR_QEMU_KERNEL=debug the kernel that honours launchdsuffix (the release one does not)
#   DR_QEMU_UP         the line that ends the run (default QDDESK_END)
#   DR_QEMU_DISK2      a third, writable virtio disk (512-byte sectors): the stand-in for the memory disk
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd); cd "$ROOT"
export PATH=$HOME/.nix-profile/bin:$PATH
N=${1:?name}; GUEST=${2:?guest script}; DATA=$(readlink -f "${3:?data image}"); MODE=${4:-ro}; TIMEOUT=${5:-840}
Q=integrations/siliconforest/qemu-dev; M=integrations/siliconforest/m1-storage; OUT=runs/m1-storage/qemu-dr/$N; SYS=runs/qemu-dev/disk-root/$N.img
mkdir -p "$OUT" runs/qemu-dev/disk-root
TEST=$(cd $M && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-sfnvme-test.nix 2>/dev/null | tail -1)
FILES=(--file "$GUEST=/usr/bin/dr-guest" --file "$TEST/usr/bin/sfnvme-test=/usr/bin/sfnvme-test")
for f in ${DR_QEMU_FILES:-}; do FILES+=(--file "$f"); done
[ -z "${DR_QEMU_BOOTARGS:-}" ] || FILES+=(--bootargs "$DR_QEMU_BOOTARGS")
[ -z "${DR_QEMU_KERNEL:-}" ] || FILES+=(--kernel "$DR_QEMU_KERNEL")   # debug: the kernel that honours launchdsuffix (the release one does not)
EXTRA=(); [ -z "${DR_QEMU_DISK2:-}" ] || EXTRA=(--qemu-arg=-drive --qemu-arg="if=none,id=state,file=$(readlink -f "$DR_QEMU_DISK2"),format=raw" --qemu-arg=-device --qemu-arg=virtio-blk-pci,drive=state)
python3 -B $Q/qd_image.py --output "$SYS" "${FILES[@]}" > "$OUT/image.json" 2> "$OUT/image.err" || { tail -5 "$OUT/image.err" >&2; exit 2; }
RO=""; [ "$MODE" = ro ] && RO=",readonly=on"
# --up at the very end: the tool takes a screenshot one second after that marker, and the
# guest's serial output stopped for good when that happened in the middle of the script
# DR_QEMU_SINGLE=1: a single-user boot (-s in the boot arguments); its prompt is a bare "# ", which
# qd_run.py does not wait for: the same tool with that one pattern replaced, nothing of it edited
RUN=(python3 -B $Q/qd_run.py)
[ -z "${DR_QEMU_SINGLE:-}" ] || RUN=(python3 -B -c 'import re, sys; sys.path.insert(0, sys.argv.pop(1)); import qd_run; qd_run.PROMPT = re.compile(rb"# ?(\x1b\[[0-9;?]*[a-zA-Z]|\x00)*\s*$"); sys.exit(qd_run.main())' "$Q")
"${RUN[@]}" --image "$SYS" --out "$OUT" --command 'sh /usr/bin/dr-guest' --up "${DR_QEMU_UP:-QDDESK_END}" --shot-every 0 --timeout "$TIMEOUT" \
  --qemu-arg=-drive --qemu-arg="if=none,id=data,file=$DATA,format=raw$RO" \
  --qemu-arg=-device --qemu-arg=virtio-blk-pci,drive=data,logical_block_size=4096,physical_block_size=4096 "${EXTRA[@]}" > "$OUT/run.log" 2>&1 || true
tail -2 "$OUT/run.log"
[ -n "${DR_QEMU_KEEP:-}" ] || rm -f "$SYS"   # DR_QEMU_KEEP=1: keep the system image for inspection
