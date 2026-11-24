#!/bin/bash
# SPDX-License-Identifier: MIT
# One bench slot around a desktop boot rooted on the SiliconForest partition (dr_desk_boot.sh):
# Linux baseline, the boot, Linux again, the verdict of nv_verdict.py in root mode.
#   BENCH_WHO=disk tools/bench.sh run .../dr_desk_slot.sh <name> <hv|native> [upper bound s]
# DR_RW=1 (step 3d): the root read-write through the write-fenced build; afterwards Linux also
# checks our partition (dr_linux_root.sh wcheck) and the verdict runs in its write mode for a
# root (NV_WMODE=root). NV_PREV_CONSOLE=<console of the previous read-write boot>: the files and
# the desktop state that boot wrote must be found by this one.
# DR_HALT="<command>" (dr_desk_boot.sh): the boot is ended by the system's own command and the machine
# restarts itself; the verdict runs with NV_WMODE=halt, and the slot stops unless the controller sent NO
# reset. The desktop-state and clock checks of that mode are nv_verdict.py's.
# DR_PLAIN=1 (with DR_STAGE=disk; dr_desk_boot.sh): a session without sfbench=. No verdict (the bench prints nothing in
# such a boot); the slot's own checks are below.
# DR_FSCK_OLD_SCAN=<output of tools/sf_freed_inode_repair.py scan, made from Linux before the boot> (plain boots):
# e2fsck -fn returning 4 is accepted when it objects only to what the driver of before boot dr41 left (see below).
# DR_STAGE=1|disk, DR_STAGE_STOP=1 (dr_desk_boot.sh): the boot goes through the SiliconForest stage. A read-back
# boot (DR_STAGE_STOP=1) never enters the kernel: instead of the verdict the slot wants the stage's read-back
# equal in every part, our partition clean and untouched (e2fsck -fn, the superblock's state, every range in it
# and in the unallocated space behind it equal in Linux before and after).
set -uo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/../../.." && pwd); cd "$ROOT"
N=${1:?name}; MODE=${2:?hv or native}; LIMIT=${3:-200}
R=${NV_RANGE_FILE:-runs/m1-storage/linux-e/ranges}; [ -s "$R" ] || { echo "no $R" >&2; exit 2; }
[ ! -e runs/m1-storage/STOP ] || { echo "runs/m1-storage/STOP exists: $(cat runs/m1-storage/STOP)" >&2; exit 3; }
stop() { echo "$1" | tee runs/m1-storage/STOP; exit 3; }
. private/disk-root/sf-part.env || exit 2
"$HERE/tools/linux_m1.sh" all "$N-before" "$R" || exit 2
[ "$(grep -c '^NVT_HASH [0-9]* [0-9]* [0-9a-f]\{64\}$' "runs/m1-storage/$N-before/hashes")" = "$(grep -c . "$R")" ] || { echo "the Linux baseline is incomplete; not booting" >&2; exit 2; }
DR_RANGES=$R "$HERE/tools/dr_desk_boot.sh" "$N" "$MODE" "$LIMIT"
C=runs/m1-desktop-010/pcie-enum-attempt-$N
# natively nothing is typed back to the controller the same way: its own status is checked for Linux, the rest by the verdict
grep -q '"status": *"[A-Z_]*LINUX_VERIFIED"' "$C/result.json" 2>/dev/null || stop "$N: the cycle did not end with Linux verified; all storage boots stopped"
"$HERE/tools/linux_m1.sh" all "$N-after" "$R" || stop "$N: Linux could not be read after the boot; all storage boots stopped"
export SF_BOOT_LBA=${SF_BOOT_LBA:-} SF_BOOT_BLOCKS=${SF_BOOT_BLOCKS:-}   # sf-part.env: the boot partition (dr_plain_ranges.py)
if [ -n "${DR_STAGE1:-}" ] && [ -z "${DR_STAGE1_GO:-}" ]; then
  # The installed stage 1 with a boot partition that does not exist, or with a file it cannot start: it must
  # stop by itself, with its reason on the console, enter no kernel and change nothing on the disk (it starts
  # the storage controller to read the partition table, as every stage 1 does).
  sleep 30
  DR_OUT=$ROOT/runs/m1-storage/$N-wcheck.txt "$HERE/tools/dr_linux_root.sh" wcheck > /dev/null || stop "$N: our partition could not be checked from Linux after the stage 1 test; all storage boots stopped"
  CON=$(mktemp); strings -n 4 "$C/guest/console.bin" | sed -e 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g' > "$CON"
  {
    grep -a 'SFL_FSCK\|SFL_SB_FIRST Filesystem state\|SFL_SB_FIRST Journal start' "runs/m1-storage/$N-wcheck.txt" | sed 's/^/DR_STAGE1 /'
    python3 -B "$HERE/tools/dr_plain_ranges.py" "runs/m1-storage/$N-before" "runs/m1-storage/$N-after" "$SF_PART_LBA" "$SF_PART_BLOCKS" | sed 's/^DR_PLAIN/DR_STAGE1/' || true
    echo "DR_STAGE1 reset_attempts=$(sed -n 's/.*"reset_attempts": *\([0-9]*\).*/\1/p' "$C/result.json" | head -1)"
    want() { if grep -a -q -- "$2" "$CON"; then echo "DR_STAGE1 ok   $1: $(grep -a -- "$2" "$CON" | head -1 | cut -c1-200)"; else echo "DR_STAGE1 FAIL $1"; fi; }
    wantnot() { if grep -a -q -- "$2" "$CON"; then echo "DR_STAGE1 FAIL $1: $(grep -a -- "$2" "$CON" | head -1 | cut -c1-160)"; else echo "DR_STAGE1 ok   $1"; fi; }
    want "it is our stage 1" 'siliconforest-stage1'
    want "it found no payload to start" 'No valid payload found'
    want "it stopped" '^SF_STOPPED'
    want "it said what to do" 'Hold the power button'
    wantnot "no kernel was entered" 'Darwin Kernel Version'
    wantnot "no proxy was offered" 'Running proxy'
    echo "DR_STAGE1 reason: $(grep -a 'Chainload failed\|chainload: image\|chainload: .*overlap' "$CON" | head -2 | tr '\n' ';' | cut -c1-200)"
    grep -a -i 'chainload\|nvme\|payload\|SiliconForest\|partition\|GPT\|fat' "$CON" | cut -c1-200 | head -40 | sed 's/^/DR_STAGE1 line /'
  } | tee "runs/m1-storage/$N-verdict.txt"
  rm -f "$CON"; V=runs/m1-storage/$N-verdict.txt
  grep -q '^DR_STAGE1 SFL_FSCK_RC 0$' "$V" && grep -q '^DR_STAGE1 SFL_SB_FIRST Filesystem state: *clean$' "$V" && grep -q '^DR_STAGE1 linux_side=ok$' "$V" \
    || stop "$N: after the stage 1 test our partition is not clean in Linux, or a range Linux never writes or a SMART counter changed; all storage boots stopped"
  if grep -q '^DR_STAGE1 FAIL' "$V"; then echo "DR_STAGE1 storage checks passed; $(grep -c '^DR_STAGE1 FAIL' "$V") line(s) FAILED"; exit 1; fi
  echo "DR_STAGE1 PASSED: stage 1 started nothing (its reason is the line above), stopped by itself, entered nothing; the disk is unchanged"
  exit 0
