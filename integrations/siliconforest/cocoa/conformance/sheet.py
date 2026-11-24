#!/usr/bin/env python3
"""Compare the control sheets: every control drawn by macOS against the same control drawn here.

  sheet.py REF.json REF-controls.png OURS.json OURS-controls.png [--key sheet|sheetActive]
           [--out scorecard.md] [--montage side-by-side.png] [--threshold 24]

Both pictures come from conformance/metrics.m (`metrics <path>` writes <path>-controls.png and
<path>-controls-active.png, and where it put each control into the JSON under "sheet" and
"sheetActive"). Each control is cut out of its own picture by its own frame plus a margin, put
over the same background (macOS's window background from the reference JSON; the reference
picture is transparent there), and the two cut-outs are compared with their frames' top left
corners together:

  frame       the control's size on each side (points)
  ink         the box that differs from the background, relative to the frame (points)
  mean        mean absolute difference per channel over the larger cut-out, 0..255
  off         share of pixels that differ by more than the threshold in some channel

The pictures must be at the same scale (the reference Mac renders at 2: run ours with
`cocoa_check.sh metrics --scale 2`). Needs Pillow. Pictures of macOS stay under runs/.
"""
import argparse
import json
import sys

from PIL import Image, ImageDraw

MARGIN = 6  # points around each frame


def background(reference):
    c = reference.get('colors', {}).get('windowBackgroundColor') or {}
    return tuple(int(round(255 * c.get(k, 0.925))) for k in ('r', 'g', 'b'))


def cut(image, sheet, frame, scale, back):
    """The control's cut-out over the background, MARGIN points around its frame."""
    height = sheet['size']['h']
    x0 = int(round((frame['x'] - MARGIN) * scale))
    y0 = int(round((height - frame['y'] - frame['h'] - MARGIN) * scale))
    x1 = int(round((frame['x'] + frame['w'] + MARGIN) * scale))
    y1 = int(round((height - frame['y'] + MARGIN) * scale))
    piece = image.crop((x0, y0, x1, y1)).convert('RGBA')
    flat = Image.new('RGBA', piece.size, back + (255,))
    flat.alpha_composite(piece)
    return flat.convert('RGB')


def ink(piece, back, scale, threshold=6):
    """Box of what is not background, in points from the frame's top left corner."""
    px = piece.load()
    xs, ys = [], []
    for y in range(piece.size[1]):
        for x in range(piece.size[0]):
            if max(abs(a - b) for a, b in zip(px[x, y], back)) > threshold:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return tuple(round(v / scale - MARGIN, 1) for v in (min(xs), min(ys), max(xs) + 1, max(ys) + 1))


def compare(a, b, back, threshold):
    w, h = max(a.size[0], b.size[0]), max(a.size[1], b.size[1])
    pa = Image.new('RGB', (w, h), back)
    pb = Image.new('RGB', (w, h), back)
    pa.paste(a, (0, 0))
    pb.paste(b, (0, 0))
    da, db = pa.load(), pb.load()
    total = off = 0
    diff = Image.new('RGB', (w, h), (255, 255, 255))
    dd = diff.load()
    for y in range(h):
        for x in range(w):
            d = [abs(p - q) for p, q in zip(da[x, y], db[x, y])]
            total += sum(d)
            worst = max(d)
            if worst > threshold:
                off += 1
            if worst:
                dd[x, y] = (255, max(0, 255 - worst * 3), max(0, 255 - worst * 3))
    return total / (3.0 * w * h), off / float(w * h), pa, pb, diff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('reference_json')
    ap.add_argument('reference_png')
    ap.add_argument('ours_json')
    ap.add_argument('ours_png')
    ap.add_argument('--key', default='sheet')
    ap.add_argument('--out')
    ap.add_argument('--montage')
    ap.add_argument('--threshold', type=int, default=24)
    args = ap.parse_args()

    ref = json.load(open(args.reference_json))
    ours = json.load(open(args.ours_json))
    rsheet, osheet = ref.get(args.key), ours.get(args.key)
    if not rsheet or not osheet:
        sys.exit('no "%s" in one of the JSON files' % args.key)
    rimg, oimg = Image.open(args.reference_png), Image.open(args.ours_png)
    rscale = rimg.size[0] / rsheet['size']['w']
    oscale = oimg.size[0] / osheet['size']['w']
    if abs(rscale - oscale) > 0.01:
        sys.exit('the pictures are at different scales (%.2f and %.2f)' % (rscale, oscale))
    back = background(ref)

    rows, strips = [], []
    for key in rsheet:
        if key == 'size':
            continue
        if key not in osheet:
            rows.append((key, None))
            continue
        a = cut(rimg, rsheet, rsheet[key], rscale, back)
        b = cut(oimg, osheet, osheet[key], oscale, back)
        mean, off, pa, pb, diff = compare(a, b, back, args.threshold)
        rows.append((key, dict(rframe=(rsheet[key]['w'], rsheet[key]['h']), oframe=(osheet[key]['w'], osheet[key]['h']),
                               rink=ink(a, back, rscale), oink=ink(b, back, oscale), mean=mean, off=off)))
        strips.append((key, pa, pb, diff))

    lines = ['# Control sheet (%s): %s against %s' % (args.key, args.ours_png, args.reference_png), '',
             'Scale %.2f, background %s, a pixel counts as off above %d of 255 in some channel.' % (rscale, back, args.threshold), '',
             '| Control | Frame macOS | Frame ours | Ink macOS | Ink ours | Mean difference | Pixels off |', '|---|---|---|---|---|---|---|']
    done = [r for r in rows if r[1]]
    for key, r in rows:
        if r is None:
            lines.append('| %s | | missing | | | | |' % key)
            continue
        lines.append('| %s | %gx%g | %gx%g | %s | %s | %.2f | %.1f%% |' % (
            key, r['rframe'][0], r['rframe'][1], r['oframe'][0], r['oframe'][1], r['rink'], r['oink'], r['mean'], 100 * r['off']))
    if done:
        same = sum(1 for _, r in done if r['rframe'] == r['oframe'])
        lines[2:2] = ['%d controls: %d with the same frame size; mean difference %.2f of 255 over all, pixels off %.1f%%; worst %s.' % (
            len(done), same, sum(r['mean'] for _, r in done) / len(done), 100 * sum(r['off'] for _, r in done) / len(done),
            ', '.join('%s (%.1f%%)' % (k, 100 * r['off']) for k, r in sorted(done, key=lambda kr: -kr[1]['off'])[:5])), '']
    text = '\n'.join(lines) + '\n'
    if args.out:
        open(args.out, 'w').write(text)
    print(text)

    if args.montage and strips:
        width = max(s[1].size[0] for s in strips)
        label = 150
        height = sum(s[1].size[1] + 4 for s in strips)
        sheet = Image.new('RGB', (label + 3 * (width + 8), height + 16), (255, 255, 255))
        draw = ImageDraw.Draw(sheet)
        draw.text((label, 2), 'macOS', fill=(0, 0, 0))
        draw.text((label + width + 8, 2), 'SiliconForest', fill=(0, 0, 0))
        draw.text((label + 2 * (width + 8), 2), 'difference', fill=(0, 0, 0))
        y = 16
        for key, pa, pb, diff in strips:
            draw.text((2, y + 4), key, fill=(0, 0, 0))
            sheet.paste(pa, (label, y))
            sheet.paste(pb, (label + width + 8, y))
            sheet.paste(diff, (label + 2 * (width + 8), y))
            y += pa.size[1] + 4
        sheet.save(args.montage)


if __name__ == '__main__':
    main()
