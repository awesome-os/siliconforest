#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SiliconForest stage image: the stage m1n1 followed by PDSF records (src/pdsf.h).

    pdsf_pack.py pack --m1n1 out/m1n1-sf-stage.bin --kernel <collection>/kernel \
        [--ramdisk root.img] --bootargs '<kernel command line>' [--set key=value ...] -o image.bin
    pdsf_pack.py pack --kernel <collection>/kernel --bootargs '...' [--set ...] -o kernel.bin
        (no --m1n1: the record list alone, the file sf/kernel.bin an installed stage 2 reads)
    pdsf_pack.py list image.bin | kernel.bin

Also imported by the launcher on the controller (native_boot.py, mode word `stage`): standard
library only. Nothing here talks to a device or writes anywhere but the named output file.
"""
import argparse
import struct
import sys
import zlib

MAGIC, VERSION, ALIGN = 0x46534450, 1, 16
END, CONFIG, KERNEL, RAMDISK = 0, 1, 2, 3
NAMES = {END: 'end', CONFIG: 'config', KERNEL: 'kernel', RAMDISK: 'ramdisk'}
F_GZIP = 1
HEADER = struct.Struct('<IHHIIQQ')
CONFIG_KEYS = ('display', 'usb-nodes', 'sprr', 'amx', 'serial', 'wdt', 'reserve-mb', 'seed-hex', 'xnu-base')
# Records travel inside the image m1n1 was loaded with; a stage 1 that chainloads from the EFI
# partition accepts about 64 MiB (DESIGN.md, "Sizes").
ESP_IMAGE_LIMIT = 60 << 20
# m1n1 src/xnuboot.h: capacity includes the terminating NUL. The M1 uses v2.
BOOTARGS_CAPACITY = {1: 256, 2: 608, 3: 1024}


def gzip_bytes(data, level=6):
    packer = zlib.compressobj(level, zlib.DEFLATED, 31)
    return packer.compress(data) + packer.flush()


def record(kind, data=b'', raw=None, flags=0):
    if kind == END:
        return HEADER.pack(MAGIC, VERSION, END, 0, 0, 0, 0)
    if not data:
        raise ValueError('empty record: ' + NAMES[kind])
    raw = len(data) if raw is None else raw
    if not flags & F_GZIP and raw != len(data):
        raise ValueError('a plain record has one size')
    body = HEADER.pack(MAGIC, VERSION, kind, flags, zlib.crc32(data), len(data), raw) + data
    return body + bytes(-len(body) % ALIGN)


def config_text(bootargs, *, bootargs_revision=2, **keys):
    """The CONFIG record: `bootargs=` and any of CONFIG_KEYS (underscores for dashes)."""
    if bootargs_revision not in BOOTARGS_CAPACITY:
        raise ValueError('bootargs revision must be 1, 2 or 3')
    capacity = BOOTARGS_CAPACITY[bootargs_revision]
    if not bootargs or len(bootargs) >= capacity or any(not 32 <= ord(c) < 127 for c in bootargs):
        raise ValueError('bootargs revision %d: 1..%d printable ASCII characters' %
                         (bootargs_revision, capacity - 1))
    lines = ['bootargs=' + bootargs]
    for key, value in keys.items():
        key = key.replace('_', '-')
        if key not in CONFIG_KEYS:
            raise ValueError('unknown configuration key: ' + key)
        if value is not None:
            lines.append('%s=%s' % (key, value))
    return ('\n'.join(lines) + '\n').encode('ascii')


def records(config, kernel, ramdisk=None, compress_kernel=True, level=6):
    out = [record(CONFIG, config)]
    if compress_kernel:
        out.append(record(KERNEL, gzip_bytes(kernel, level), len(kernel), F_GZIP))
    else:
        out.append(record(KERNEL, kernel))
    if ramdisk is not None:
        out.append(record(RAMDISK, gzip_bytes(ramdisk, level), len(ramdisk), F_GZIP))
    out.append(record(END))
    return b''.join(out)


def image(m1n1, config, kernel, ramdisk=None, **options):
    """m1n1 image + records + the four zero bytes m1n1's own payload list ends with."""
    if len(m1n1) % 0x4000 or m1n1[0x808:0x810] != b'AURALOAD':
        raise ValueError('not the raw stage m1n1 image')
    return m1n1 + records(config, kernel, ramdisk, **options) + bytes(4)


