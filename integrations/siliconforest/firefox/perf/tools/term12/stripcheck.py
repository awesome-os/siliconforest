#!/usr/bin/env python3
"""stripcheck.py <strip.bin> [png-out-dir]: per recorded frame, the column strip (32 px wide, the whole drawable tall)
read top-down and classified per row: W white page, G grey border, R red (video top-left), B blue (video bottom-left),
K black text/other, and anything else ('?': a blend). Correct frames show runs ... W G R... B... G W ...; checks:
  edge-blend: a row at a video's top or bottom edge (next to G) that is neither R/B nor G (a mix: the opposite edge's
              pixels pulled in by a wrapped linear sample)
  tear: a run order other than W* G R+ ?{0,2} B+ G (a video's picture and its border/tile not in the same place)
  rowsplit: a row whose 32 px are not all of one class (a horizontal line ending inside the strip)
Writes a summary and, with a directory, PNGs of the frames with findings (strip x8 wide for viewing)."""
import struct, sys, collections


def cls(r, g, b):
    if r > 235 and g > 235 and b > 235: return 'W'
    if abs(r - 128) < 14 and abs(g - 128) < 14 and abs(b - 128) < 14: return 'G'
    if r > 200 and g < 50 and b < 50: return 'R'
    if b > 200 and r < 50 and g < 50: return 'B'
    if r < 40 and g < 40 and b < 40: return 'K'
    return '?'


def frames(path):
    d = open(path, 'rb').read(); off = 0
    while off + 16 <= len(d):
        magic, n, ms, h = struct.unpack('<IIII', d[off:off + 16])
        if magic != 0x54534143: break
        w = 32; size = w * h * 4
        yield n, ms, h, d[off + 16:off + 16 + size]
        off += 16 + size


def analyse(n, h, px, top=128):
    """rows top-down; the browser chrome (above <top>) and the last rows are not page"""
    w = 32; rows = []
    for y in range(h - 1, -1, -1):          # GL rows bottom-up -> top-down
        base = y * w * 4
        rows.append([cls(px[base + 4 * x], px[base + 4 * x + 1], px[base + 4 * x + 2]) for x in range(0, w, 4)])
    find = []
    line = [max(set(c), key=c.count) for c in rows]
    for i, c in enumerate(rows):
        if i > top and len(set(c)) > 1 and not (set(c) <= {'W', 'K', '?'}):
            find.append(('rowsplit', i, ''.join(c)))
    runs = []
    for i, c in enumerate(line):
        if i < top: continue
        if runs and runs[-1][0] == c: runs[-1][2] = i
        else: runs.append([c, i, i])
    for k, (c, a, b) in enumerate(runs):
        prev = runs[k - 1] if k > 0 else None; nxt = runs[k + 1] if k + 1 < len(runs) else None
        if c == 'R' and prev is not None and prev[0] != 'G':
            if prev[0] == '?' and k > 1 and runs[k - 2][0] == 'G':
                find.append(('edge-blend-top', a, 'G>?x%d>R' % (prev[2] - prev[1] + 1)))
            else:
                find.append(('tear-top', a, '%s>R' % prev[0]))
        if c == 'B' and nxt is not None and nxt[0] != 'G' and nxt[1] < h - 2:
            if nxt[0] == '?' and k + 2 < len(runs) and runs[k + 2][0] == 'G':
                find.append(('edge-blend-bottom', b, 'B>?x%d>G' % (nxt[2] - nxt[1] + 1)))
            else:
                find.append(('tear-bottom', b, 'B>%s' % nxt[0]))
        if c == 'G' and prev is not None and nxt is not None and b - a + 1 > 9:
            find.append(('border-thick', a, 'G x%d' % (b - a + 1)))
    return line, runs, find


def main():
    path = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else None
    tot = collections.Counter(); bad = 0; n = 0; shown = 0
    for fn, ms, h, px in frames(path):
        n += 1
        line, runs, find = analyse(fn, h, px)
        kinds = collections.Counter(f[0] for f in find)
        tot.update(kinds)
        if find:
            bad += 1
            if shown < 12:
                print('frame %d t=%d: %s' % (fn, ms, '; '.join('%s@%d %s' % f for f in find[:6])))
                shown += 1
            if out:
                from PIL import Image
                im = Image.frombytes('RGBA', (32, h), px).transpose(Image.FLIP_TOP_BOTTOM).resize((256, h), Image.NEAREST)
                im.save('%s/f%04d.png' % (out, fn))
    print('frames %d, with findings %d: %s' % (n, bad, dict(tot)))


if __name__ == '__main__':
    main()
