#!/bin/bash
# SPDX-License-Identifier: MIT
# Host checks of the read-only property of SFAppleNVMe:
#   1. the command gate (kernel/sf_nvme_gate.c, the kext's own source) over every opcode,
#      named destructive commands, read bounds and 20 million random entries;
#   2. the sources: one doorbell write, behind the gate; no opcode outside the allowlist
#      has a name; no host-to-device data direction on the I/O queue;
#   3. with a built kext ($1 = SFAppleNVMe.kext): its strings name no write path.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); K=$HERE/../kernel
OUT=$(mktemp -d); trap 'rm -rf "$OUT"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -o "$OUT/gate_host" "$HERE/gate_host.c" "$K/sf_nvme_gate.c"
"$OUT/gate_host"
#   1b. the identification of the SiliconForest partition in both GPT copies (kernel/sf_nvme_part.c)
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -o "$OUT/part_host" "$HERE/part_host.c" "$K/sf_nvme_part.c"
"$OUT/part_host" | grep -v '^PARTHOST ok'
#   1c. the gate of the write-fenced build (kernel/sf_nvme_wgate.c; NOT part of the read-only kext)
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -o "$OUT/wgate_host" "$HERE/wgate_host.c" "$K/sf_nvme_wgate.c" "$K/sf_nvme_gate.c"
"$OUT/wgate_host" | grep -v '^WGATEHOST ok'
#   1d. the driver-side wiring of the write-fenced build (kernel-rw/sf_nvme_wring.c)
cc -std=gnu11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -DSF_WRING_HOST_TEST=1 -I"$K" -o "$OUT/wring_host" "$HERE/wring_host.c" "$K/../kernel-rw/sf_nvme_wring.c" "$K/sf_nvme_wgate.c" "$K/sf_nvme_gate.c"
"$OUT/wring_host"
fail=0
check() { if [ "$2" != "$3" ]; then echo "SRCHOST FAIL: $1: $2, expected $3"; fail=1; fi; }
# the submission doorbells are written in exactly one statement, in SFAns::ring()
check "writes to the submission doorbells" "$(grep -c 'w32(.*ANS_LINEAR_\(A\|IO\)SQ_DB' "$K/sf_ans.cpp")" 1
check "doorbell writes outside sf_ans.cpp" "$(cat "$K/SFAppleNVMe.cpp" "$K/sf_soc.cpp" "$K/sf_nvme_gate.c" | grep -c 'SQ_DB' || true)" 0
# that statement comes after both gate calls in ring()
awk '/^SFAns::ring/,/^}/' "$K/sf_ans.cpp" > "$OUT/ring"
check "gate calls in ring()" "$(grep -c 'sf_nvme_gate(queue' "$OUT/ring")" 2
check "doorbell after the gate" "$(awk '/sf_nvme_gate\(queue/ {g=NR} /SQ_DB/ {d=NR} END {print (g && d > g) ? "yes" : "no"}' "$OUT/ring")" yes
# host-to-device DMA: the flag is set in one place, for odd opcodes, and refused on the I/O queue
check "uses of the host-to-device flag" "$(grep -c 'ANS_TCB_DMA_TO_DEVICE' "$K/sf_ans.cpp")" 2
# opcode names: only those of the allowlist exist anywhere in the driver
check "opcode names" "$(grep -ho 'SF_NVME_\(ADMIN\|IO\)_[A-Z_]*' "$K"/*.c "$K"/*.cpp "$K"/*.h | sort -u | tr '\n' ' ')" \
  "SF_NVME_ADMIN_CREATE_CQ SF_NVME_ADMIN_CREATE_SQ SF_NVME_ADMIN_DELETE_CQ SF_NVME_ADMIN_DELETE_SQ SF_NVME_ADMIN_GET_LOG_PAGE SF_NVME_ADMIN_IDENTIFY SF_NVME_IO_QID SF_NVME_IO_READ "