def parse(data, offset=0):
    """[(type, flags, stored, raw, crc_ok, data offset)] of a record list; ValueError if damaged."""
    found = []
    while True:
        if offset + HEADER.size > len(data):
            raise ValueError('record list is cut off')
        magic, version, kind, flags, crc, stored, raw = HEADER.unpack_from(data, offset)
        if magic != MAGIC or version != VERSION or kind not in NAMES:
            raise ValueError('not a record at %#x' % offset)
        offset += HEADER.size
        if kind == END:
            return found, offset
        if offset + stored > len(data):
            raise ValueError('record reaches past the end')
        body = data[offset:offset + stored]
        found.append((kind, flags, stored, raw, zlib.crc32(body) == crc, offset))
        offset += stored + (-(HEADER.size + stored) % ALIGN)


def records_offset(data):
    """Offset of the record list in an image: the stage m1n1's footprint from its load header."""
    if data[:4] == struct.pack('<I', MAGIC):
        return 0                                    # the record list alone (sf/kernel.bin)
    if data[0x808:0x810] != b'AURALOAD':
        raise ValueError('no m1n1 load header')
    return struct.unpack_from('<Q', data, 0x808 + 0x10)[0]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = parser.add_subparsers(dest='command', required=True)
    pack = sub.add_parser('pack')
    pack.add_argument('--m1n1')
    pack.add_argument('--kernel', required=True)
    pack.add_argument('--ramdisk')
    pack.add_argument('--bootargs', required=True)
    pack.add_argument('--bootargs-revision', type=int, choices=BOOTARGS_CAPACITY, default=2,
                      help='firmware BootArgs revision (M1: 2, at most 607 characters)')
    pack.add_argument('--set', action='append', default=[], metavar='key=value')
    pack.add_argument('-o', '--output', required=True)
    show = sub.add_parser('list')
    show.add_argument('image')
    args = parser.parse_args(argv)
    if args.command == 'list':
        data = open(args.image, 'rb').read()
        at = records_offset(data)
        found, end = parse(data, at)
        print('m1n1 %d bytes, records at %#x, image %d bytes' % (at, at, len(data)))
        for kind, flags, stored, raw, ok, _ in found:
            print('%-8s %10d bytes%s, %10d in memory, crc %s' % (
                NAMES[kind], stored, ' gzip' if flags & F_GZIP else '', raw, 'ok' if ok else 'WRONG'))
        return 0 if all(item[4] for item in found) else 1
    keys = dict(item.split('=', 1) for item in args.set)
    config = config_text(args.bootargs, bootargs_revision=args.bootargs_revision, **keys)
    m1n1 = open(args.m1n1, 'rb').read() if args.m1n1 else b''
    kernel = open(args.kernel, 'rb').read()
    ramdisk = open(args.ramdisk, 'rb').read() if args.ramdisk else None
    if args.m1n1:
        data = image(m1n1, config, kernel, ramdisk)
    else:
        data = records(config, kernel, ramdisk)
    with open(args.output, 'wb') as out:
        out.write(data)
    print('%s: %d bytes (m1n1 %d, kernel %d%s)' % (
        args.output, len(data), len(m1n1), len(kernel),
        ', ramdisk %d' % len(ramdisk) if ramdisk is not None else ''))
    if len(data) > ESP_IMAGE_LIMIT:
        print('note: larger than %d MiB: loadable into RAM by the launcher, not by a stage 1 from '
              'the EFI partition' % (ESP_IMAGE_LIMIT >> 20))
    return 0


if __name__ == '__main__':
    sys.exit(main())
