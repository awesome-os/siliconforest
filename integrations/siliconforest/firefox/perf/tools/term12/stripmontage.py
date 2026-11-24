#!/usr/bin/env python3
"""stripmontage.py <strip.bin> <first> <count> <out.png> [y0 y1]: frames side by side (each 32 px wide, 2 px gap)"""
import sys
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/sf-term12/tools')
from stripcheck import frames
from PIL import Image
path, first, count, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
y0 = int(sys.argv[5]) if len(sys.argv) > 5 else 0; y1 = int(sys.argv[6]) if len(sys.argv) > 6 else None
ims = []
for n, ms, h, px in frames(path):
    if first <= n < first + count:
        im = Image.frombytes('RGBA', (32, h), px).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB')
        ims.append(im.crop((0, y0, 32, y1 or h)))
H = ims[0].height
M = Image.new('RGB', (len(ims) * 34, H), (0, 0, 0))
for i, im in enumerate(ims):
    M.paste(im, (i * 34, 0))
M.save(out); print(M.size)