fi
if [ "${DR_STAGE_STOP:-0}" = 1 ]; then
  sleep 30
  DR_OUT=$ROOT/runs/m1-storage/$N-wcheck.txt "$HERE/tools/dr_linux_root.sh" wcheck > /dev/null || stop "$N: our partition could not be checked from Linux after the read-back boot; all storage boots stopped"
  {
    grep -a 'SFL_FSCK_RC\|SFL_SB_FIRST Filesystem state\|SFL_SB_FIRST Journal start' "runs/m1-storage/$N-wcheck.txt" | sed 's/^/DR_STOP /'
    # every range from our partition's first block to the next partition (ours and the unallocated space) must be unchanged
    join -j1 <(awk '/^NVT_HASH/ {print $2"+"$3, $4}' "runs/m1-storage/$N-before/hashes" | sort) <(awk '/^NVT_HASH/ {print $2"+"$3, $4}' "runs/m1-storage/$N-after/hashes" | sort) |
      awk -v first="$SF_PART_LBA" -v blocks="$SF_PART_BLOCKS" '{ split($1, r, "+"); ours = (r[1] >= first && r[1] < first + blocks + 741417); if ($2 == $3) { e++; oe += ours } else { d++; od += ours; print "DR_STOP changed in Linux between before and after:", $1, (ours ? "OURS OR THE FREED SPACE" : "") } }
        END { printf "DR_STOP ranges equal=%d different=%d; in our partition and the space behind it equal=%d different=%d\n", e, d, oe, od }'
    grep -a 'STAGE_READBACK' "$C/guest/worker.log" | grep -a -o '"[a-z_]*": {"bytes": [0-9]*, "equal": [a-z]*\|"equal_to_the_launchers": [a-z]*\|"cmdline_at_108": "[^"]*"' | sed -e 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g' -e 's/^/DR_STOP readback /'
    strings -n 6 "$C/guest/console.bin" | grep -a 'PDSF: ' | sed -e 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g' -e 's/^/DR_STOP console /' | cut -c1-300
  } | tee "runs/m1-storage/$N-verdict.txt"
  V=runs/m1-storage/$N-verdict.txt
  grep -q '^DR_STOP SFL_FSCK_RC 0$' "$V" && grep -q 'in our partition and the space behind it equal=[1-9][0-9]* different=0$' "$V" && grep -q '"equal_to_the_launchers": true' "$V" \
    && ! grep -q '"equal": false' "$V" && grep -q 'DR_STOP console PDSF: stop requested' "$V" && ! grep -a -q 'Darwin Kernel Version' "$C/guest/console.bin" \
    || stop "$N: the stage read-back boot did not pass; all storage boots stopped"
  echo "DR_STOP PASSED: the stage placed what the launcher would have placed, the kernel was not entered, our partition is untouched"
  exit 0
