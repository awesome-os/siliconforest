#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The SiliconForest pointer set: one family of macOS-like cursors, drawn here (DESK7).

Every cursor is a few closed shapes on a 24x24 grid of logical units (one unit = one pixel at
100 %), drawn as a body with an outline: first every shape stroked with the outline colour
(twice the outline width, round joins), then every shape filled with the body colour, so
overlapping shapes read as one silhouette; then optional thin detail strokes. Arrows, I-beams
and resize arrows are black with a white outline; hands are white with a black outline. These
are original drawings (no Apple cursor image was looked at pixel by pixel or copied).

    make_cursors.py <out-dir> [<rust-list>]

writes <out-dir>/<name>.svg for every shape (absolute M/L/C/Z path data only, the subset the
compositor's renderer reads: crates/wm-darwin/src/cursors.rs) and, with a second argument, the
Rust table that includes them (cursor_set.rs). The hotspot is the root element's
data-hotspot="x y" in the same units.
"""
import math
import sys
from pathlib import Path

K = 0.5522847498  # cubic approximation of a quarter circle

BLACK, WHITE = "#000000", "#ffffff"
OUTLINE = 1.1  # logical units outside the body


def poly(points):
    p = [("M",) + tuple(points[0])]
    p += [("L",) + tuple(q) for q in points[1:]]
    return p + [("Z",)]


def rrect(x0, y0, x1, y1, r):
    r = min(r, (x1 - x0) / 2, (y1 - y0) / 2)
    k = r * K
    return [
        ("M", x0 + r, y0), ("L", x1 - r, y0), ("C", x1 - r + k, y0, x1, y0 + r - k, x1, y0 + r),
        ("L", x1, y1 - r), ("C", x1, y1 - r + k, x1 - r + k, y1, x1 - r, y1),
        ("L", x0 + r, y1), ("C", x0 + r - k, y1, x0, y1 - r + k, x0, y1 - r),
        ("L", x0, y0 + r), ("C", x0, y0 + r - k, x0 + r - k, y0, x0 + r, y0), ("Z",),
    ]


def capsule(x0, y0, x1, y1):
    return rrect(x0, y0, x1, y1, min(x1 - x0, y1 - y0) / 2)


def circle(cx, cy, r):
    k = r * K
    return [
        ("M", cx + r, cy), ("C", cx + r, cy + k, cx + k, cy + r, cx, cy + r),
        ("C", cx - k, cy + r, cx - r, cy + k, cx - r, cy), ("C", cx - r, cy - k, cx - k, cy - r, cx, cy - r),
        ("C", cx + k, cy - r, cx + r, cy - k, cx + r, cy), ("Z",),
    ]


def arc(cx, cy, r, a0, a1, segments=None):
    """An open arc from angle a0 to a1 (degrees, clockwise on screen, 0 = right)."""
    n = segments or max(1, int(math.ceil(abs(a1 - a0) / 90)))
    step = math.radians(a1 - a0) / n
    k = 4 / 3 * math.tan(step / 4) * r
    a = math.radians(a0)
    out = [("M", cx + r * math.cos(a), cy + r * math.sin(a))]
    for _ in range(n):
        b = a + step
        out.append(("C", cx + r * math.cos(a) - k * math.sin(a), cy + r * math.sin(a) + k * math.cos(a),
                    cx + r * math.cos(b) + k * math.sin(b), cy + r * math.sin(b) - k * math.cos(b),
                    cx + r * math.cos(b), cy + r * math.sin(b)))
        a = b
    return out


def line(*points):
    return [("M",) + tuple(points[0])] + [("L",) + tuple(q) for q in points[1:]]


def xform(path, rotate=0.0, about=(12, 12), dx=0.0, dy=0.0, scale=1.0):
    c, s = math.cos(math.radians(rotate)), math.sin(math.radians(rotate))
    ax, ay = about

    def f(x, y):
        x, y = (x - ax) * scale, (y - ay) * scale
        return (ax + x * c - y * s + dx, ay + x * s + y * c + dy)

    out = []
    for cmd in path:
        if cmd[0] == "Z":
            out.append(cmd)
            continue
        pts = [f(cmd[i], cmd[i + 1]) for i in range(1, len(cmd), 2)]
        out.append((cmd[0],) + tuple(v for p in pts for v in p))
    return out


# ---- primitives -------------------------------------------------------------------------------

def arrow_body(tx, ty, scale=1.0):
    """The arrow, its tip at (tx, ty): down the left edge, the notch, the tail, back to the wing."""
    pts = [(0, 0), (0, 16.4), (3.9, 12.7), (6.4, 18.5), (8.9, 17.4), (6.5, 11.8), (11.7, 11.8)]
    return poly([(tx + x * scale, ty + y * scale) for x, y in pts])


def double_arrow(length=16.0, shaft=1.0, head=4.6, half=4.2):
    """Horizontal double-headed arrow centred on (12, 12)."""
    x0, x1, y = 12 - length / 2, 12 + length / 2, 12
    return poly([(x0, y), (x0 + head, y - half), (x0 + head, y - shaft), (x1 - head, y - shaft), (x1 - head, y - half),
                 (x1, y), (x1 - head, y + half), (x1 - head, y + shaft), (x0 + head, y + shaft), (x0 + head, y + half)])


def bar_arrows():
    """Two arrows pointing away from a central bar (column resize), centred on (12, 12)."""
    bar = rrect(11.0, 4.5, 13.0, 19.5, 0.4)
    left = poly([(3.0, 12), (7.4, 8.0), (7.4, 11.0), (10.0, 11.0), (10.0, 13.0), (7.4, 13.0), (7.4, 16.0)])
    right = poly([(21.0, 12), (16.6, 8.0), (16.6, 11.0), (14.0, 11.0), (14.0, 13.0), (16.6, 13.0), (16.6, 16.0)])
    return [bar, left, right]


def edge_arrow():
    """An arrow pointing right at a bar (resize towards one edge), centred near (12, 12)."""
    shaft = poly([(4.0, 11.0), (12.6, 11.0), (12.6, 7.4), (17.4, 12), (12.6, 16.6), (12.6, 13.0), (4.0, 13.0)])
    bar = rrect(18.4, 5.0, 20.4, 19.0, 0.4)
    return [shaft, bar]


def ibeam():
    """A text I-beam centred on (12, 12): a stem with two curved serifs at each end."""
    return [
        rrect(11.25, 5.2, 12.75, 18.8, 0.3),
        [("M", 8.6, 4.0), ("C", 10.0, 4.0, 11.4, 4.2, 12.0, 5.0), ("C", 12.6, 4.2, 14.0, 4.0, 15.4, 4.0),
         ("L", 15.4, 5.4), ("C", 14.2, 5.4, 12.8, 5.6, 12.75, 6.4), ("L", 11.25, 6.4),
         ("C", 11.2, 5.6, 9.8, 5.4, 8.6, 5.4), ("Z",)],
        [("M", 8.6, 20.0), ("C", 10.0, 20.0, 11.4, 19.8, 12.0, 19.0), ("C", 12.6, 19.8, 14.0, 20.0, 15.4, 20.0),
         ("L", 15.4, 18.6), ("C", 14.2, 18.6, 12.8, 18.4, 12.75, 17.6), ("L", 11.25, 17.6),
         ("C", 11.2, 18.4, 9.8, 18.6, 8.6, 18.6), ("Z",)],
    ]


def pointing_hand():
    """A hand with the index finger up, its tip near (9.5, 2)."""
    outline = [
        ("M", 9.5, 2.0), ("C", 10.6, 2.0, 11.3, 2.8, 11.3, 3.8), ("L", 11.3, 9.9),
        ("C", 11.6, 9.6, 12.1, 9.4, 12.7, 9.4), ("C", 13.6, 9.4, 14.2, 10.0, 14.3, 10.7),
        ("C", 14.7, 10.4, 15.2, 10.2, 15.7, 10.2), ("C", 16.6, 10.2, 17.2, 10.9, 17.2, 11.7),
        ("C", 17.5, 11.5, 17.9, 11.4, 18.3, 11.4), ("C", 19.2, 11.4, 19.8, 12.1, 19.8, 13.0),
        ("L", 19.8, 16.6), ("C", 19.8, 19.4, 17.9, 21.6, 15.0, 21.6), ("L", 11.6, 21.6),
        ("C", 9.7, 21.6, 8.7, 20.8, 7.7, 19.5), ("L", 4.3, 14.9), ("C", 3.7, 14.1, 3.9, 13.1, 4.7, 12.6),
        ("C", 5.5, 12.1, 6.4, 12.3, 7.0, 13.0), ("L", 7.7, 13.9), ("L", 7.7, 3.8),
        ("C", 7.7, 2.8, 8.4, 2.0, 9.5, 2.0), ("Z",),
    ]
    details = [line((11.3, 9.9), (11.3, 14.2)), line((14.3, 10.7), (14.3, 14.2)), line((17.2, 11.7), (17.2, 14.6))]
    return [outline], details


def open_hand():
    fingers = [capsule(6.0, 6.0, 8.6, 14.0), capsule(8.9, 3.4, 11.5, 13.0), capsule(11.8, 3.0, 14.4, 13.0), capsule(14.7, 4.6, 17.2, 13.4)]
    palm = rrect(6.0, 10.0, 17.2, 21.0, 4.0)
    thumb = xform(capsule(2.0, 12.0, 9.5, 14.6), rotate=40, about=(7.5, 14.0))
    details = [line((8.75, 11.5), (8.75, 14.0)), line((11.65, 11.0), (11.65, 14.0)), line((14.55, 11.0), (14.55, 14.0))]
    return fingers + [thumb, palm], details


def closed_hand():
    knuckles = [capsule(6.4, 8.4, 9.2, 13.0), capsule(9.2, 7.6, 12.0, 12.6), capsule(12.0, 7.6, 14.8, 12.6), capsule(14.8, 8.4, 17.4, 13.0)]
    palm = rrect(6.4, 10.4, 17.4, 20.4, 3.8)
    thumb = xform(capsule(3.4, 12.6, 9.4, 15.0), rotate=20, about=(7.0, 13.8))
    details = [line((9.2, 9.6), (9.2, 12.4)), line((12.0, 9.0), (12.0, 12.4)), line((14.8, 9.6), (14.8, 12.4))]
    return knuckles + [thumb, palm], details


# ---- the set ----------------------------------------------------------------------------------

class Cursor:
    def __init__(self, name, hotspot, shapes, body=BLACK, outline=WHITE, details=(), extra=()):
        self.name, self.hotspot, self.shapes = name, hotspot, shapes
        self.body, self.outline, self.details, self.extra = body, outline, list(details), list(extra)

    def svg(self):
        out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" data-hotspot="{fmt(self.hotspot[0])} {fmt(self.hotspot[1])}">',
               f"  <!-- SiliconForest cursor '{self.name}' (DESK7, original drawing; make_cursors.py) -->"]
        for shape in self.shapes:
            out.append(f'  <path d="{d(shape)}" fill="none" stroke="{self.outline}" stroke-width="{fmt(2 * OUTLINE)}" stroke-linejoin="round"/>')
        for shape in self.shapes:
            out.append(f'  <path d="{d(shape)}" fill="{self.body}"/>')
        for detail in self.details:
            out.append(f'  <path d="{d(detail)}" fill="none" stroke="{self.outline}" stroke-width="1" stroke-linecap="round"/>')
        out.extend(self.extra)
        out.append("</svg>")
        return "\n".join(out) + "\n"


def fmt(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def d(path):
    return " ".join(c[0] + (" " + " ".join(fmt(v) for v in c[1:]) if len(c) > 1 else "") for c in path)


def badge(kind):
    """A small sign at the arrow's lower right (copy, alias, not allowed, help, context menu, progress)."""
    cx, cy, r = 17.0, 17.0, 4.6
    ring = circle(cx, cy, r)
    out = [f'  <path d="{d(ring)}" fill="none" stroke="{WHITE}" stroke-width="2.2"/>', f'  <path d="{d(ring)}" fill="{BLACK}"/>']

    def stroke(path, width=1.4, color=WHITE, cap="round"):
        out.append(f'  <path d="{d(path)}" fill="none" stroke="{color}" stroke-width="{fmt(width)}" stroke-linecap="{cap}"/>')

    if kind == "copy":
        stroke(line((cx - 2.4, cy), (cx + 2.4, cy)))
        stroke(line((cx, cy - 2.4), (cx, cy + 2.4)))
    elif kind == "alias":
        stroke(arc(cx + 0.6, cy + 1.6, 2.6, 180, 270))
        out.append(f'  <path d="{d(poly([(cx + 0.6, cy - 2.6), (cx + 2.8, cy - 1.0), (cx + 0.6, cy + 0.6)]))}" fill="{WHITE}"/>')
    elif kind == "not-allowed":
        stroke(circle(cx, cy, 2.7), width=1.3)
        stroke(line((cx - 1.9, cy + 1.9), (cx + 1.9, cy - 1.9)), width=1.3)
    elif kind == "help":
        stroke(arc(cx, cy - 1.0, 1.7, 180, 405), width=1.3)
        stroke(line((cx + 1.2, cy + 0.2), (cx, cy + 0.8), (cx, cy + 1.4)), width=1.3)
        out.append(f'  <path d="{d(circle(cx, cy + 2.9, 0.75))}" fill="{WHITE}"/>')
    elif kind == "context-menu":
        for i, w in enumerate((3.6, 2.6, 3.0)):
            y = cy - 1.8 + i * 1.8
            stroke(line((cx - 1.8, y), (cx - 1.8 + w, y)), width=1.0, cap="butt")
    elif kind == "progress":
        stroke(arc(cx, cy, 2.5, -90, 180), width=1.4)
    return out


def arrow_with(name, kind):
    a = Cursor(name, (3, 2), [arrow_body(3, 2)])
    a.extra = badge(kind)
    return a


def build():
    cursors = [Cursor("default", (3, 2), [arrow_body(3, 2)])]
    cursors.append(Cursor("text", (12, 12), ibeam()))
    cursors.append(Cursor("vertical-text", (12, 12), [xform(s, rotate=90) for s in ibeam()]))
    shapes, details = pointing_hand()
    cursors.append(Cursor("pointer", (9, 2), shapes, body=WHITE, outline=BLACK, details=details))
    shapes, details = open_hand()
    cursors.append(Cursor("grab", (12, 11), shapes, body=WHITE, outline=BLACK, details=details))
    shapes, details = closed_hand()
    cursors.append(Cursor("grabbing", (12, 12), shapes, body=WHITE, outline=BLACK, details=details))
    for name, angle in (("ew-resize", 0), ("ns-resize", 90), ("nwse-resize", 45), ("nesw-resize", -45)):
        cursors.append(Cursor(name, (12, 12), [xform(double_arrow(17.0 if angle % 90 == 0 else 15.0), rotate=angle)]))
    cursors.append(Cursor("col-resize", (12, 12), bar_arrows()))
    cursors.append(Cursor("row-resize", (12, 12), [xform(s, rotate=90) for s in bar_arrows()]))
    for name, angle in (("e-resize", 0), ("se-resize", 45), ("s-resize", 90), ("sw-resize", 135),
                        ("w-resize", 180), ("nw-resize", 225), ("n-resize", 270), ("ne-resize", 315)):
        cursors.append(Cursor(name, (12, 12), [xform(s, rotate=angle, scale=0.92 if angle % 90 else 1.0) for s in edge_arrow()]))
    four = [double_arrow(18.0, shaft=1.0, head=4.2, half=3.6), xform(double_arrow(18.0, shaft=1.0, head=4.2, half=3.6), rotate=90)]
    cursors.append(Cursor("move", (12, 12), four))
    cursors.append(Cursor("all-scroll", (12, 12), four + [circle(12, 12, 2.4)]))
    # one pixel wide at 100 %, its hotspot pixel the middle of the cross
    cursors.append(Cursor("crosshair", (12, 12), [rrect(3.5, 12.0, 21.5, 13.0, 0.1), rrect(12.0, 3.5, 13.0, 21.5, 0.1)]))
    cursors.append(Cursor("cell", (12, 12), [rrect(9.4, 4.0, 14.6, 20.0, 0.6), rrect(4.0, 9.4, 20.0, 14.6, 0.6)], body=WHITE, outline=BLACK))
    # the spinning wait sign, drawn still: a ring with one dark quarter
    wait = Cursor("wait", (12, 12), [circle(12, 12, 8.0)], body=WHITE, outline=BLACK)
    wait.extra = [f'  <path d="{d([("M", 12, 12)] + [c for c in arc(12, 12, 8.0, -90, 0)[1:]] + [("Z",)])}" fill="{BLACK}"/>',
                  f'  <path d="{d([("M", 12, 12)] + [c for c in arc(12, 12, 8.0, 90, 180)[1:]] + [("Z",)])}" fill="{BLACK}"/>',
                  f'  <path d="{d(circle(12, 12, 2.6))}" fill="{WHITE}" stroke="{BLACK}" stroke-width="1"/>']
    cursors.append(wait)
    # a magnifying glass, the hotspot in the middle of its lens
    for name, sign in (("zoom-in", "+"), ("zoom-out", "-")):
        z = Cursor(name, (10, 10), [circle(10, 10, 6.2), xform(capsule(14.0, 13.8, 21.5, 16.6), rotate=45, about=(14.6, 15.2))])
        lens = circle(10, 10, 4.6)
        z.extra = [f'  <path d="{d(lens)}" fill="{WHITE}"/>',
                   f'  <path d="{d(line((7.4, 10), (12.6, 10)))}" fill="none" stroke="{BLACK}" stroke-width="1.5" stroke-linecap="round"/>']
        if sign == "+":
            z.extra.append(f'  <path d="{d(line((10, 7.4), (10, 12.6)))}" fill="none" stroke="{BLACK}" stroke-width="1.5" stroke-linecap="round"/>')
        cursors.append(z)
    for name, kind in (("copy", "copy"), ("alias", "alias"), ("not-allowed", "not-allowed"), ("no-drop", "not-allowed"),
                       ("help", "help"), ("context-menu", "context-menu"), ("progress", "progress")):
        cursors.append(arrow_with(name, kind))
    return cursors


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    cursors = build()
    for c in cursors:
        (out / f"{c.name}.svg").write_text(c.svg())
    if len(sys.argv) > 2:
        rel = sys.argv[3] if len(sys.argv) > 3 else "../cursors"
        lines = ["// Generated by integrations/siliconforest/desktop/cursors/make_cursors.py (DESK7); do not edit.",
                 "/// Every shape of the SiliconForest pointer set: its name (the wp_cursor_shape_v1 / CSS name) and its SVG.",
                 "pub(crate) const SET: &[(&str, &str)] = &["]
        lines += [f'    ("{c.name}", include_str!("{rel}/{c.name}.svg")),' for c in cursors]
        lines.append("];")
        Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
    print(f"{len(cursors)} cursors: {' '.join(c.name for c in cursors)}")


if __name__ == "__main__":
    main()
