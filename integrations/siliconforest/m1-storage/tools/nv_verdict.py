#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verdict of a boot that started the NVMe controller read-only.

  nv_verdict.py <linux-before dir> <linux-after dir> <console.bin> <expected write_refused> [level, default 2]

The proof it checks, honest about Linux' own writes:
  - ranges on partitions Linux does not modify (the GPTs and the APFS partitions) must be
    identical in Linux before and after the Darwin boot (nothing changed) and identical to
    what Darwin read (the reads are right);
  - ranges inside Linux' mounted filesystems are compared too, but a difference there is
    only reported: where Linux-before equals Linux-after, Darwin must equal both; where
    Linux itself changed the range around its reboot, nothing can be attributed.
  - Linux afterwards: no recovery/replay lines, its filesystems mounted, unsafe_shutdowns
    unchanged; Darwin: gate_rejected 0, write_refused as expected, stopped cleanly,
    Identify equal to Linux'.
Prints NV_VERDICT lines; exit 0 only when everything holds. A failure writes
runs/m1-storage/STOP so that no further storage boot starts.
"""
import re, subprocess, sys
from pathlib import Path

import os
before, after, console = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
expect_refused = None if sys.argv[4] == 'any' else int(sys.argv[4])
# NV_VERDICT_MODE=part: the SiliconForest partition (first block NV_PART_LBA) was named to the driver and
# mounted read-only; NV_TREE_REF holds the tree digest of the image it was copied from.
part_mode = os.environ.get('NV_VERDICT_MODE') == 'part'
root_mode = os.environ.get('NV_VERDICT_MODE') == 'root'     # the root filesystem was our partition, read-only (guest/darwin-diskroot.in)
# NV_VERDICT_MODE=write: the write-fenced build wrote files through ext4 on the fenced device (guest/darwin-disk-rw.in);
# NV_WCHECK is the output of tools/dr_linux_root.sh wcheck, made from Linux after the boot.
write_mode = os.environ.get('NV_VERDICT_MODE') == 'write'
wmode = os.environ.get('NV_WMODE', '') if write_mode else ''     # '', 'unclean' or 'downgrade' (step 3c, guest/darwin-disk-rw2.in), 'root' (3d, guest/darwin-diskroot-rw.in)
halt = wmode == 'halt'      # 3d ended by the system's own command (guest/darwin-diskroot-halt.in: sf-halt prints the end's lines, the machine restarts itself)
if halt: wmode = 'root'
rootw = wmode == 'root'     # the ROOT was the fenced device, read-write; driver log lines may be absent from the console (noiolog)
ours_lba = int(os.environ.get('NV_PART_LBA', '0'))
level = int(sys.argv[5]) if len(sys.argv) > 5 else 2
bad = []
def check(ok, text):
    print('NV_VERDICT %s %s' % ('ok  ' if ok else 'FAIL', text))
    if not ok: bad.append(text)

def hashes(text, prefix='NVT_HASH'):
    return {(int(a), int(b)): h for a, b, h in re.findall(r'^%s (\d+) (\d+) ([0-9a-f]{64})\s*$' % prefix, text, re.M)}
def parts(d):
    out = []
    for name, start, sectors in re.findall(r'^(nvme0n1p\d+) start=(\d+) sectors=(\d+)', (d / 'sysfs.txt').read_text(), re.M):
        fstype = re.search(r'%s\s+\d+\s+part\s+(\S+)' % name, (d / 'lsblk.txt').read_text())
        out.append((name[7:], int(start) // 8, int(sectors) // 8, fstype.group(1) if fstype else '?'))
    return out
def where(lba, blocks, table):
    for name, start, size, fs in table:
        if lba >= start and lba + blocks <= start + size: return name, fs
        if lba < start + size and lba + blocks > start: return name + '+gap', fs
    return 'gpt/gap', 'none'
def smart(text, key):
    m = re.search(r'%s=(\d+)' % key, text); return int(m.group(1)) if m else None

con = subprocess.run(['strings', '-n', '6', str(console)], capture_output=True, text=True, errors='replace').stdout
hb, ha = hashes((before / 'hashes').read_text()), hashes((after / 'hashes').read_text())
hd = hashes(con)
table = parts(after)
check(len(hd) > 0 and len(ha) >= len(hd), 'hash sets: Linux before %d, Darwin %d, Linux after %d ranges' % (len(hb), len(hd), len(ha)))
static_ok = static_n = stable_n = stable_ok = moved = 0
for (lba, blocks), h in sorted(ha.items()):
    name, fs = where(lba, blocks, table)
    ours = ours_lba and name == next((n for n, st, _, _ in table if st == ours_lba), None)
    quiet = fs in ('apfs', 'none') or bool(ours)    # Linux mounts none of these (ours: nothing may write it in a read-only boot)
    if ours: name = name + '=SF'
    b, d = hb.get((lba, blocks)), hd.get((lba, blocks))
    if write_mode and ours:
        # written on purpose in this boot: before == Darwin's reading before its first write; "after" is reported only
        ours_n = globals().get('ours_n', 0) + 1; ours_pre = globals().get('ours_pre', 0) + (b == d and b is not None)
        print('NV_RANGE %10d %4d %-8s %-5s %-22s %s' % (lba, blocks, name, fs, 'unchanged' if b == h else 'written-in-this-boot', 'darwin-before-write==linux-before' if b == d else 'DARWIN-BEFORE-WRITE-DIFFERS'))
        continue
    state = 'unchanged' if b == h else ('no-before' if b is None else 'CHANGED-BY-LINUX-OR-US')
    rd = 'darwin==linux' if d == h else ('darwin-missing' if d is None else 'DARWIN-DIFFERS')
    print('NV_RANGE %10d %4d %-8s %-5s %-22s %s' % (lba, blocks, name, fs, state, rd))
    if quiet:
        static_n += 1; static_ok += (b == h and d == h)
    elif b == h:
        stable_n += 1; stable_ok += (d == h)
    else:
        moved += 1
check(static_n > 0 and static_ok == static_n, 'ranges Linux never writes (GPTs, APFS partitions%s): ' % (', the SiliconForest partition' if ours_lba else '') + '%d of %d unchanged before/after AND equal under Darwin' % (static_ok, static_n))
check(stable_ok == stable_n, 'ranges in Linux-mounted filesystems that Linux left alone: %d of %d equal under Darwin' % (stable_ok, stable_n))
if write_mode:
    if not rootw: check(globals().get('ours_n', 0) > 0 and globals().get('ours_pre') == globals().get('ours_n'), 'ranges inside the SiliconForest partition: %s of %s read by Darwin before its first write equal Linux before the boot' % (globals().get('ours_pre'), globals().get('ours_n')))
    haw = hashes(con, 'NVT_AFTER_WRITE_HASH')
    outside = [k for k in hd if not (k[0] >= ours_lba and k[0] + k[1] <= ours_lba + int(os.environ['NV_PART_BLOCKS']))]
    same = sum(1 for k in outside if haw.get(k) == hd[k])
    check(len(haw) == len(hd) and len(outside) > 0 and same == len(outside), 'inside Darwin, after its writes: %d of %d ranges outside our partition hash as before the writes' % (same, len(outside)))
print('NV_VERDICT note %d range(s) in Linux-mounted filesystems differ between Linux before and after (Linux writes there on every boot; not attributable)' % moved)

sb, sa = (before / 'smart-after.txt').read_text(), (after / 'nvme-id.txt').read_text()
for key in ('unsafe_shutdowns', 'media_errors', 'num_err_log_entries', 'critical_warning'):
    kb, ka = smart(sb.replace('0x', ''), key), smart(sa.replace('0x', ''), key)
    check(kb is not None and kb == ka, 'SMART %s: %s before, %s after' % (key, kb, ka))
print('NV_VERDICT note SMART deltas across the cycle: ' + ' '.join('%s=%+d' % (k, smart(sa, k) - smart(sb, k)) for k in
      ('power_cycles', 'data_units_read', 'data_units_written', 'host_read_commands', 'host_write_commands')))
ds = re.findall(r'^NV_SMART (start|end) (.*)$', con, re.M)
if len(ds) == 2:
    print('NV_VERDICT note SMART inside Darwin, start to end: ' + ' '.join('%s=%+d' % (k, smart(ds[1][1], k) - smart(ds[0][1], k)) for k in
          ('power_cycles', 'unsafe_shutdowns', 'data_units_read', 'data_units_written', 'host_read_commands', 'host_write_commands')))
    if not write_mode:
        check(smart(ds[1][1], 'host_write_commands') == smart(ds[0][1], 'host_write_commands') and
              smart(ds[1][1], 'data_units_written') == smart(ds[0][1], 'data_units_written'), 'write counters did not move during the Darwin session')
else:
    check(False, 'SMART start and end lines from Darwin')

klog = (after / 'dmesg.txt').read_text()
recov = [l for l in klog.splitlines() if 'raid6' not in l and (re.search(r'recover|replay|orphan|unclean|not cleanly|corrupt|I/O error', l, re.I) or
         (re.search(r'nvme|sart|BTRFS|EXT4|FAT-fs', l) and re.search(r'error|warn|fail|timeout|crash', l, re.I)))]
check(not recov, 'Linux kernel log after the boot: %d recovery/replay/error line(s)%s' % (len(recov), ''.join('\n    ' + l[:150] for l in recov[:5])))
mounts = (after / 'mounts.txt').read_text()
# SiliconForest's own boot partition (FAT, SF_BOOT_LBA from sf-part.env; there since the install) is not
# one of Linux's filesystems: Linux never mounts it, and it must not be mounted now
boot_lba = int(os.environ.get('SF_BOOT_LBA') or 0)
linux_fs = [('nvme0n1' + n, fs) for n, st, _, fs in table if fs in ('vfat', 'ext4', 'btrfs') and st != ours_lba and not (boot_lba and st == boot_lba)]
check(len(linux_fs) == 3, 'Linux has its three filesystems (EFI, /boot, root): %s' % linux_fs)
if boot_lba:
    check(not re.search(r'/dev/nvme0n1%s\b' % next((n for n, st, _, _ in table if st == boot_lba), 'pX'), mounts), 'the SiliconForest boot partition is not mounted in Linux')
if ours_lba:
    check(not re.search(r'/dev/nvme0n1%s\b' % next((n for n, st, _, _ in table if st == ours_lba), 'pX'), mounts), 'the SiliconForest partition is not mounted in Linux')
for dev, fs in linux_fs:
    check(re.search(r'/dev/%s\S* %s rw' % (dev, fs), mounts) is not None, 'Linux mounted %s (%s) read-write as usual' % (dev, fs))
check('state=live' in (after / 'nvme-sysfs.txt').read_text(), 'Linux NVMe controller state live')

gates = re.findall(r'^NV_GATE_REJECTED (\S+)', con, re.M); refused = re.findall(r'^NV_WRITE_REFUSED (\S+)', con, re.M)
check(bool(gates) and all(g == '0' for g in gates), 'gate_rejected: %s' % ' '.join(gates))
if expect_refused is None:
    check(bool(refused), 'write_refused (requests that reached the driver and were refused there): %s' % ' '.join(refused))
else:
    check(bool(refused) and refused[-1] == str(expect_refused), 'write_refused: %s (expected %d = the deliberate attempts)' % (' '.join(refused), expect_refused))
check(re.search(r'^NV_STATE stopped-clean', con, re.M) is not None, 'controller stopped cleanly inside Darwin')
check(re.search(r'^NV_GATE_SELFTEST allowed=7 refused=505 wrong=0', con, re.M) is not None, 'gate self-test 7/505/0')
check(re.search(r'^NV_NCPU 8', con, re.M) is not None, '8 CPUs')
check('panic(' not in con, 'no panic')
check(not re.search(r'SFAns: REFUSED|TCB invalidation failed|CRASHED|not in flight|no completion|bring-up failed|NOT CLEANLY', con), 'no driver anomaly line')
for tag in ('NV_IDCTRL', 'NV_IDNS'):
    l = re.search(r'^%s (.*)$' % tag, (after / 'nvme-id.txt').read_text(), re.M); d = re.search(r'^%s (.*)$' % tag, con, re.M)
    same = bool(l and d and l.group(1).strip() == d.group(1).strip())
    check(same, '%s fields equal Linux\'' % tag)
    if l and d and not same:
        lf, df = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', l.group(1))), dict(re.findall(r'(\w+)=("[^"]*"|\S+)', d.group(1)))
        print('    differing: ' + ' '.join('%s linux=%s darwin=%s' % (k, lf.get(k), df.get(k)) for k in lf if lf.get(k) != df.get(k)))
if level >= 3 and not root_mode:
    nparts = len(table)
    census = re.findall(r'^NV_MEDIA (.*)$', con, re.M)
    want = 'published=1 partitions=%d guarded=%d foreign=0' % (nparts, nparts)
    if write_mode: want += ' ours=1'    # all guarded, ours included: it is reached through the fenced device only
    if part_mode: want = 'published=1 partitions=%d guarded=%d foreign=0 ours=1' % (nparts, nparts - 1)
    check(len(census) >= 2 and all(c.strip() == want for c in census), 'below the block device: %s (expected "%s", before and after the reads)' % (' | '.join(census), want))
    if part_mode or write_mode:
        check(not re.search(r'ApfsFileSystemDriver|AppleAPFS|apfs_|hfs_mount|msdosfs_mount', con), 'no APFS, HFS or FAT driver line on the console')
    else:
        check(not re.search(r'ApfsFileSystemDriver|AppleAPFS|apfs_|Ext4FileSystemDriver|hfs_mount|msdosfs_mount|ext4_mount', con), 'no APFS / filesystem driver line on the console')
    mounts_d = re.findall(r'^NV_MOUNT (.*)$', con, re.M)
    check(bool(mounts_d) and not any('/dev/disk' in m for m in mounts_d), 'nothing from a disk is mounted in Darwin (%d mount lines, all root or devfs)' % len(mounts_d))
    hm = hashes(con, 'NVT_MEDIA_HASH')
    okm = sum(1 for k, h in hm.items() if hd.get(k) == h)
    if not write_mode: check(len(hm) == len(hd) and okm == len(hm), 'through the block device (/dev/rdiskN): %d of %d ranges equal to the same boot\'s /dev/sfnvme0 reads' % (okm, len(hm)))
    wt = re.findall(r'^NVT_WTEST_RESULT (\S+) (\S+)', con, re.M)
    if not wmode: check(len(wt) == (6 if part_mode or write_mode else 4) and all(r == 'writes-refused' for _, r in wt), 'write attempts: %s' % ' '.join('%s=%s' % x for x in wt))
    check('GRANTED' not in con, 'no open for writing was granted on any node')
    gpt = re.findall(r'^NV_GPT (\d+) (\d+) (\d+) GPT part', con, re.M)
    check(sorted((int(a), int(b)) for a, b, _ in gpt) == sorted((st, sz) for _, st, sz, _ in table), 'GPT as parsed by the stack: %d partitions with Linux\' starts and sizes' % len(gpt))
if part_mode:
    check(re.search(r'^NV_PART verified first=%d ' % ours_lba, con, re.M) is not None, 'the driver identified our partition in both GPT copies: %s' % ' | '.join(re.findall(r'^NV_PART (.*)$', con, re.M)))
    ours_idx = re.search(r'^NV_OURS index=(\d+)', con, re.M)
    for rnd in ('1', '2'):
        ml = re.findall(r'^NV_MOUNT%s (.*)$' % rnd, con, re.M)
        disk = [m for m in ml if '/dev/disk' in m]
        check(len(disk) == 1 and ours_idx is not None and re.match(r'/dev/disk\d+s%s on /private/tmp/sf \(ext4.*read-only' % ours_idx.group(1), disk[0]) is not None,
              'mount %s: only our partition, ext4, read-only: %s' % (rnd, ' | '.join(disk)))
        check(re.search(r'^NV_UMOUNT%s rc=0' % rnd, con, re.M) is not None, 'unmount %s succeeded' % rnd)
    rw = re.findall(r'^NV_MOUNT_RW_STATE (.*)$', con, re.M)
    check(all('read-only' in m for m in rw), 'a mount without rdonly did not become writable: %s %s' % (' '.join(re.findall(r'^NV_MOUNT_RW (.*)$', con, re.M)), ' | '.join(rw) or '(not mounted)'))
    fw = re.findall(r'^NV_FSWRITE (\S+) rc=(\d+) ?(.*)$', con, re.M)
    check(len(fw) == 5 and all(rc != '0' for _, rc, _ in fw), 'writes into the mounted filesystem refused: %s' % '; '.join('%s rc=%s %s' % x for x in fw))
    up = re.findall(r'^NV_MOUNT_AFTER_UPGRADE (.*)$', con, re.M)
    check(len(up) == 1 and 'read-only' in up[0], 'still read-only after the attempt to remount read-write')
    ref = Path(os.environ['NV_TREE_REF']).read_text()
    def tree(text, tag='NVT_TREE_ALL'):
        m = re.search(r'^%s (files=\d+ dirs=\d+ links=\d+ other=\d+ bytes=\d+ errors=\d+ [0-9a-f]{64})' % tag, text, re.M); return m.group(1) if m else None
    check(tree(con) is not None and tree(con) == tree(ref) and 'errors=0' in tree(con), 'the whole tree read from the mounted partition equals the source image: %s' % tree(con))
    check(tree(con, 'NVT_TREE2_ALL') == tree(ref), 'the same digest from the second mount')
    tops_d, tops_r = dict(re.findall(r'^NVT_TREE_TOP (\S+) ([0-9a-f]{64})', con, re.M)), dict(re.findall(r'^NVT_TREE_TOP (\S+) ([0-9a-f]{64})', ref, re.M))
    if tops_d != tops_r: print('    top-level entries that differ: ' + ' '.join(sorted(k for k in set(tops_d) | set(tops_r) if tops_d.get(k) != tops_r.get(k))))
    files_d, files_r = re.findall(r'^NVT_FILE (\S+ \d+ [0-9a-f]{64})', con, re.M), re.findall(r'^NVT_FILE (\S+ \d+ [0-9a-f]{64})', ref, re.M)
    check(len(files_d) == 3 and files_d == files_r, 'three named files hash as in the source image: %s' % ' '.join(f.split()[0] for f in files_d))
    am = re.search(r'^NVT_AFTER_MOUNT_HASH_ALL ranges=(\d+) errors=0 ([0-9a-f]{64})', con, re.M); mm = re.search(r'^NVT_MEDIA_HASH_ALL ranges=(\d+) errors=0 ([0-9a-f]{64})', con, re.M)
    check(bool(am and mm and am.group(2) == mm.group(2)), 'all ranges hash the same after the mounts as before them (inside Darwin)')
    check(not re.search(r'^NV_FAIL', con, re.M), 'no NV_FAIL line')
    for l in re.findall(r'^NV_DMESG .*$|^.*ext4: .*$', con, re.M)[:40]: print('NV_RESULT ' + l[:200])
if write_mode:
    first, nblk = ours_lba, int(os.environ['NV_PART_BLOCKS']); last = first + nblk - 1
    kv = lambda t, k: (re.search(r'\b%s=(\S+)' % re.escape(k), t) or [None, None])[1]
    # each reading is printed twice; one that a kernel log line cut into lacks its last field and is passed over
    ws = {}
    for tag, text in re.findall(r'^NV_WSTATS (\S+) (.*)$', con, re.M):
        if re.search(r'flush_max_us=\d+$', text) or tag not in ws: ws[tag] = text
    check(re.search(r'^NV_PART verified first=%d ' % ours_lba, con, re.M) is not None, 'the driver identified our partition in both GPT copies')
    if not rootw: check('WRITE-FENCED build' in con and 'the write fence is OPEN over the SiliconForest partition' in con, 'the write-fenced build ran and opened the fence after the verification')
    st = ws.get('start', '')
    check(kv(st, 'fence') == 'open' and kv(st, 'first') == str(first) and kv(st, 'last') == str(last) and (kv(st, 'device') or '').startswith('disk'), 'fence open over exactly [%d, %d], fenced device %s' % (first, last, kv(st, 'device')))
    if not rootw: check(kv(st, 'writes') == '0' and kv(st, 'flushes') == '0', 'no write and no flush before the write phase')
    check(re.search(r'^NV_WGATE_SELFTEST start fence=open admitted=4 refused=13 wrong=0', con, re.M) is not None, 'write-gate self-test on the live fence: 4/13/0')
    check(re.search(r'^NV_(WGATE_SELFTEST final|AFTER_STOP wgate_selftest) fence=closed admitted=0 refused=17 wrong=0', con, re.M) is not None, 'after the stop the fence is closed: 0/17/0')
    check(not re.search(r'^NV_FENCED_CHILD', con, re.M), 'the fenced device has no partitions of its own')
    if not wmode:
        ft = dict(re.findall(r'^NV_FENCETEST (\S+) (.*)$', con, re.M))
        check(len(ft) == 2 and all('NVT_FAIL' in v or 'rror' in v or 'not permitted' in v for v in ft.values()), 'reads of an owner partition and of ours through the whole disk refused: %s' % ft)
        fb, fa = re.search(r'^NV_FENCE before (.*)$', con, re.M), re.search(r'^NV_FENCE after (.*)$', con, re.M)
        check(bool(fb and fa) and kv(fb.group(1), 'other') == '0' and kv(fa.group(1), 'other') == '0' and kv(fb.group(1), 'readfence') == '1' and int(kv(fa.group(1), 'refused')) > int(kv(fb.group(1), 'refused')),
              'read fence on (GPT only): served outside 0; refused %s -> %s by the deliberate reads' % (fb and kv(fb.group(1), 'refused'), fa and kv(fa.group(1), 'refused')))
        pr = re.findall(r'^NV_WPROBE (\S+) lba=(\d+) blocks=(\d+) rc=(\d+)', con, re.M)
        wrong = [p for p in pr if (p[0].startswith('in-')) != (p[3] == '0')]
        owners = [p for p in pr if p[0].startswith('out-p')]
        check(len(pr) >= 8 + 2 * (nparts - 1) and not wrong and len(owners) == 2 * (nparts - 1), 'range check: first and last block of the fence admitted; one before, one after, across either end, block 0, the GPT header and the first and last block of each of the %d owner partitions refused (%d probes, nothing written)%s' % (nparts - 1, len(pr), ' WRONG: %s' % wrong if wrong else ''))
        for nm, a, b, _ in table:
            if a != first: check(any(int(p[1]) == a for p in owners) and any(int(p[1]) == a + b - 1 for p in owners), 'probed owner partition %s at its first and last block (%d, %d)' % (nm, a, a + b - 1))
        check(re.search(r'^NV_PASTEND rc=[1-9]', con, re.M) is not None, 'a write at the block after the fenced device\'s last was refused: %s' % ' '.join(re.findall(r'^NV_PASTEND (.*)$', con, re.M)))
        rf = ws.get('refuse', '')
        check(kv(rf, 'writes') == '0' and kv(rf, 'flushes') == '0' and kv(rf, 'io_ops[00,01]') == '0,0', 'after all deliberate attempts: no write or flush command was built (writes=0 flushes=0 io_ops[00,01]=0,0; range_refused=%s)' % kv(rf, 'range_refused'))
    mrw = re.findall(r'^NV_MOUNT_RW (.*)$', con, re.M)
    if rootw:
        check(re.search(r'^NV_ROOT disk read-write: root_device on / \(ext4(?!.*read-only).*\) on %s$' % re.escape(kv(st, 'device') or '?'), con, re.M) is not None, 'the root filesystem is the fenced device, ext4, read-write: %s' % ' | '.join(re.findall(r'^NV_ROOT (.*)$', con, re.M)))
        ba = ' '.join(re.findall(r'^NV_BOOTARGS (.*)$', con, re.M))
        check('rd=uuid boot-uuid=GUID' in ba and 'rd=md0' not in ba and 'sfnvme_write=1' in ba and 'launchdsuffix' not in ba, 'booted with rd=uuid boot-uuid=<filesystem UUID> sfnvme_write=1, no rd=md0, process 1 is launchd itself')
        sw = re.findall(r'^NV_STATEWRITE (\S+) rc=(\d+)', con, re.M)
        check(len(sw) == 5 and all(rc == '0' for _, rc in sw), 'writes to /var/run, /tmp, /private/var/tmp, /etc, /usr succeed on the disk root: %s' % ' '.join('%s=%s' % x for x in sw))
        check(('SF_BENCH_REBOOT' if halt else 'SF_BENCH_DONE') in con.split('NV_AFTER_STOP')[-1], 'the report after the stop came out complete (from memory)')
        for l in re.findall(r'^(?:GPUDESK|WMSTAT|NV_DESKSET|NV_DESKSTATE|WMSCALE|WMSET|WMAUTO (?:armed|settings|check)).*$', con, re.M)[:30]: print('NV_RESULT ' + l[:200])
    else: check(len(mrw) == 1 and re.match(r'/dev/%s on /private/tmp/sf \(ext4' % kv(st, 'device'), mrw[0]) is not None and 'read-only' not in mrw[0], 'ext4 mounted read-write from the fenced device: %s' % ' | '.join(mrw))
    check(re.search(r'^NV_STOP_WHILE_MOUNTED rc=[1-9]\d* state=ready', con, re.M) is not None and (rootw or 'stop refused: the fenced device is open' in con), 'a stop request while the filesystem was mounted read-write was refused')
    wsha = lambda tag, text=con: dict((f, h) for h, f in re.findall(r'^NV_WSHA %s ([0-9a-f]{64})\s+(\S+)' % tag, text, re.M))
    w1, w2 = wsha('written'), wsha('reread')
    um = ws.get('sealed' if wmode else 'unmounted', ''); fin = ws.get('final', '')
    psha = lambda kind, tag, text=con: dict((f, h) for h, f in re.findall(r'^NV_%s %s ([0-9a-f]{64})\s+(\S+)' % (kind, tag), text, re.M))
    if rootw:
        w1 = psha('PSHA', 'written')
        fin = (re.findall(r'^NV_AFTER_STOP wstats (.*)$', con, re.M) or [''])[-1]; ws['final'] = fin
    if not wmode:
        check(re.search(r'^NV_UMOUNT1 rc=0', con, re.M) is not None and re.search(r'^NV_UMOUNT2 rc=0', con, re.M) is not None, 'both unmounts succeeded')
        check(len(w1) == 4 and w1 == w2, 'four files written; after unmount and a new mount they read back with the same SHA-256')
        check(int(kv(um, 'writes') or 0) > 0 and int(kv(um, 'written_bytes') or 0) >= 34000000, 'writes went through the driver: %s commands, %s bytes by the first unmount, %s flushes' % (kv(um, 'writes'), kv(um, 'written_bytes'), kv(um, 'flushes')))
    else:
        check(len(w1) >= (1 if rootw else 3) and int(kv(um, 'writes') or 0) > 0, '%d files written and synced (%s write commands, %s flushes by the seal)' % (len(w1), kv(um, 'writes'), kv(um, 'flushes')))
        check(re.search(r'^NV_SEAL rc=0 seal=1', con, re.M) is not None and (rootw or 'sealed: flush 0x0' in con), 'sealed: Flush, then the fence closed, with the filesystem still mounted')
        me = re.findall(r'^NV_MOUNT_END (.*)$', con, re.M)
        check(len(me) == 1, 'the filesystem was still mounted when the controller stopped: %s' % ' | '.join(me))
        prev = os.environ.get('NV_PREV_CONSOLE')
        found = wsha('found')
        print('NV_VERDICT note files found at the mount: %d' % len(found))
        if prev and rootw:
            pcon = subprocess.run(['strings', '-n', '6', prev], capture_output=True, text=True, errors='replace').stdout
            pp, pd_ = psha('PSHA', 'written', pcon), psha('DSHA', 'written', pcon)
            fp, fd = psha('PSHA', 'found'), psha('DSHA', 'found')
            check(len(pp) >= 1 and all(fp.get(f) == h for f, h in pp.items()), 'persistence: the %d file(s) the previous boot wrote under /private/var/sf-persist are there, hash-equal: %s' % (len(pp), ' '.join(sorted(pp))))
            check(all(fd.get(f) == h for f, h in pd_.items()), 'persistence: the %d file(s) of the desktop\'s own state directory as the previous boot left them are there, hash-equal: %s' % (len(pd_), ' '.join(sorted(pd_)) or '(the desktop wrote none)'))
        elif prev:
            pw = wsha('written', subprocess.run(['strings', '-n', '6', prev], capture_output=True, text=True, errors='replace').stdout)
            check(len(pw) >= 3 and all(found.get(f) == h for f, h in pw.items()), 'the %d synced files of the previous boot (ended unclean) are there after Darwin\'s own mount, hash-equal' % len(pw))
            rp = sorted(set(re.findall(r'^NV_DMESG .*ext4: (journal dirty.*|.*replay.*)$', con, re.M)))
            check(any(r.startswith('journal dirty') for r in rp), 'Darwin found the journal dirty at its mount and recovered it: %s' % (' | '.join(r[:100] for r in rp) or 'no such line'))
    if wmode in ('downgrade', 'root'):
        check(re.search(r'^NV_DOWNGRADE rc=0', con, re.M) is not None and 'ext4: downgraded to read-only: journal empty' in con, 'mount -o update,rdonly succeeded and the driver reported the journal empty and RECOVER cleared')
        md = re.findall(r'^NV_MOUNT_DOWN (.*)$', con, re.M); fw = re.findall(r'^NV_FSWRITE (\S+) rc=(\d+)', con, re.M)
        check(len(md) == 1 and 'read-only' in md[0] and len(fw) == 2 and all(rc != '0' for _, rc in fw), 'mounted read-only afterwards; create and append refused')
        if not rootw: check(all(w2.get(f) == w1.get(f) for f in w2) and len(w2) == 2, 'files read the same after the downgrade')
        for tag in ('sealed', 'after', 'final'):
            check(kv(ws.get(tag, ''), 'sealed_refused') == '0', 'wstats %s: sealed_refused=0: nothing tried to write after the downgrade and the seal' % tag)
        check(kv(ws.get('sealed', ''), 'writes') == kv(ws.get('before-seal', ''), 'writes') and kv(fin, 'writes') == kv(ws.get('before-seal', ''), 'writes'), 'no write command after the downgrade (%s before the seal, %s at the end)' % (kv(ws.get('before-seal', ''), 'writes'), kv(fin, 'writes')))
    if wmode == 'unclean':
        print('NV_VERDICT note writes refused after the seal (the filesystem was still read-write): %s' % kv(fin, 'sealed_refused'))
    for tag, t in sorted(ws.items()):
        check(kv(t, 'range_refused') == '0' and kv(t, 'copy_failed') == '0' and (wmode or kv(t, 'sealed_refused') == '0'), 'wstats %s: range_refused=0 copy_failed=0%s' % (tag, '' if wmode else ' sealed_refused=0'))
        # a refusal at the doorbell after the seal (a Flush or Write that got past the sealed check) would count here too
        check(kv(t, 'fence_rejected') == '0', 'wstats %s: fence_rejected=0' % tag)
    check(kv(fin, 'fence') == 'sealed' and (bool(wmode) or 'flush before the stop: 0' in con), 'the stop flushed first and closed the fence: %s' % ' | '.join(l[:120] for l in re.findall(r'^NV_DMESG .*(?:write side|flush before).*$', con, re.M)))
    print('NV_VERDICT note final write counters: %s' % fin)
    print('NV_VERDICT note %s' % ' '.join(re.findall(r'^NV_WRITE_SECONDS .*$', con, re.M)))
    # from Linux afterwards
    wc = Path(os.environ['NV_WCHECK']).read_text(errors='replace') if os.path.exists(os.environ.get('NV_WCHECK', '')) else ''
    check('SFL_IDENTITY ok' in wc and 'SFL_END' in wc, 'Linux identified our partition three ways and finished its check')
    lsha = dict((f, h) for h, f in re.findall(r'^SFL_WSHA ([0-9a-f]{64})\s+(\S+)', wc, re.M))
    recov = re.search(r'^SFL_SB_FIRST Filesystem features:.*needs_recovery', wc, re.M) is not None
    if wmode != 'unclean':
        check(re.search(r'^SFL_FSCK_RC 0$', wc, re.M) is not None, 'Linux e2fsck -fn of our partition: clean (%s)' % ' | '.join(re.findall(r'^SFL_FSCK (.*)$', wc, re.M)[-1:]))
        check(re.search(r'^SFL_SB_FIRST Filesystem state:\s+clean$', wc, re.M) is not None and not recov, 'superblock as Darwin left it: clean, no needs_recovery')
        js = re.search(r'^SFL_SB_FIRST Journal start:\s+(\d+)', wc, re.M)
        check(js is not None and js.group(1) == '0', 'journal superblock as Darwin left it: empty (start 0)')
    else:
        check(recov, 'superblock as Darwin left it: needs_recovery set (the filesystem was mounted read-write when the controller stopped)')
        print('NV_VERDICT note Linux e2fsck -fn before any replay: rc %s: %s' % (' '.join(re.findall(r'^SFL_FSCK_RC (\d+)', wc, re.M)), ' | '.join(re.findall(r'^SFL_FSCK (.*)$', wc, re.M)[-2:])))
        if 'SFL_REPLAY_RC' in wc:
            check(re.search(r'^SFL_REPLAY_RC [01]$', wc, re.M) is not None and re.search(r'^SFL_FSCK2_RC 0$', wc, re.M) is not None, 'Linux replayed the journal (e2fsck -fy rc %s) and the filesystem is clean afterwards (e2fsck -fn rc 0): %s' % (' '.join(re.findall(r'^SFL_REPLAY_RC (\d+)', wc, re.M)), ' | '.join(l[:90] for l in re.findall(r'^SFL_REPLAY (.*)$', wc, re.M)[:3])))
        else:
            print('NV_VERDICT note the journal is left for Darwin\'s next mount (no replay from Linux)')
    if rootw:
        lp = dict((f, h) for h, f in re.findall(r'^SFL_PSHA ([0-9a-f]{64})\s+(\S+)', wc, re.M)); ld = dict((f, h) for h, f in re.findall(r'^SFL_DSHA ([0-9a-f]{64})\s+(\S+)', wc, re.M))
        check(len(w1) >= 1 and lp == w1, 'Linux reads the %d file(s) under /private/var/sf-persist with the SHA-256 Darwin reported at the end of the boot' % len(w1))
        dd = psha('DSHA', 'written')
        check(ld == dd, 'Linux reads the desktop\'s state directory as Darwin reported it (%d file(s): %s)' % (len(dd), ' '.join(sorted(dd)) or 'none'))
        lib = re.search(r'^NV_LIBSYSTEM in use ([0-9a-f]{64}); memory-disk copy ([0-9a-f]{64})', con, re.M); ol = re.search(r'^SFL_OLDSHA ([0-9a-f]{64})\s+usr/lib/libSystem.B.dylib', wc, re.M)
        check(bool(lib and ol) and ol.group(1) == lib.group(2), 'the libSystem in use was not written back: on disk it has the digest of the untouched copy (%s)' % ('in use it reads differently, as known' if lib and lib.group(1) != lib.group(2) else 'and reads the same in use'))
    elif wmode:
        check(len(w1) >= 3 and all(lsha.get(f) == h for f, h in w1.items()), 'Linux reads the %d files Darwin wrote and synced with the SHA-256 Darwin reported' % len(w1))
        print('NV_VERDICT note files under /private/sf-wtest as Linux sees them: %s' % ' '.join(sorted(lsha)))
    else:
        check(len(lsha) == 4 and lsha == w1, 'Linux reads the four written files with the SHA-256 Darwin reported: %s' % ' '.join(sorted(lsha)))
    old_d = dict((f.replace('/tmp/sf/', ''), h) for h, f in re.findall(r'^NV_OLDSHA ([0-9a-f]{64})\s+(\S+)', con, re.M)); old_l = dict((f, h) for h, f in re.findall(r'^SFL_OLDSHA ([0-9a-f]{64})\s+(\S+)', wc, re.M))
    if not wmode: check(len(old_l) == 2 and old_l == old_d, 'two files that were there before read the same in Darwin and Linux')
    check(not re.search(r'^NV_FAIL', con, re.M), 'no NV_FAIL line')
    for l in re.findall(r'^NV_DMESG .*$', con, re.M)[:30]: print('NV_RESULT ' + l[:200])
if root_mode:
    nparts = len(table)
    def tree(text, tag='NVT_TREE_ALL'):
        m = re.search(r'^%s (files=\d+ dirs=\d+ links=\d+ other=\d+ bytes=\d+ errors=\d+ [0-9a-f]{64})' % tag, text, re.M); return m.group(1) if m else None
    ref = Path(os.environ['NV_TREE_REF']).read_text()
    check(re.search(r'^NV_ROOT disk read-only: root_device on / \(ext4.*read-only.*\) on disk\d+s\d+', con, re.M) is not None, 'the root filesystem is our partition, ext4, read-only: %s' % ' | '.join(re.findall(r'^NV_ROOT (.*)$', con, re.M)))
    check(re.search(r'^NV_BOOTARGS .*rd=uuid boot-uuid=GUID', con, re.M) is not None and not re.search(r'^NV_BOOTARGS .*rd=md0', con, re.M), 'booted with rd=uuid boot-uuid=<partition GUID>, no rd=md0')
    check(re.search(r'^NV_MOUNT /dev/md0 on /private/ram \(ext4(?!.*read-only)', con, re.M) is not None, 'the memory disk is mounted writable on /private/ram')
    check('sf-roinit: root read-only' in con, 'process 1 was the wrapper (sf-roinit line on the console)')
    check(re.search(r'^NV_PART verified first=%d ' % ours_lba, con, re.M) is not None, 'the driver identified our partition in both GPT copies')
    census = re.findall(r'^NV_(?:MEDIA|BEFORE_STOP media|AFTER_STOP media) (.*)$', con, re.M)
    want = 'published=1 partitions=%d guarded=%d foreign=0 ours=1' % (nparts, nparts - 1)
    check(len(census) >= 3 and all(c.strip() == want for c in census), 'below the block device, %d readings: %s' % (len(census), ' | '.join(sorted(set(census)))))
    fences = re.findall(r'^NV_(?:FENCE|BEFORE_STOP fence|AFTER_STOP fence) (.*)$', con, re.M)
    f = lambda t, k: int(re.search(r'\b%s=(\d+)' % k, t).group(1))
    check(len(fences) >= 4 and all(f(t, 'readfence') == 1 and f(t, 'other') == 0 for t in fences), 'read fence on; requests served outside the GPT and our partition: 0 in every reading')
    check(len(fences) >= 4 and f(fences[0], 'refused') == 0, 'before the deliberate test NOTHING had asked for a block of an owner partition through the block device (refused=0): %s' % (fences[0] if fences else ''))
    check(len(fences) >= 4 and 1 <= f(fences[-1], 'refused') <= 8 and 'NVT_FAIL' in ''.join(re.findall(r'^NV_FENCETEST (.*)$', con, re.M)), 'the deliberate read of an owner partition through /dev/rdiskN was refused: %s' % (fences[-1] if fences else ''))
    print('NV_VERDICT note reads of our partition through the block device: %s' % (fences[-1] if fences else ''))
    sw = re.findall(r'^NV_STATEWRITE (\S+) rc=(\d+)', con, re.M)
    check(len(sw) == 4 and all(rc == '0' for _, rc in sw), 'writes to /var, /tmp, /private/var/tmp, /etc/resolv.conf succeed (memory): %s' % ' '.join('%s=%s' % x for x in sw))
    fw = re.findall(r'^NV_FSWRITE (\S+) rc=(\d+) ?(.*)$', con, re.M)
    check(len(fw) == 5 and all(rc != '0' for _, rc, _ in fw), 'writes to the root filesystem refused: %s' % '; '.join('%s rc=%s %s' % x for x in fw))
    up = re.findall(r'^NV_MOUNT_AFTER_UPGRADE (.*)$', con, re.M)
    check(len(up) == 1 and 'read-only' in up[0], 'root still read-only after the attempt to remount read-write')
    check(tree(con) is not None and tree(con) == tree(ref) and 'errors=0' in tree(con), 'the whole root tree as the running system sees it equals the transformed source image: %s' % tree(con))
    files_d, files_r = re.findall(r'^NVT_FILE (\S+ \d+ [0-9a-f]{64})', con, re.M), re.findall(r'^NVT_FILE (\S+ \d+ [0-9a-f]{64})', ref, re.M)
    # the libSystem in use reads back changed in the cache (the same other digest in QEMU and on the M1, read-only
    # or writable root); everything else must be exact
    IN_USE = 'usr/lib/libSystem.B.dylib 4338830 83e40e0bcd4452917ffcc01815a16b41753fe4e478cf7e5ba092ef4c00fba765'
    check(len(files_d) == 3 and files_d[:2] == files_r[:2] and files_d[2] in (files_r[2], IN_USE), 'named files hash as in the source image (libSystem: as on disk, or the known in-use digest): %s' % ('in-use digest' if files_d[2:] == [IN_USE] else 'as on disk'))
    for l in re.findall(r'^NV_LIBSYSTEM .*$', con, re.M): print('NV_RESULT ' + l[:220])
    check(not re.search(r'^NV_FAIL', con, re.M), 'no NV_FAIL line')
    check('SF_BENCH_DONE' in con.split('NV_AFTER_STOP')[-1], 'the report after the stop came out complete (from memory)')
    print('NV_VERDICT note writes the ext4 driver tried and IOMedia refused ("privilege violation" lines): %d' % con.count('privilege violation'))
    for l in re.findall(r'^NV_DMESG .*$', con, re.M)[:30]: print('NV_RESULT ' + l[:200])
if halt:
    # The boot was ended by the system's own command; the machine restarted itself. Clock: the time the guest
    # printed against the time its line arrived at the controller; a file made in the boot against both.
    wc = Path(os.environ['NV_WCHECK']).read_text(errors='replace') if os.path.exists(os.environ.get('NV_WCHECK', '')) else ''
    rawc = console.read_bytes()
    try: arrivals = [(float(x), int(y)) for x, y in (l.split() for l in (console.parent / 'console-times.txt').read_text().splitlines())]
    except OSError: arrivals = []
    def arrived(pattern):
        m = re.search(pattern, rawc)
        return (next((t for t, count in arrivals if count > m.start()), None), m) if m else (None, None)
    for tag in ('start', 'end'):
        t, m = arrived(rb'NV_CLOCK %s epoch=(\d+)' % tag.encode())
        d = None if t is None else int(m.group(1)) - t
        check(d is not None and -6 <= d <= 3, 'clock at the %s of the session: Darwin says %s, its line reached the controller at %s (difference %s s; a UART line arrives up to some seconds late)' % (tag, m.group(1).decode() if m else '?', '%.1f' % t if t else '?', '%.1f' % d if d is not None else '?'))
    check(re.search(r'^SF_CLOCK set utc=\d+ .*source=hardware-clock', con, re.M) is not None, 'the time came from the hardware clock: %s' % ' | '.join(re.findall(r'^SF_CLOCK .*$', con, re.M))[:200])
    check(re.search(r'^NV_CLOCK kernel sfrtc=1 result=0 .*writes=none', con, re.M) is not None, 'the kernel read the SMC counter and the stored offset, wrote nothing: %s' % ' | '.join(re.findall(r'^NV_CLOCK kernel (.*)$', con, re.M))[:200])
    ft = re.search(r'^NV_FTIME (\S+) mtime=(\d+) now=(\d+)', con, re.M)
    lt = dict((f, int(t)) for t, f in re.findall(r'^SFL_PTIME (\d+) (\S+)', wc, re.M))
    now = re.search(r'^SFL_NOW (\d+)', wc, re.M)
    check(bool(ft) and abs(int(ft.group(2)) - int(ft.group(3))) <= 2 and lt.get(ft.group(1)) == int(ft.group(2)) and bool(now) and 0 <= int(now.group(1)) - int(ft.group(2)) < 3600,
          'a file made in this boot carries the time of its making: Darwin mtime %s (its clock then %s), Linux reads mtime %s, Linux\'s clock at the check %s' % (ft.group(2) if ft else '?', ft.group(3) if ft else '?', lt.get(ft.group(1)) if ft else '?', now.group(1) if now else '?'))
    old = [f for f, t in lt.items() if t < 1700000000]
    print('NV_VERDICT note files under sf-persist dated before 2023 (written by boots without a clock): %s' % (' '.join(sorted(old)) or 'none'))
    # the look the previous boot's menu choices left must be the look this session starts with (not only the files)
    prev = os.environ.get('NV_PREV_CONSOLE')
    if prev:
        pcon = subprocess.run(['strings', '-n', '6', prev], capture_output=True, text=True, errors='replace').stdout
        pt = re.findall(r'^WMSET theme=(\S+) .*persisted=1', pcon, re.M); ps = re.findall(r'^WMSET scale=\S+ scale120=(\d+) persisted=1', pcon, re.M)
        init = re.search(r'^WMSCALE initial scale=(\S+) scale120=(\d+) source=(\S+) .* theme=(\S+) theme_source=(\S+)', con, re.M)
        if pt or ps:
            check(bool(init) and (not ps or (init.group(2) == ps[-1] and init.group(3) == 'persisted')) and (not pt or (init.group(4) == pt[-1] and init.group(5) == 'persisted')),
                  'the session came up with what the previous boot chose in the desktop menu (theme %s, scale120 %s): %s' % (pt[-1] if pt else '-', ps[-1] if ps else '-', init.group(0)[:230] if init else 'no WMSCALE initial line'))
            chk = re.search(r'^WMAUTO check phase=settle .*result=(\S+) frame_hash=(\S+) cpu_hash=(\S+) titlebar=(\S+)', con, re.M)
            check(bool(chk) and chk.group(1) == 'equal', 'the frame on the panel in that look, read back from the renderer, equals the CPU render of the scene: %s' % (chk.group(0)[:200] if chk else 'no settle check'))
        else:
            print('NV_VERDICT note the previous boot chose no theme or scale in the menu')
    # the fence's ends, asked of the range check with the root mounted (nothing is written by a probe)
    pr = re.findall(r'^NV_WPROBE (\S+) lba=(\d+) blocks=(\d+) rc=(\d+)', con, re.M)
    first, last = ours_lba, ours_lba + int(os.environ['NV_PART_BLOCKS']) - 1
    names = dict((p[0], (int(p[1]), int(p[2]))) for p in pr)
    wrong = [p for p in pr if (p[0].startswith('in-')) != (p[3] == '0')]
    # since the second shrink (13 November 2026) ten probes: the partition's last block before each of the two shrinks
    check(len(pr) == 10 and not wrong and names.get('in-first') == (first, 1) and names.get('in-last') == (last, 1) and names.get('out-after') == (last + 1, 1) and names.get('out-across-end') == (last, 2) and names.get('out-before') == (first - 1, 1)
          and names.get('out-old-last') == (last + 131072, 1) and names.get('out-older-last') == (last + 741376, 1) and names.get('out-freed-last') == (last + 741417, 1),
          'range check at the fence [%d, %d]: first, last and the last 256 blocks admitted; one before, one after, across the end, the partition\'s last block before the second shrink (%s) and before the first (%s), the last unallocated block (%s) and block 0 refused (%d probes, nothing written)%s'
          % (first, last, names.get('out-old-last', ('?',))[0], names.get('out-older-last', ('?',))[0], names.get('out-freed-last', ('?',))[0], len(pr), ' WRONG: %s' % wrong if wrong else ''))
    # the end by the system's own command
    if os.environ.get('NV_HALT_MENU') == '1':
        check(re.search(r'^WMSET session_end=restart', con, re.M) is not None and re.search(r'^NV_SESSION_END request=restart$', con, re.M) is not None,
              'the restart was asked for in the desktop menu (Session > Restart) and the session ended with that request: %s' % ' | '.join(re.findall(r'^(?:WMSET session_end|NV_SESSION_END).*$', con, re.M))[:200])
    check(re.search(r'^NV_HALT the command of the system now: (reboot|shutdown -r now)$', con, re.M) is not None, 'the session was ended by the system\'s command: %s' % ' | '.join(re.findall(r'^NV_HALT (.*)$', con, re.M)))
    check(re.search(r'^SF_HALT downgrade: mount rc=0, root is now read-only', con, re.M) is not None, 'sf-halt: the root downgraded to read-only')
    check(re.search(r'^SF_HALT stop errno=0 state=stopped-clean', con, re.M) is not None, 'sf-halt: the controller stopped cleanly before the restart: %s' % ' | '.join(re.findall(r'^SF_HALT stop .*$', con, re.M))[:160])
    check(re.search(r'^SF_HALT restarting now', con, re.M) is not None and 'SF_HALT reboot returned' not in con and 'SF_HALT FAILED' not in con, 'sf-halt asked the kernel to restart and did not come back')
    hg = re.findall(r'^NV_HALTGATE (start|end) (.*)$', con, re.M)
    check(len(hg) == 2 and all('sent=0 refused=0' in t.split('|')[0] and 'sfpmu=1' in t and 'sent=0 refused=0' in t.split('|')[1] for _, t in hg), 'before the restart no SMC or PMU write had been sent by the restart path, and the PMU is read-only in this boot (sfpmu=1): %s' % (hg[-1][1] if hg else ''))
    check(os.environ.get('NV_RESETS') == '0', 'the controller sent no reset: the machine came back to Linux by itself (reset_attempts=%s)' % os.environ.get('NV_RESETS'))
    # In a multi-user boot the console's login shell shares the console: its prompt's escape sequences can
    # stand in front of the line on the same console line (dr27: `ESC[?2004hSF_BOOT ...`, and the check, then
    # anchored at the line's start, failed on a line that was there). After a CSI sequence counts as a start.
    sb = re.findall(r'(?:^|\[[0-9;?]*[A-Za-z])(SF_BOOT clock_rc=.*)$', con, re.M)
    check(len(sb) == 1 and 'run_and_tmp=cleaned' in sb[0] and re.search(r'^NV_SFBOOT rc=0 stamp=this-boot', con, re.M) is not None, 'sf-boot ran once: %s' % ' | '.join(sb)[:200])
    for l in re.findall(r'^(?:SF_HALT|SF_INSTALL|NV_SFINSTALL|NV_LEFT|NV_DMESG_CLOCK|SFL_DESKSTATE|SFL_LEFT|SFL_SBTIME).*$', con + wc, re.M)[:40]: print('NV_RESULT ' + l[:220])
raws = dict(re.findall(r'^NV_SMARTRAW (\S+) ([0-9a-f]{1024})', con, re.M))
order = [k for k in ('start', 'end', 'start2', 'end2') if k in raws]
for a, b in zip(order, order[1:]):
    x, y = bytes.fromhex(raws[a]), bytes.fromhex(raws[b])
    diff = [i for i in range(512) if x[i] != y[i]]
    fields = sorted({(i // 16) * 16 if i >= 32 else i for i in diff})
    print('NV_VERDICT note SMART page %s -> %s: bytes differing at offsets %s (1-2 temperature, 32 data units read, 64 host reads, 96 busy time, 112 power cycles, 144 unsafe shutdowns, 48/80 writes; >=192 vendor/other)' % (a, b, fields))
    check(not any(f in ((144, 160, 176) if write_mode else (48, 80, 144, 160, 176)) or f == 0 for f in fields), 'SMART %s -> %s: no change in %sunsafe shutdowns, media errors, error log, critical warning' % (a, b, '' if write_mode else 'write counters, '))
starts = con.split('SFAppleNVMe: start ')
for chunk in starts[1:]:
    n = chunk.split(' ', 1)[0]
    msgs = re.findall(r'SFAns: syslog: (.*)', chunk)
    print('NV_VERDICT note firmware log after start %s: %s' % (n, ' || '.join(m.strip()[:90] for m in msgs) or '(none)'))
if 'start2' in raws:
    check(len(re.findall(r'^NV_STATE stopped-clean', con, re.M)) >= 2 and 'end2' in raws, 'second start in the same boot: ready, read, stopped cleanly again')
    hs = re.search(r'^NVT_SECOND_HASH_ALL ranges=(\d+) errors=0 ([0-9a-f]{64})', con, re.M); h1 = re.search(r'^NVT_HASH_ALL ranges=(\d+) errors=0 ([0-9a-f]{64})', con, re.M)
    check(bool(hs and h1 and hs.group(2) == h1.group(2)), 'second start: all ranges hash to the same digest as on the first start')
for l in re.findall(r'^NVT_(?:MEDIA_)?(?:SEQ|RAND) .*$|^NVT_WTEST.*$|^NVT_FAIL.*$|^NV_FAIL.*$', con, re.M): print('NV_RESULT ' + l)
if bad:
    stop = Path(__file__).resolve().parents[4] / 'runs/m1-storage/STOP'
    stop.write_text('nv_verdict failed for %s: %s\n' % (console, '; '.join(bad)))
    print('NV_VERDICT FAILED (%d); %s written: no further storage boot' % (len(bad), stop)); sys.exit(1)
print('NV_VERDICT PASSED')