fi
if [ "${DR_PLAIN:-0}" = 1 ] || [ -n "${DR_STAGE1_GO:-}" ]; then
  # A session without sfbench= prints none of the bench's lines: no verdict. Storage (these stop the stream): the
  # machine came back by itself, our partition is clean in Linux, every range Linux never writes is as before. The rest is
  # reported line by line (PLAIN ok / PLAIN FAIL) from what the session and sf-halt themselves print.
  sleep 30
  DR_OUT=$ROOT/runs/m1-storage/$N-wcheck.txt "$HERE/tools/dr_linux_root.sh" wcheck > /dev/null || stop "$N: our partition could not be checked from Linux after the plain boot; all storage boots stopped"
  CON=$(mktemp); strings -n 4 "$C/guest/console.bin" | sed -e 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g' > "$CON"
  {
    grep -a 'SFL_FSCK\|SFL_SB_FIRST Filesystem state\|SFL_SB_FIRST Journal start' "runs/m1-storage/$N-wcheck.txt" | sed 's/^/DR_PLAIN /'
    python3 -B "$HERE/tools/dr_plain_ranges.py" "runs/m1-storage/$N-before" "runs/m1-storage/$N-after" "$SF_PART_LBA" "$SF_PART_BLOCKS" || true
    echo "DR_PLAIN reset_attempts=$(sed -n 's/.*"reset_attempts": *\([0-9]*\).*/\1/p' "$C/result.json" | head -1)"
    want() { if grep -a -q -- "$2" "$CON"; then echo "DR_PLAIN ok   $1: $(grep -a -- "$2" "$CON" | head -1 | cut -c1-200)"; else echo "DR_PLAIN FAIL $1"; fi; }
    wantnot() { if grep -a -q -- "$2" "$CON"; then echo "DR_PLAIN FAIL $1: $(grep -a -- "$2" "$CON" | head -1 | cut -c1-160)"; else echo "DR_PLAIN ok   $1"; fi; }
    if [ -n "${DR_STAGE1_GO:-}" ]; then
      # the installed chain: stage 1 (uploaded, entered as iBoot enters it) -> sf/boot.bin from the boot
      # partition -> sf/kernel.bin from the boot partition -> the kernel; nothing else was uploaded
      want "stage 1 is ours" 'siliconforest-stage1'
      want "stage 1 read sf/boot.bin from the boot partition" 'File read successfully'
      want "stage 1 started it" 'chainload: image 0x'
      wantnot "stage 1 did not refuse it" 'overlaps the live heap'
      want "stage 2 is ours" 'siliconforest-boot-v'
      want "stage 2 read sf/kernel.bin itself" 'PDSF: reading sf/kernel.bin'
      want "the records came from the boot partition" 'PDSF: SiliconForest payload at .*read from the boot partition'
      want "the kernel record's checksum" 'PDSF: kernel .* crc [0-9a-f]* ok'
      want "the root rule" 'PDSF: root: the disk filesystem this stage was built for'
      want "the kernel was entered" 'PDSF: entering XNU'
      wantnot "nothing stopped" '^SF_STOPPED'
      [ "$(grep -a -c 'File read successfully' "$CON")" = 2 ] && echo "DR_PLAIN ok   two files were read from the boot partition" || echo "DR_PLAIN FAIL two files were read from the boot partition"
      grep -a 'chainload\|Partition offset\|File size\|nvme: \|PDSF: \(reading\|SiliconForest\|config\|kernel\|root\|XNU memory\|boot seed\|entering\)\|##m1n1_ver\|^m1n1 [0-9a-f]\|Vectoring\|payload' "$CON" | cut -c1-200 | head -40 | sed 's/^/DR_PLAIN chain /'
    fi
    wantnot "the boot arguments carry no sfbench=" 'sfbench='
    wantnot "nothing of the bench ran (no NV_ line)" '^NV_[A-Z]'
    want "the session is the disk's own, started by launchd, without the bench" 'SF_SESSION begin seconds=[0-9]* bench=no started_by=launchd'
    want "sf-boot ran" 'SF_BOOT clock_rc=0 run_and_tmp=cleaned'
    want "the renderer" 'WMSTAT selected renderer='
    want "the network came up by sf-netd" 'GPUDESK_NET_UP_RC=0 by=sf-netd'
    want "a host on the local network answered" 'GPUDESK_LAN_PING_RC=0'
    want "a host beyond it answered" 'GPUDESK_PING_RC=0'
    want "the desktop ended with 0" 'GPUDESK_DESKTOP_RC=0'
    want "the session's end" 'SF_SESSION end request='
    want "sf-halt: the root downgraded" 'SF_HALT downgrade: mount rc=0, root is now read-only'
    want "sf-halt: the controller stopped cleanly" 'SF_HALT stop errno=0 state=stopped-clean'
    want "sf-halt asked for the restart" 'SF_HALT restarting now'
    wantnot "no panic" 'panic(cpu'
    grep -a 'SF_HALT processes\|SF_SESSION\|GPUDESK_\(NET\|LAN\|PING\|TOUCHBAR\|INPUT\|DESKTOP\|COREAUDIOD\)\|WMSTAT selected\|WMSCALE initial\|SF_BOOT' "$CON" | cut -c1-240 | head -40 | sed 's/^/DR_PLAIN line /'
  } | tee "runs/m1-storage/$N-verdict.txt"
  rm -f "$CON"; V=runs/m1-storage/$N-verdict.txt
  if [ -n "${DR_FSCK_OLD_SCAN:-}" ] && grep -q '^DR_PLAIN SFL_FSCK_RC 4$' "$V"; then
    # The disk still holds inodes the driver of before boot dr41 freed (extents flag over a zeroed i_block):
    # e2fsck -fn returns 4 until they are reused or repaired. Accepted ONLY when a scan from Linux now finds no
    # such inode that the scan of before (DR_FSCK_OLD_SCAN) does not list, every inode e2fsck calls garbage lies
    # in an inode-table block that is more than half such inodes, and e2fsck says nothing else.
    scp -q -o HostKeyAlias=10.1.1.127 -o BatchMode=yes "$HERE/tools/sf_freed_inode_repair.py" chrisk@10.1.1.211:/tmp/sf_freed_inode_repair.py \
      && ssh -o HostKeyAlias=10.1.1.127 -o BatchMode=yes -o ConnectTimeout=8 chrisk@10.1.1.211 'sudo -n python3 -I -B /tmp/sf_freed_inode_repair.py scan /dev/nvme0n1p3' > "runs/m1-storage/$N-freed-inode-scan.txt" 2>&1 \
      || stop "$N: the freed-inode scan could not be run from Linux; all storage boots stopped"
    python3 -I -B - "$DR_FSCK_OLD_SCAN" "runs/m1-storage/$N-freed-inode-scan.txt" "runs/m1-storage/$N-wcheck.txt" <<'PY' | tee -a "$V"
