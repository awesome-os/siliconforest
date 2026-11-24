#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Freed ext4 inodes that the SiliconForest driver left with the extents flag over a zeroed
i_block (boot dr41): find them, and give them the empty extent root Linux leaves in a freed inode.

  sf_freed_inode_repair.py scan <device or image>             read-only: count and list
  sf_freed_inode_repair.py repair <device or image> <backup file, must not exist>
                                                              first saves every inode it is about to
                                                              write (number, byte offset, the 256
                                                              original bytes) to the backup file and
                                                              syncs it, then writes ONLY those inodes
  sf_freed_inode_repair.py restore <device or image> <backup file>
                                                              puts the saved inodes back, each one
                                                              only where the device still holds
                                                              exactly what `repair` wrote there
  sf_freed_inode_repair.py compare <before image> <after image>
                                                              every byte that differs lies inside
                                                              one of the inodes `scan` lists for
                                                              <before>; nothing else changed

An inode is touched only if ALL of these hold: the group's inode bitmap says it is free, its
link count is 0, EXT4_EXTENTS_FL is set, all 60 bytes of i_block are zero, and its present
checksum is correct (an inode this tool does not understand is left alone and reported).
What is written: i_block = an extent header with the magic, no entries, room for four, depth 0
(what ext4_ext_remove_space leaves and what the fixed driver writes), and the inode's checksum
for the new content. The flag, the deletion time and every other field stay. No bitmap, no
descriptor, no superblock, no journal block and no in-use inode is written.

