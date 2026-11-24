#!/bin/bash
# SPDX-License-Identifier: MIT
# One bench slot around a boot that starts the controller: Linux baseline (read-only),
# the boot, Linux again, the verdict. Run it through the bench so nothing else boots in
# between:   BENCH_WHO=storage tools/bench.sh run .../nv_slot.sh <name> <level> <expected write_refused>
# The ranges are those of runs/m1-storage/linux-a/ranges (fixed, so every set is comparable);
# NV_RANGE_FILE names another set (linux-d/ranges: the layout since the second shrink of the SiliconForest partition, 13 November 2026; linux-c/ranges after the first, linux-b/ranges before that).
# NV_VERDICT_MODE=root: the boot ROOTS on the SiliconForest partition, read-only (guest/darwin-diskroot.in,
# launcher-diskroot.patch, read fence on); the partition must have been prepared with tools/dr_linux_root.sh transform.
# NV_VERDICT_MODE=part: the boot names the SiliconForest partition (NV_PART=1) and mounts it
# read-only (guest/darwin-disk-ro.in); the verdict then also wants the tree digest of
# NV_TREE_REF and our partition unchanged. <expected write_refused> may be "any".
# NV_VERDICT_MODE=write: a boot of the WRITE-FENCED build (../build-kc-rw.nix; guest/darwin-disk-rw.in,
# root on the memory disk): sfnvme_write=1, read fence on. After the boot Linux also checks our
# partition (tools/dr_linux_root.sh wcheck: e2fsck -fn, the written files' hashes).
#   NV_WMODE=unclean|downgrade  step 3c (guest/darwin-disk-rw2.in): how the boot ends; unset: dr11's boot
#   DR_REPLAY=1                 (unclean) Linux replays the journal afterwards (e2fsck -fy); unset: left for Darwin
#   NV_PREV_CONSOLE=<file>      the console of the boot whose files this one must find
set -uo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
N=${1:?name}; LEVEL=${2:?level}; EXPECT=${3:?expected write_refused}
R=${NV_RANGE_FILE:-runs/m1-storage/linux-a/ranges}; [ -s "$R" ] || { echo "no $R" >&2; exit 2; }
[ ! -e runs/m1-storage/STOP ] || { echo "runs/m1-storage/STOP exists: $(cat runs/m1-storage/STOP)" >&2; exit 3; }
stop() { echo "$1" | tee runs/m1-storage/STOP; exit 3; }
if [ "${NV_VERDICT_MODE:-}" = root ]; then
  # the root filesystem is our partition, read-only; the RAM root is uploaded as always (memory disk, fallback)
  . private/disk-root/sf-part.env || exit 2
  export NV_PART=1 NV_PART_LBA=$SF_PART_LBA NV_GUEST=${NV_GUEST:-$HERE/guest/darwin-diskroot.in} NV_TREE_REF=${NV_TREE_REF:-$ROOT/runs/m1-storage/ref-tree-diskroot.txt} NV_VERDICT_MODE
  # boot-uuid is the FILESYSTEM UUID: this kernel waits for the boot-uuid-media resource, which
  # Ext4FileSystemDriver publishes for the Linux-typed leaf media whose superblock has that UUID
  # (dr03 named the partition GUID, as QEMU's kernel wants, and waited until the time to live)
  export PCIE_LAB_PATCH=${PCIE_LAB_PATCH:-$HERE/launcher-diskroot.patch}
  export NV_BOOTARGS="rd=uuid boot-uuid=$SF_FSUUID launchdsuffix=sfro wdt=-1 sfnvme_readfence=1 sf_quiet_wait=0 sf_rtc_wait=0 ${NV_BOOTARGS:-}"
  [ -s "$NV_TREE_REF" ] || { echo "no tree reference $NV_TREE_REF" >&2; exit 2; }
fi
if [ "${NV_VERDICT_MODE:-}" = part ]; then
  . private/disk-root/sf-part.env || exit 2
  export NV_PART=1 NV_PART_LBA=$SF_PART_LBA NV_GUEST=${NV_GUEST:-$HERE/guest/darwin-disk-ro.in} NV_TREE_REF=${NV_TREE_REF:-$ROOT/runs/m1-storage/ref-tree-sw01.txt} NV_VERDICT_MODE
  [ -s "$NV_TREE_REF" ] || { echo "no tree reference $NV_TREE_REF" >&2; exit 2; }
fi
if [ "${NV_VERDICT_MODE:-}" = write ]; then
  . private/disk-root/sf-part.env || exit 2
  export PATH=$HOME/.nix-profile/bin:$PATH
  export NV_PART=1 NV_PART_LBA=$SF_PART_LBA NV_PART_BLOCKS=$SF_PART_BLOCKS NV_GUEST=${NV_GUEST:-$HERE/guest/darwin-disk-rw${NV_WMODE:+2}.in} NV_VERDICT_MODE NV_WMODE=${NV_WMODE:-}
  export NV_KC=${NV_KC:-$(cd "$HERE" && nix-build --extra-experimental-features 'nix-command flakes' --no-out-link build-kc-rw.nix 2>/dev/null | tail -1)}
  [ -f "$NV_KC/kernel" ] || { echo "no write-fenced collection" >&2; exit 2; }
  export NV_BOOTARGS="sfnvme_write=1 sfnvme_readfence=1 sf_quiet_wait=0 sf_rtc_wait=0 ${NV_BOOTARGS:-}"
  export NV_WCHECK=$ROOT/runs/m1-storage/$N-wcheck.txt
  # the write collection leaves six other-board kexts out; the launcher's required list is told so, for this boot only
  export PCIE_LAB_PATCH=${PCIE_LAB_PATCH:-$HERE/launcher-m1-members.patch}
fi
"$HERE/tools/linux_m1.sh" all "$N-before" "$R" || exit 2
[ "$(grep -c '^NVT_HASH [0-9]* [0-9]* [0-9a-f]\{64\}$' "runs/m1-storage/$N-before/hashes")" = "$(grep -c . "$R")" ] || { echo "the Linux baseline is incomplete; not booting" >&2; exit 2; }
NV_RANGES=$R "$HERE/tools/nv_boot.sh" "$N" "$LEVEL"
grep -q '"status": *"DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED"' "runs/m1-desktop-010/pcie-enum-attempt-$N/result.json" 2>/dev/null ||
  stop "$N: the cycle did not end DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED; all storage boots stopped"
"$HERE/tools/linux_m1.sh" all "$N-after" "$R" || stop "$N: Linux could not be read after the boot; all storage boots stopped"
if [ "${NV_VERDICT_MODE:-}" = write ]; then
  DR_OUT=$NV_WCHECK "$HERE/tools/dr_linux_root.sh" wcheck || stop "$N: our partition could not be checked from Linux after the write boot; all storage boots stopped"
fi
python3 -B "$HERE/tools/nv_verdict.py" "runs/m1-storage/$N-before" "runs/m1-storage/$N-after" "runs/m1-desktop-010/pcie-enum-attempt-$N/guest/console.bin" "$EXPECT" "$LEVEL" | tee "runs/m1-storage/$N-verdict.txt"