import re, sys
def scan(path):
    inodes, blocks = set(), []
    for line in open(path):
        if line.startswith('SFREPAIR inodes:') or line.startswith('SFREPAIR blocks over half:'):
            for a, b in re.findall(r'(\d+)(?:-(\d+))?', line.split(':', 1)[1]):
                if line.startswith('SFREPAIR inodes:'): inodes.update(range(int(a), int(b or a) + 1))
                else: blocks.append((int(a), int(b or a)))
    return inodes, blocks
old, _ = scan(sys.argv[1]); new, blocks = scan(sys.argv[2])
w = open(sys.argv[3], errors='replace').read()
garbage = [int(n) for n in re.search(r'^SFL_FSCK_GARBAGE (.*)$', w, re.M).group(1).split()]
other = re.findall(r'^SFL_FSCK_OTHER (.*)$', w, re.M)
fresh = sorted(new - old); outside = [g for g in garbage if not any(a <= g <= b for a, b in blocks)]
print('DR_PLAIN freed inodes of the old kind: %d before, %d now, %d of them not listed before%s' % (len(old), len(new), len(fresh), (': ' + ' '.join(map(str, fresh[:12]))) if fresh else ''))
print('DR_PLAIN e2fsck calls %d inodes garbage; %d of them outside a block that is more than half old freed inodes%s' % (len(garbage), len(outside), (': ' + ' '.join(map(str, outside[:12]))) if outside else ''))
print('DR_PLAIN e2fsck other complaints: %d%s' % (len(other), (': ' + ' | '.join(other[:3])) if other else ''))
print('DR_PLAIN fsck_known_only=%s' % ('yes' if old and not fresh and not outside and not other else 'NO'))
PY
    grep -q '^DR_PLAIN fsck_known_only=yes$' "$V" && sed -i 's/^DR_PLAIN SFL_FSCK_RC 4$/DR_PLAIN SFL_FSCK_RC 0 (was 4: only the old driver'"'"'s freed inodes, none new; see the lines below)/' "$V"
  fi
  grep -q '^DR_PLAIN SFL_FSCK_RC 0' "$V" && grep -q '^DR_PLAIN SFL_SB_FIRST Filesystem state: *clean$' "$V" && grep -q '^DR_PLAIN linux_side=ok$' "$V" && grep -q '^DR_PLAIN reset_attempts=0$' "$V" \
    || stop "$N: after the plain boot our partition is not clean in Linux, a range Linux never writes or a SMART counter changed, or the controller had to reset; all storage boots stopped"
  if grep -q '^DR_PLAIN FAIL' "$V"; then echo "DR_PLAIN storage checks passed; $(grep -c '^DR_PLAIN FAIL' "$V") session line(s) FAILED (no stop file: not a storage matter)"; exit 1; fi
  echo "DR_PLAIN PASSED: the plain session came up, reached the network, ended in its own restart; our partition is clean and nothing outside it changed"
  exit 0