check "names of write-side commands in the driver" "$(grep -il 'nvme_cmd_write\|NVME_CMD_WRITE\|nvme_cmd_dsm\|nvme_admin_format\|nvme_admin_set_features\|nvme_cmd_flush\|NVME_CMD_FLUSH' "$K"/* | wc -l)" 0
# the write-fenced gate is in no source list of the read-only kext and is named by none of its files
check "read-only kext sources that mention the write gate" "$(grep -l 'wgate\|SF_NVMEW' "$K/CMakeLists.txt" "$K/SFAppleNVMe.cpp" "$K/sf_ans.cpp" "$K/sf_ans.h" "$K/sf_soc.cpp" "$K/sf_nvme_gate.c" "$K/sf_nvme_part.c" | wc -l)" 0
# the block device always reports write protection
check "reportWriteProtection" "$(grep -c 'reportWriteProtection.*\*isWriteProtected = true' "$K/SFAppleNVMe.cpp")" 1
# ---- the write-fenced build's sources (kernel + kernel-rw/rw.patch, as build-kext-rw.nix compiles them)
RW=$OUT/rw; "$HERE/../tools/rw_src.sh" make "$RW"
check "rw: writes to the submission doorbells" "$(grep -c 'w32(.*ANS_LINEAR_\(A\|IO\)SQ_DB' "$RW/sf_ans.cpp")" 1
check "rw: doorbell writes outside sf_ans.cpp" "$(cat "$RW/SFAppleNVMe.cpp" "$RW/sf_soc.cpp" "$RW"/sf_nvme_*.c | grep -c 'SQ_DB' || true)" 0
awk '/^SFAns::ring/,/^}/' "$RW/sf_ans.cpp" > "$OUT/rwring"
check "rw: ring() goes through sf_wring_prepare" "$(grep -c 'sf_wring_prepare(queue, tag' "$OUT/rwring")" 1
check "rw: the second look in ring(), on the slot" "$(grep -c 'sf_nvmew_inside(queue, sq, &fence)' "$OUT/rwring")" 1
check "rw: prepare, then the second look, then the doorbell, nothing but the barrier between" "$(awk '/sf_wring_prepare\(/ {p=NR} /sf_nvmew_inside\(/ {i=NR} /mem_sync\(\)/ {m=NR} /SQ_DB/ {d=NR} END {print (p && i > p && m > i && d == m + 1) ? "yes" : "no"}' "$OUT/rwring")" yes
check "rw: no return true before the doorbell" "$(awk '/return true/ {t=NR} /SQ_DB/ {d=NR} END {print (t > d) ? "yes" : "no"}' "$OUT/rwring")" yes
awk '/^sf_wring_prepare/,/^}/' "$K/../kernel-rw/sf_nvme_wring.c" > "$OUT/prep"
check "rw: gate calls in sf_wring_prepare (command, slot)" "$(grep -c 'sf_nvmew_gate(queue, \(sqe\|slot\), nsBlocks, f)' "$OUT/prep")" 2
check "rw: second look in sf_wring_prepare" "$(grep -c 'sf_nvmew_inside(queue, slot, f)' "$OUT/prep")" 1
check "rw: the TCB is written after the three looks" "$(awk '/sf_nvmew_inside\(/ {i=NR} /tcb\[SF_TCB_OPCODE\] = / {t=NR} END {print (t > i) ? "yes" : "no"}' "$OUT/prep")" yes
check "rw: host-to-device named in one function only" "$(grep -c 'SF_TCB_DMA_TO_DEVICE' "$RW/sf_ans.cpp" "$RW/SFAppleNVMe.cpp" | awk -F: '{n+=$2} END {print n}')" 1
check "rw: submitWrite looks at the range before a command exists" "$(awk '/^SFAns::submitWrite/,/^}/' "$RW/sf_ans.cpp" | awk '/sf_wabs_ok\(&fence, lba, blocks\)/ {a=NR} /sqe\[0\] = / {s=NR} END {print (a && s > a) ? "yes" : "no"}')" yes
check "rw: write commands are built in submitWrite (and the self-test scratch command, which has no queue)" "$(grep -c 'sqe\[0\] = SF_NVMEW_IO_WRITE' "$RW/sf_ans.cpp" "$RW/SFAppleNVMe.cpp" | awk -F: '{n+=$2} END {print n}')" 2
check "rw: sf_write checks before it queues" "$(awk '/^sf_write\(/,/^}/' "$RW/SFAppleNVMe.cpp" | awk '/write_admissible\(lba, blocks\)/ {a=NR} /sf_io\(kOpWrite/ {q=NR} END {print (a && q > a) ? "yes" : "no"}')" yes
check "rw: the probe has no data path" "$(awk '/case kCtlWProbe/,/case kCtlSeal/' "$RW/SFAppleNVMe.cpp" | grep -c 'sf_write\|sf_io\|submit\|sf_flush')" 0
check "rw: the fence is opened in one place, after the verification" "$(grep -c 'sf_wfence_open(' "$RW/SFAppleNVMe.cpp" "$RW/sf_ans.cpp" | awk -F: '{n+=$2} END {print n}')" 1
check "rw: the fence fields are assigned nowhere else" "$(grep -c 'fence\.\(first\|last\|open\) *= \|fence\.\(first\|last\|open\)++' "$RW/SFAppleNVMe.cpp" "$RW/sf_ans.cpp" | awk -F: '{n+=$2} END {print n}')" 0
check "rw: the whole disk still always reports write protection" "$(grep -c 'reportWriteProtection.*\*isWriteProtected = true' "$RW/SFAppleNVMe.cpp")" 1
check "rw: fence constants" "$(grep -c 'define SF_FENCE_FIRST  192633862ULL\|define SF_FENCE_BLOCKS 16035840ULL' "$RW/sf_nvme_wring.h")" 2
check "rw: no boot argument reaches the fence but sfnvme_write and the three partition arguments" "$(grep -o 'PE_parse_boot_argn("[a-z_]*"' "$RW/SFAppleNVMe.cpp" | sort -u | tr '\n' ' ')" 'PE_parse_boot_argn("sfnvme" PE_parse_boot_argn("sfnvme_delay" PE_parse_boot_argn("sfnvme_part" PE_parse_boot_argn("sfnvme_part_blocks" PE_parse_boot_argn("sfnvme_part_lba" PE_parse_boot_argn("sfnvme_readfence" PE_parse_boot_argn("sfnvme_ttl" PE_parse_boot_argn("sfnvme_verbose" PE_parse_boot_argn("sfnvme_write" '
check "rw: not in the default collection recipe" "$(grep -c 'SFAppleNVMeRW\|build-kext-rw' "$HERE/../../rebase/build-kc.nix" "$HERE/../../../../tools/default_kc.sh" | awk -F: '{n+=$2} END {print n}')" 0
if [ -n "${1:-}" ]; then
  BIN=$1/Contents/MacOS/SFAppleNVMe
  [ -f "$BIN" ] || { echo "SRCHOST FAIL: no binary at $BIN"; exit 1; }
  check "allowlist line in the binary" "$(strings -a "$BIN" | grep -c 'READ-ONLY build: allowlist admin 00 01 02 04 05 06, I/O 02')" 1
  echo "SRCHOST binary: $(sha256sum "$BIN" | cut -c1-16) $(stat -c %s "$BIN") bytes"
fi
[ $fail = 0 ] && echo "SRCHOST PASSED" || { echo "SRCHOST FAILED"; exit 1; }
