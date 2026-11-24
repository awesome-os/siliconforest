#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The boot picture for the M1's panel, in the firmware framebuffer's own pixel format.

  boot_logo.py pack <picture.png> <out-dir>     writes boot-logo.gz and boot-logo.json
  boot_logo.py unpack <framebuffer.bin> <out.png> [--scale N]
  boot_logo.py compare <framebuffer.bin> <out-dir of pack>

pack: the picture (default use: the desktop background, so the boot picture and the desktop
match) centred on black at 2560x1600, as 32-bit little-endian x2r10g10b10 rows (what iBoot
hands over on J293: depth 30, stride 10240), gzip-compressed for the m1n1 proxy's gzdec.
boot-logo.json names the geometry and the SHA-256 of the uncompressed pixels; the launcher
paints only when the live framebuffer has that geometry.

unpack: a framebuffer dump saved by the launcher (guest/framebuffer.bin of a boot cycle) as a
PNG. compare: whether such a dump is byte-identical to the packed picture, and if not how
many pixels differ and their bounding box.
"""
import gzip
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

WIDTH, HEIGHT, STRIDE, DEPTH = 2560, 1600, 10240, 30


def to_native(rgb8):
    c = rgb8.astype(np.uint32)
    c10 = (c << 2) | (c >> 6)
    return ((c10[..., 0] << 20) | (c10[..., 1] << 10) | c10[..., 2]).astype('<u4')


def from_native(raw):
    px = np.frombuffer(raw, dtype='<u4').reshape(HEIGHT, STRIDE // 4)[:, :WIDTH]
    return np.stack([(px >> 22) & 0xff, (px >> 12) & 0xff, (px >> 2) & 0xff], axis=-1).astype(np.uint8)


def pack(picture, out):
    image = Image.open(picture).convert('RGB')
    if image.width > WIDTH or image.height > HEIGHT:
        image.thumbnail((WIDTH, HEIGHT), Image.LANCZOS)
    canvas = Image.new('RGB', (WIDTH, HEIGHT), (0, 0, 0))
    canvas.paste(image, ((WIDTH - image.width) // 2, (HEIGHT - image.height) // 2))
    raw = to_native(np.asarray(canvas)).tobytes()
    assert len(raw) == STRIDE * HEIGHT
    out.mkdir(parents=True, exist_ok=True)
    with open(out / 'boot-logo.gz', 'wb') as f, gzip.GzipFile(fileobj=f, mode='wb', compresslevel=9, mtime=0) as z:
        z.write(raw)
    meta = dict(width=WIDTH, height=HEIGHT, stride=STRIDE, depth=DEPTH, bytes=len(raw),
                sha256=hashlib.sha256(raw).hexdigest(), format='x2r10g10b10 little-endian')
    (out / 'boot-logo.json').write_text(json.dumps(meta, indent=1) + '\n')
    print(json.dumps(dict(meta, compressed=(out / 'boot-logo.gz').stat().st_size)))


def unpack(dump, out, scale):
    image = Image.fromarray(from_native(Path(dump).read_bytes()))
    if scale > 1:
        image = image.resize((WIDTH // scale, HEIGHT // scale), Image.LANCZOS)
    image.save(out)


def compare(dump, packed):
    got = np.frombuffer(Path(dump).read_bytes(), dtype='<u4').reshape(HEIGHT, STRIDE // 4)
    want = np.frombuffer(gzip.decompress((Path(packed) / 'boot-logo.gz').read_bytes()), dtype='<u4').reshape(got.shape)
    differ = got != want
    result = dict(identical=not differ.any(), differing_pixels=int(differ.sum()),
                  dump_sha256=hashlib.sha256(got.tobytes()).hexdigest())
    if differ.any():
        rows, cols = np.where(differ.any(axis=1))[0], np.where(differ.any(axis=0))[0]
        result['bounding_box'] = dict(x0=int(cols[0]), y0=int(rows[0]), x1=int(cols[-1]), y1=int(rows[-1]))
    print(json.dumps(result))
    return 0 if result['identical'] else 1


def main(argv):
    if len(argv) >= 4 and argv[1] == 'pack':
        pack(argv[2], Path(argv[3]))
    elif len(argv) >= 4 and argv[1] == 'unpack':
        unpack(argv[2], argv[3], int(argv[5]) if len(argv) > 5 and argv[4] == '--scale' else 1)
    elif len(argv) == 4 and argv[1] == 'compare':
        return compare(argv[2], argv[3])
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