fi
if [ "${DR_RW:-0}" = 1 ]; then
  sleep 30
  export NV_WCHECK=$ROOT/runs/m1-storage/$N-wcheck.txt
  DR_OUT=$NV_WCHECK "$HERE/tools/dr_linux_root.sh" wcheck || stop "$N: our partition could not be checked from Linux after the read-write boot; all storage boots stopped"
  [ "${DR_HALT:-}" != menu ] || export NV_HALT_MENU=1
  if [ -n "${DR_HALT:-}" ]; then export NV_RESETS=$(sed -n 's/.*"reset_attempts": *\([0-9]*\).*/\1/p' "$C/result.json" | head -1); fi
  NV_VERDICT_MODE=write NV_WMODE=$([ -n "${DR_HALT:-}" ] && echo halt || echo root) NV_PART_LBA=$SF_PART_LBA NV_PART_BLOCKS=$SF_PART_BLOCKS \
    python3 -B "$HERE/tools/nv_verdict.py" "runs/m1-storage/$N-before" "runs/m1-storage/$N-after" "$C/guest/console.bin" any 3 | tee "runs/m1-storage/$N-verdict.txt"
else
NV_VERDICT_MODE=root NV_PART_LBA=$SF_PART_LBA NV_TREE_REF=${NV_TREE_REF:-$ROOT/runs/m1-storage/ref-tree-diskroot.txt} \
  python3 -B "$HERE/tools/nv_verdict.py" "runs/m1-storage/$N-before" "runs/m1-storage/$N-after" "$C/guest/console.bin" any 3 | tee "runs/m1-storage/$N-verdict.txt"
fi
strings -n 4 "$C/guest/console.bin" | grep -a 'GPUDESK\|WMSTAT\|NV_EARLY\|WMSCALE\|WMSET\|WMAUTO\|SF_\|NV_CLOCK\|NV_DESKSTATE\|NV_LEFT\|NV_SF' | sed 's/[0-9A-Fa-f]\{8\}-[0-9A-Fa-f-]\{27\}/GUID/g' | cut -c1-260 | head -150 | tee "runs/m1-storage/$N-desktop.txt"
