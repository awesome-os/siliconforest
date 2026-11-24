#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The disk's two partition-table copies, read-only: are they valid and identical, is the
SiliconForest partition's entry exactly what it was, and are the new entries (the startup
volume and the boot partition) where they may be. Standard library only. Nothing is written
to the disk, ever: the device is opened O_RDONLY.

  sudo python3 sf-gpt-check.py read /dev/nvme0n1 <prefix>
      saves the first and the last six blocks as <prefix>.head.bin and <prefix>.tail.bin
      (they hold partition identifiers: keep them out of git)
  python3 sf-gpt-check.py check <prefix> --partuuid <ours> --first <block> --blocks <n>
          [--free-first <block> --free-last <block>] [--before <prefix of an earlier read>] [--new 0|2]
      --before: every entry of the earlier table must still be there, byte for byte (its
                position in the table may differ: macOS sorts entries by disk order)
      --new 2 : exactly two entries are new: one APFS of 610,304 blocks and one EFI-typed of
                126,976..131,113 blocks, both wholly inside --free-first..--free-last
      --new 0 : no entry is new
  Exit 0: every line says ok.  Lines: GPT ok|FAIL <what>.
"""
import argparse, fcntl, os, struct, sys, uuid, zlib

BLOCKS = 6
LINUX = uuid.UUID('0FC63DAF-8483-4772-8E79-3D69D8477DE4')
APFS = uuid.UUID('7C3457EF-0000-11AA-AA11-00306543ECAC')
EFI = uuid.UUID('C12A7328-F81F-11D2-BA4B-00A0C93EC93B')
STUB_BLOCKS = 610304


def read(device, prefix):
    fd = os.open(device, os.O_RDONLY)
    try:
        size = os.lseek(fd, 0, os.SEEK_END)
        bs = struct.unpack('i', fcntl.ioctl(fd, 0x1268, b'\0' * 4))[0] if os.path.exists('/sys/block') and device.startswith('/dev/') else 4096
        head, tail = os.pread(fd, BLOCKS * bs, 0), os.pread(fd, BLOCKS * bs, size - BLOCKS * bs)
    finally:
        os.close(fd)
    for name, data in (('.head.bin', head), ('.tail.bin', tail)):
        with open(prefix + name, 'wb') as out:
            out.write(data)
    print('GPT read %s: %d blocks of %d bytes; saved %s.head.bin and .tail.bin' % (device, size // bs, bs, prefix))


class Table:
    def __init__(self, prefix):
        head, tail = open(prefix + '.head.bin', 'rb').read(), open(prefix + '.tail.bin', 'rb').read()
        self.bs = len(head) // BLOCKS
        self.problems = []
        self.primary = self.header(head[self.bs:2 * self.bs], 'primary')
        self.backup = self.header(tail[-self.bs:], 'backup')
        p, b = self.primary, self.backup
        last = p['alternate']
        tail_first = last - BLOCKS + 1
        self.ok(p['my'] == 1 and b['my'] == last and b['alternate'] == 1, 'the two headers point at each other')
        self.ok((p['first_usable'], p['last_usable'], p['disk'], p['count'], p['entry_size']) ==
                (b['first_usable'], b['last_usable'], b['disk'], b['count'], b['entry_size']), 'the two headers agree')
        n = p['count'] * p['entry_size']
        a1 = head[p['entries'] * self.bs:p['entries'] * self.bs + n]
        off = (b['entries'] - tail_first) * self.bs
        a2 = tail[off:off + n] if off >= 0 else b''
        self.ok(len(a1) == n and zlib.crc32(a1) == p['entries_crc'], "the primary table's checksum")
        self.ok(len(a2) == n and zlib.crc32(a2) == b['entries_crc'], "the backup table's checksum")
        self.ok(a1 == a2, 'both table copies are byte-identical')
        self.entries = []
        for i in range(p['count']):
            raw = a1[i * p['entry_size']:(i + 1) * p['entry_size']]
            if raw[:16] != bytes(16):
                first, lastb = struct.unpack_from('<QQ', raw, 32)
                self.entries.append(dict(index=i + 1, raw=raw, type=uuid.UUID(bytes_le=raw[:16]), guid=uuid.UUID(bytes_le=raw[16:32]),
                                         first=first, last=lastb, blocks=lastb - first + 1,
                                         name=raw[56:].decode('utf-16-le').rstrip('\0')))
        spans = sorted((e['first'], e['last']) for e in self.entries)
        self.ok(all(a[1] < b[0] for a, b in zip(spans, spans[1:])) and
                all(p['first_usable'] <= s[0] <= s[1] <= p['last_usable'] for s in spans), 'entries lie in the usable range and do not overlap')

    def ok(self, good, what):
        print('GPT %s %s' % ('ok  ' if good else 'FAIL', what))
        if not good:
            self.problems.append(what)

    def header(self, block, which):
        sig, rev, size, crc, _, my, alt, fu, lu = struct.unpack_from('<8sIIIIQQQQ', block)
        disk = block[56:72]
        entries, count, esize, ecrc = struct.unpack_from('<QIII', block, 72)
        good = sig == b'EFI PART' and 92 <= size <= len(block) and zlib.crc32(block[:16] + bytes(4) + block[20:size]) == crc
        self.ok(good, 'the %s header and its checksum' % which)
        return dict(my=my, alternate=alt, first_usable=fu, last_usable=lu, disk=disk, entries=entries, count=count,
                    entry_size=esize, entries_crc=ecrc)


def check(args):
    t = Table(args.prefix)
    ours = [e for e in t.entries if e['guid'] == uuid.UUID(args.partuuid)]
    t.ok(len(ours) == 1, 'exactly one entry has our identifier')
    if ours:
        e = ours[0]
        t.ok(e['type'] == LINUX and e['first'] == args.first and e['blocks'] == args.blocks,
             'our entry (table position %d): Linux filesystem, first block %d, %d blocks (wanted %d, %d)' % (
                 e['index'], e['first'], e['blocks'], args.first, args.blocks))
    known = None
    if args.before:
        print('GPT      the earlier table:')
        b = Table(args.before)
        t.problems += ['earlier table: ' + p for p in b.problems]
        now = {e['raw'] for e in t.entries}
        gone = [e for e in b.entries if e['raw'] not in now]
        t.ok(not gone, 'every earlier entry is still there byte for byte (%d of %d)' % (len(b.entries) - len(gone), len(b.entries)))
        t.ok(t.primary['disk'] == b.primary['disk'] and (t.primary['first_usable'], t.primary['last_usable']) ==
             (b.primary['first_usable'], b.primary['last_usable']), 'disk identifier and usable range as before')
        moved = sum(1 for e in b.entries for f in t.entries if f['raw'] == e['raw'] and f['index'] != e['index'])
        print('GPT      %d earlier entries changed their position in the table (Linux partition numbers follow the position)' % moved)
        known = {e['raw'] for e in b.entries}
    if args.new is not None:
        if known is None:
            t.ok(False, '--new needs --before')
        else:
            new = [e for e in t.entries if e['raw'] not in known]
            t.ok(len(new) == args.new, '%d new entries (wanted %d)' % (len(new), args.new))
            for e in new:
                kind = 'APFS' if e['type'] == APFS else 'EFI-typed' if e['type'] == EFI else 'of an unexpected type'
                print('GPT      new: table position %d, %s, blocks %d..%d (%d), name "%s", identifier %s' % (
                    e['index'], kind, e['first'], e['last'], e['blocks'], e['name'], str(e['guid']).lower()))
                inside = args.free_first is not None and args.free_first <= e['first'] and e['last'] <= args.free_last
                t.ok(inside, 'it lies wholly inside the space that was free (%s..%s)' % (args.free_first, args.free_last))
            if args.new == 2 and len(new) == 2:
                t.ok(sorted(e['type'] for e in new) == sorted([APFS, EFI]), 'one is APFS (the startup volume), one EFI-typed (the boot partition)')
                for e in new:
                    if e['type'] == APFS:
                        t.ok(e['blocks'] == STUB_BLOCKS, 'the startup volume has %d blocks' % STUB_BLOCKS)
                    if e['type'] == EFI:
                        t.ok(126976 <= e['blocks'] <= 131113, 'the boot partition has 126,976..131,113 blocks')
    print('GPT %s' % ('ALL OK' if not t.problems else 'FAILED: %d problem(s)' % len(t.problems)))
    return 1 if t.problems else 0


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='command', required=True)
    r = sub.add_parser('read'); r.add_argument('device'); r.add_argument('prefix')
    c = sub.add_parser('check'); c.add_argument('prefix')
    c.add_argument('--partuuid', required=True); c.add_argument('--first', type=int, required=True); c.add_argument('--blocks', type=int, required=True)
    c.add_argument('--free-first', type=int); c.add_argument('--free-last', type=int)
    c.add_argument('--before'); c.add_argument('--new', type=int, choices=(0, 2))
    args = parser.parse_args()
    if args.command == 'read':
        return read(args.device, args.prefix)
    return check(args)


if __name__ == '__main__':
    sys.exit(main())
