#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Checks a kernel collection the way the lab launcher's image gate does
(m1-bringup/normal_image.py): no two LC_SEGMENT_64 of the fileset may overlap in the address
space unless they map the same file bytes there. kc-builder accepts collections whose kext
area runs into the kernel's segments; the launcher then refuses them ("Overlapping
file-backed segments map different bytes").

    kc_overlap.py <kernel>        exit 0 and a summary, or exit 1 naming the first overlap
"""
import struct, sys

def segments(data):
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from('<IiiIIIII', data, 0)
    assert magic == 0xfeedfacf, 'not a 64-bit Mach-O'
    off, out = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            name = data[off + 8:off + 24].rstrip(b'\0').decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, off + 24)
            out.append(dict(name=name, vmaddr=vmaddr, vmsize=vmsize, fileoff=fileoff, filesize=filesize))
        off += size
    return out

def main():
    data = open(sys.argv[1], 'rb').read()
    segs = segments(data)
    bad = 0
    for i, later in enumerate(segs):
        for prior in segs[:i]:
            lo = max(later['vmaddr'], prior['vmaddr'])
            hi = min(later['vmaddr'] + later['filesize'], prior['vmaddr'] + prior['filesize'])
            if lo < hi and later['fileoff'] + lo - later['vmaddr'] != prior['fileoff'] + lo - prior['vmaddr']:
                print('OVERLAP: %s [0x%x, +0x%x) and %s [0x%x, +0x%x) share 0x%x bytes at 0x%x' % (
                    prior['name'], prior['vmaddr'], prior['filesize'], later['name'], later['vmaddr'],
                    later['filesize'], hi - lo, lo))
                bad += 1
    print('%d segments, %d overlapping pair(s)' % (len(segs), bad))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