It refuses a filesystem that is mounted (a block device with a mount), that needs journal
recovery, whose state is not clean, or without metadata_csum. Before the old driver's inodes
are gone e2fsck -fn reports "Inode N seems to contain garbage" for VALID inodes that share an
inode-table block with more than eight of them; e2fsck -fy would clear those valid inodes.
On the M1 this is a write to the SiliconForest partition from Linux: the owner's decision.
"""
import os
import struct
import sys

EXTENTS_FL = 0x80000
EMPTY_ROOT = struct.pack('<HHHHI', 0xF30A, 0, 4, 0, 0) + bytes(48)


def crc32c(crc, data, _table=[]):
    if not _table:
        for n in range(256):
            c = n
            for _ in range(8):
                c = (c >> 1) ^ (0x82F63B78 if c & 1 else 0)
            _table.append(c)
    for b in data:
        crc = _table[(crc ^ b) & 0xff] ^ (crc >> 8)
    return crc


class Fs:
    def __init__(self, path, write=False):
        self.f = open(path, 'r+b' if write else 'rb', buffering=0)
        sb = self.read(1024, 1024)
        if struct.unpack_from('<H', sb, 0x38)[0] != 0xEF53:
            raise SystemExit('not an ext filesystem')
        self.block = 1024 << struct.unpack_from('<I', sb, 0x18)[0]
        self.inodes_count, blocks_lo = struct.unpack_from('<II', sb, 0)
        self.first_data = struct.unpack_from('<I', sb, 0x14)[0]
        self.blocks_per_group, = struct.unpack_from('<I', sb, 0x20)
        self.inodes_per_group, = struct.unpack_from('<I', sb, 0x28)
        self.state, = struct.unpack_from('<H', sb, 0x3A)
        self.inode_size, = struct.unpack_from('<H', sb, 0x58)
        compat, incompat, ro = struct.unpack_from('<III', sb, 0x5C)
        self.is64 = bool(incompat & 0x80)
        self.desc_size = struct.unpack_from('<H', sb, 0xFE)[0] if self.is64 else 32
        blocks = blocks_lo | (struct.unpack_from('<I', sb, 0x150)[0] << 32 if self.is64 else 0)
        self.groups = (blocks - self.first_data + self.blocks_per_group - 1) // self.blocks_per_group
        if not ro & 0x400:
            raise SystemExit('no metadata_csum: not a filesystem this tool is for')
        if incompat & 0x4:
            raise SystemExit('the journal needs recovery: refused')
        if not self.state & 1 or self.state & 2:
            raise SystemExit('the filesystem state is not clean: refused')
        if self.inode_size < 160:
            raise SystemExit('inode size %d: refused' % self.inode_size)
        self.seed = struct.unpack_from('<I', sb, 0x270)[0] if incompat & 0x2000 else crc32c(0xFFFFFFFF, sb[0x68:0x78])

    def read(self, offset, size):
        data = os.pread(self.f.fileno(), size, offset)
        if len(data) != size:
            raise SystemExit('short read at %d' % offset)
        return data

    def group(self, g):
        d = self.read((self.first_data + 1) * self.block + g * self.desc_size, self.desc_size)
        bitmap, table = struct.unpack_from('<II', d, 4)[0], struct.unpack_from('<I', d, 8)[0]
        flags, = struct.unpack_from('<H', d, 0x12)
        if self.is64 and self.desc_size >= 64:
            bitmap |= struct.unpack_from('<I', d, 0x24)[0] << 32
            table |= struct.unpack_from('<I', d, 0x28)[0] << 32
        return bitmap, table, flags

    def checksum(self, ino, raw):
        body = bytearray(raw)
        body[0x7C:0x7E] = b'\0\0'
        extra = struct.unpack_from('<H', raw, 0x80)[0]
        hi = extra >= 4
        if hi:
            body[0x82:0x84] = b'\0\0'
        c = crc32c(crc32c(crc32c(self.seed, struct.pack('<I', ino)), raw[0x64:0x68]), bytes(body))
        stored = struct.unpack_from('<H', raw, 0x7C)[0] | (struct.unpack_from('<H', raw, 0x82)[0] << 16 if hi else 0)
        return c if hi else c & 0xFFFF, stored, hi

    def candidates(self):
        """(inode number, byte offset, raw inode) of every freed inode of the kind; and those left alone."""
        found, odd = [], []
        for g in range(self.groups):
            bitmap_blk, table_blk, flags = self.group(g)
            if flags & 1:                       # INODE_UNINIT: no inode of this group was ever used
                continue
            bitmap = self.read(bitmap_blk * self.block, self.block)
            table = self.read(table_blk * self.block, self.inodes_per_group * self.inode_size)
            for i in range(self.inodes_per_group):
                if bitmap[i >> 3] >> (i & 7) & 1:
                    continue
                raw = table[i * self.inode_size:(i + 1) * self.inode_size]
                links, = struct.unpack_from('<H', raw, 0x1A)
                iflags, = struct.unpack_from('<I', raw, 0x20)
                if links or not iflags & EXTENTS_FL or any(raw[0x28:0x64]):
                    continue
                ino = g * self.inodes_per_group + i + 1
                want, stored, _ = self.checksum(ino, raw)
                (found if want == stored else odd).append((ino, table_blk * self.block + i * self.inode_size, raw))
        return found, odd

    def repaired(self, ino, raw):
        new = bytearray(raw)
        new[0x28:0x64] = EMPTY_ROOT
        c, _, hi = self.checksum(ino, bytes(new))
        struct.pack_into('<H', new, 0x7C, c & 0xFFFF)
        if hi:
            struct.pack_into('<H', new, 0x82, c >> 16)
        return bytes(new)


def mounted(path):
    real = os.path.realpath(path)
    try:
        return any(os.path.realpath(line.split()[0]) == real for line in open('/proc/mounts') if line.startswith('/'))
    except OSError:
        return False


def ranges(numbers):
    out, start, prev = [], None, None
    for n in numbers:
        if start is None:
            start = prev = n
        elif n == prev + 1:
            prev = n
        else:
            out.append((start, prev))
            start = prev = n
    if start is not None:
        out.append((start, prev))
    return ' '.join('%d' % a if a == b else '%d-%d' % (a, b) for a, b in out)


def main(argv):
    if len(argv) == 4 and argv[1] == 'restore':
        if mounted(argv[2]):
            raise SystemExit('%s is mounted: refused' % argv[2])
        fs = Fs(argv[2], write=True)
        data = open(argv[3], 'rb').read()
        head = struct.Struct('<8sIQI')            # magic, inode number, byte offset, inode size
        size = head.size + fs.inode_size
        if not data or len(data) % size:
            raise SystemExit('the backup file is not whole')
        records = []
        for at in range(0, len(data), size):
            magic, ino, off, isize = head.unpack_from(data, at)
            if magic != b'SFINODE1' or isize != fs.inode_size:
                raise SystemExit('not a backup of this tool, or of another inode size')
            records.append((ino, off, data[at + head.size:at + size]))
        back = same = other = 0
        for ino, off, raw in records:
            now = fs.read(off, fs.inode_size)
            if now == raw:
                same += 1
            elif now == fs.repaired(ino, raw):
                os.pwrite(fs.f.fileno(), raw, off)
                back += 1
            else:
                other += 1
        os.fsync(fs.f.fileno())
        print('SFREPAIR restore: %d inodes put back, %d were already as saved, %d hold something else and were left alone' % (back, same, other))
        return 0 if other == 0 else 1
    if (len(argv) == 3 and argv[1] == 'scan') or (len(argv) == 4 and argv[1] == 'repair'):
        if mounted(argv[2]):
            raise SystemExit('%s is mounted: refused' % argv[2])
        if argv[1] == 'repair' and os.path.exists(argv[3]):
            raise SystemExit('the backup file exists: refused')
        fs = Fs(argv[2], write=argv[1] == 'repair')
        found, odd = fs.candidates()
        blocks = {}
        for ino, off, _ in found:
            blocks.setdefault(off // fs.block, []).append(ino)
        over = sum(1 for inos in blocks.values() if len(inos) > (fs.block // fs.inode_size) // 2)
        print('SFREPAIR %s: %d freed inodes with the extents flag and a zeroed i_block, in %d inode-table blocks '
              '(%d of them more than half such inodes: the ones e2fsck objects to); %d left alone (checksum not as expected)'
              % (argv[1], len(found), len(blocks), over, len(odd)))
        print('SFREPAIR inodes: ' + ranges([ino for ino, _, _ in found]))
        if odd:
            print('SFREPAIR left alone: ' + ranges([ino for ino, _, _ in odd]))
        # the inode numbers of the blocks e2fsck objects to (it reports the VALID inodes in them as garbage)
        per = fs.block // fs.inode_size
        firsts = sorted(min(inos) - (min(o for i, o, _ in found if i == min(inos)) % fs.block) // fs.inode_size
                        for inos in blocks.values() if len(inos) > per // 2)
        print('SFREPAIR blocks over half: ' + ' '.join('%d-%d' % (f, f + per - 1) for f in firsts))
        if argv[1] == 'repair':
            # the originals first, on stable storage, before the first write
            with open(argv[3], 'xb') as backup:
                for ino, off, raw in found:
                    backup.write(struct.pack('<8sIQI', b'SFINODE1', ino, off, fs.inode_size) + raw)
                backup.flush()
                os.fsync(backup.fileno())
            saved = os.path.getsize(argv[3])
            if saved != len(found) * (24 + fs.inode_size):
                raise SystemExit('the backup file is not complete: nothing written')
            print('SFREPAIR backup: %d inodes, %d bytes, in %s' % (len(found), saved, argv[3]))
            for ino, off, raw in found:
                if fs.read(off, fs.inode_size) != raw:
                    raise SystemExit('inode %d changed under the tool: stopped' % ino)
                os.pwrite(fs.f.fileno(), fs.repaired(ino, raw), off)
            os.fsync(fs.f.fileno())
            again, _ = Fs(argv[2]).candidates()
            print('SFREPAIR written: %d inodes; a second scan finds %d' % (len(found), len(again)))
        return 0
    if len(argv) == 4 and argv[1] == 'compare':
        before = Fs(argv[2])
        allowed = {}
        for ino, off, raw in before.candidates()[0]:
            allowed[off] = (ino, before.repaired(ino, raw))
        a, b = open(argv[2], 'rb'), open(argv[3], 'rb')
        size = os.path.getsize(argv[2])
        if size != os.path.getsize(argv[3]):
            raise SystemExit('the images differ in size')
        changed_inodes, bad, pos, step = set(), 0, 0, before.inode_size
        while pos < size:
            x, y = a.read(1 << 20), b.read(1 << 20)
            if x != y:
                for o in range(0, len(x), step):
                    if x[o:o + step] != y[o:o + step]:
                        hit = allowed.get(pos + o)
                        if hit and y[o:o + step] == hit[1]:
                            changed_inodes.add(hit[0])
                        else:
                            bad += 1
            pos += len(x)
        print('SFREPAIR compare: %d inodes changed, each one a listed freed inode with exactly the expected new content; '
              '%d listed inodes unchanged; %d other %d-byte pieces differ' % (len(changed_inodes), len(allowed) - len(changed_inodes), bad, step))
        return 0 if bad == 0 else 1
    raise SystemExit(__doc__)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
