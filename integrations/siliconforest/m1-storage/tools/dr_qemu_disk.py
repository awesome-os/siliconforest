#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A stand-in for the M1's disk for QEMU: a raw image with 4096-byte sectors, a GPT (both
copies) and one "Linux filesystem" partition holding an ext4 image, with a second, empty
Linux-typed partition and an APFS-typed one beside it so that "the other partitions" exist.

  dr_qemu_disk.py <ext4 image> <output> [--guid <partition GUID>] [--grow <MiB>]

--grow enlarges the partition beyond the image (the filesystem keeps its size unless
resize2fs is run on the result). Prints the partition's first block, length and GUID.
Attach with:  -drive if=none,id=d,file=<output>,format=raw[,readonly=on]
              -device virtio-blk-pci,drive=d,logical_block_size=4096,physical_block_size=4096
"""
import argparse, struct, uuid, zlib
from pathlib import Path

B = 4096
LINUX = uuid.UUID('0FC63DAF-8483-4772-8E79-3D69D8477DE4')
APFS = uuid.UUID('7C3457EF-0000-11AA-AA11-00306543ECAC')

p = argparse.ArgumentParser()
p.add_argument('image', type=Path); p.add_argument('output', type=Path)
p.add_argument('--guid', default=str(uuid.uuid4())); p.add_argument('--grow', type=int, default=0)
a = p.parse_args()
fs = a.image.read_bytes()
assert len(fs) % B == 0
blocks = len(fs) // B + a.grow * 256
other = 4096                                    # 16 MiB each for the two neighbours
first = 6 + other                               # as on the M1: the table ends at block 5
total = first + blocks + other + 5 + 1
total += -total % 256
parts = [(APFS, uuid.uuid4(), 6, other, 'before'), (LINUX, uuid.UUID(a.guid), first, blocks, 'SiliconForest'),
         (LINUX, uuid.uuid4(), first + blocks, other, 'neighbour')]
table = bytearray(128 * 128)
for i, (t, u, s, n, name) in enumerate(parts):
    struct.pack_into('<16s16sQQQ72s', table, i * 128, t.bytes_le, u.bytes_le, s, s + n - 1, 0, name.encode('utf-16-le'))
disk_guid = uuid.uuid4().bytes_le
def header(me, alt, tab):
    h = bytearray(struct.pack('<8sIIIIQQQQ16sQIII', b'EFI PART', 0x10000, 92, 0, 0, me, alt, 6, total - 7, disk_guid, tab, 128, 128,
                              zlib.crc32(bytes(table))))
    struct.pack_into('<I', h, 16, zlib.crc32(bytes(h)))
    return bytes(h).ljust(B, b'\0')
pmbr = bytearray(B)
struct.pack_into('<B3sB3sII', pmbr, 446, 0, b'\0\x02\0', 0xee, b'\xff\xff\xff', 1, min(total - 1, 0xffffffff)); pmbr[510:512] = b'\x55\xaa'
with open(a.output, 'wb') as f:
    f.truncate(total * B)
    f.write(pmbr); f.write(header(1, total - 1, 2)); f.write(table)
    f.seek(first * B); f.write(fs)
    f.seek((total - 5) * B); f.write(table); f.write(header(total - 1, 1, total - 5))
print('first=%d blocks=%d total=%d guid=%s' % (first, blocks, total, a.guid))
