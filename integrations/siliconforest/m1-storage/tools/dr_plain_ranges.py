#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The Linux side of a boot that printed none of the bench's lines (tools/dr_desk_slot.sh DR_PLAIN=1):
the range hashes and the SMART counters Linux took before and after the boot (tools/linux_m1.sh).
Ranges are classed as nv_verdict.py classes them: in a partition Linux does not mount (APFS, no
filesystem, the table copies, unallocated space) a range must be equal before and after; in ours
the boot wrote on purpose (reported, not compared); in a filesystem Linux has mounted it may have
moved under Linux itself (reported).
  dr_plain_ranges.py <before directory> <after directory> <our first block> <our blocks>
SF_BOOT_LBA / SF_BOOT_BLOCKS in the environment (sf-part.env): the SiliconForest boot partition, FAT, which
Linux never mounts and no boot writes (it is written from Linux between boots only): its ranges must be equal.
Prints DR_PLAIN lines; exit 1 when a range Linux never writes changed or a SMART counter moved."""
import re, sys
from pathlib import Path

import os
before, after, first, blocks = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
boot_first, boot_blocks = int(os.environ.get('SF_BOOT_LBA') or 0), int(os.environ.get('SF_BOOT_BLOCKS') or 0)
def hashes(d):
    return {(int(a), int(b)): h for a, b, h in re.findall(r'^NVT_HASH (\d+) (\d+) ([0-9a-f]{64})\s*$', (d / 'hashes').read_text(), re.M)}
table = []
for name, start, sectors in re.findall(r'^(nvme0n1p\d+) start=(\d+) sectors=(\d+)', (after / 'sysfs.txt').read_text(), re.M):
    fs = re.search(r'%s\s+\d+\s+part\s+(\S+)' % name, (after / 'lsblk.txt').read_text())
    table.append((int(start) // 8, int(sectors) // 8, fs.group(1) if fs else '?'))
def fstype(lba, n):
    for start, size, fs in table:
        if lba < start + size and lba + n > start: return fs
    return 'none'
hb, ha = hashes(before), hashes(after)
quiet = quiet_ok = ours = ours_written = linux = linux_moved = 0
bad = []
for (lba, n), h in sorted(ha.items()):
    same = hb.get((lba, n)) == h
    if lba >= first and lba + n <= first + blocks:
        ours += 1; ours_written += not same
    elif fstype(lba, n) in ('apfs', 'none') or (boot_blocks and lba >= boot_first and lba + n <= boot_first + boot_blocks):
        quiet += 1; quiet_ok += same
        if not same: bad.append('range %d+%d, which Linux never writes, CHANGED' % (lba, n))
    else:
        linux += 1; linux_moved += not same
print('DR_PLAIN ranges Linux never writes (table copies, APFS partitions, the boot partition, unallocated space): %d of %d unchanged; ours: %d (%d written in this boot); in filesystems Linux mounts: %d (%d moved under Linux)'
      % (quiet_ok, quiet, ours, ours_written, linux, linux_moved))
if len(ha) != len(hb) or quiet == 0: bad.append('hash sets: %d before, %d after, %d never written by Linux' % (len(hb), len(ha), quiet))
def smart(d, key):
    m = re.search(r'%s=(\d+|0x[0-9a-f]+)' % key, (d / 'smart-after.txt').read_text()); return m.group(1) if m else None
for key in ('unsafe_shutdowns', 'media_errors', 'num_err_log_entries', 'critical_warning'):
    b, a = smart(before, key), smart(after, key)
    print('DR_PLAIN smart %s %s -> %s' % (key, b, a))
    if b is None or b != a: bad.append('SMART %s %s -> %s' % (key, b, a))
for l in bad: print('DR_PLAIN STORAGE-FAIL ' + l)
print('DR_PLAIN linux_side=%s' % ('FAILED' if bad else 'ok'))
sys.exit(1 if bad else 0)
