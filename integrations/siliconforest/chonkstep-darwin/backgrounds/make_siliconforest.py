#!/usr/bin/env python3
"""Draw the SiliconForest desktop background (2560x1600) as SVG; render with rsvg-convert.
Usage: make_siliconforest.py > siliconforest.svg && rsvg-convert siliconforest.svg -o siliconforest.png"""
import random
W, H = 2560, 1600
r = random.Random(8103)
out = []
out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
out.append('''<defs>
<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#050d0c"/><stop offset="0.45" stop-color="#0a2019"/>
<stop offset="0.8" stop-color="#16402f"/><stop offset="1" stop-color="#1d5239"/></linearGradient>
<radialGradient id="glow" cx="0.5" cy="0.62" r="0.55">
<stop offset="0" stop-color="#7fe0a8" stop-opacity="0.30"/><stop offset="0.5" stop-color="#3c9a72" stop-opacity="0.10"/>
<stop offset="1" stop-color="#000" stop-opacity="0"/></radialGradient>
<linearGradient id="mist" x1="0" y1="0" x2="0" y2="1">
<stop offset="0" stop-color="#9fe8c0" stop-opacity="0"/><stop offset="1" stop-color="#9fe8c0" stop-opacity="0.16"/></linearGradient>
</defs>''')
out.append(f'<rect width="{W}" height="{H}" fill="url(#sky)"/><rect width="{W}" height="{H}" fill="url(#glow)"/>')
# stars: a sparse wafer-grid of dots in the upper sky
for _ in range(170):
    x, y = r.randrange(0, W, 32) + 16, r.randrange(0, 640, 32) + 16
    out.append(f'<circle cx="{x}" cy="{y}" r="{r.choice([1.2,1.2,1.6,2.2])}" fill="#bff5d6" opacity="{r.uniform(0.10,0.45):.2f}"/>')

def tree(cx, base, h, color, trace):
    """A conifer of stacked tiers; its trunk continues below ground as a circuit trace."""
    w = h * 0.42
    tiers = 5
    p = []
    for i in range(tiers):
        top = base - h + (h * 0.86) * i / tiers
        bot = base - h + (h * 0.86) * (i + 1.25) / tiers
        half = w * (0.22 + 0.78 * (i + 1) / tiers) / 2
        p.append(f'<path d="M{cx:.0f} {top:.0f} L{cx+half:.0f} {bot:.0f} L{cx-half:.0f} {bot:.0f} Z" fill="{color}"/>')
    tw = max(3, h * 0.035)
    p.append(f'<rect x="{cx-tw/2:.1f}" y="{base-h*0.16:.0f}" width="{tw:.1f}" height="{h*0.16+2:.0f}" fill="{color}"/>')
    if trace:
        # root: straight down, a 45-degree jog, down again, ending in a via
        d1 = r.uniform(30, 90); j = r.choice([-1, 1]) * r.uniform(24, 70); d2 = r.uniform(30, 120)
        y1 = base + d1; y2 = y1 + abs(j); y3 = min(H - 24, y2 + d2)
        p.append(f'<path d="M{cx:.0f} {base:.0f} V{y1:.0f} L{cx+j:.0f} {y2:.0f} V{y3:.0f}" fill="none" stroke="{trace}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" opacity="0.75"/>')
        p.append(f'<circle cx="{cx+j:.0f}" cy="{y3:.0f}" r="7" fill="#0b1f18" stroke="{trace}" stroke-width="3" opacity="0.9"/>')
    return ''.join(p)

def ridge(base, amp, color, seed):
    rr = random.Random(seed); pts = [f'M0 {H}', f'L0 {base}']
    x = 0
    while x < W:
        x += rr.uniform(60, 160); pts.append(f'L{x:.0f} {base + rr.uniform(-amp, amp):.0f}')
    pts.append(f'L{W} {H} Z')
    return f'<path d="{" ".join(pts)}" fill="{color}"/>'

layers = [  # base y, tree height range, spacing, colour, trace colour
    (1130, (150, 260), 58, '#1f5a41', None),
    (1230, (220, 380), 86, '#16432f', None),
    (1340, (300, 520), 128, '#0d2c20', None),
    (1450, (380, 640), 190, '#07170f', '#58d99a'),
]
for n, (base, (lo, hi), step, color, trace) in enumerate(layers):
    out.append(ridge(base, 14, color, 100 + n))
    if trace:
        # ground plane: faint PCB grid over the foreground soil
        out.append(f'<rect x="0" y="1450" width="{W}" height="150" fill="#07170f"/>')
        for x in range(0, W, 64):
            out.append(f'<path d="M{x} 1470 V{H}" stroke="#58d99a" stroke-width="1" opacity="0.07"/>')
        for y in range(1480, H, 40):
            out.append(f'<path d="M0 {y} H{W}" stroke="#58d99a" stroke-width="1" opacity="0.07"/>')
    x = r.uniform(-step, 0)
    while x < W + step:
        h = r.uniform(lo, hi)
        # keep the middle of the front rows low so the name has a clearing behind it
        mid = abs(x - W / 2) / (W / 2)
        if n >= 2: h *= 0.55 + 0.45 * min(1, mid * 1.6)
        out.append(tree(x, base + r.uniform(-6, 10), h, color, trace))
        x += step * r.uniform(0.7, 1.35)
    out.append(f'<rect x="0" y="{base-260}" width="{W}" height="300" fill="url(#mist)"/>')
# the name
out.append(f'''<g font-family="IBM Plex Sans" font-size="212" text-anchor="middle" letter-spacing="-3">
<text x="{W/2}" y="640" fill="#04100c" opacity="0.55" transform="translate(0,8)"><tspan font-weight="300">Silicon</tspan><tspan font-weight="600">Forest</tspan></text>
<text x="{W/2}" y="640"><tspan font-weight="300" fill="#d9f7e6">Silicon</tspan><tspan font-weight="600" fill="#6fe3a6">Forest</tspan></text></g>
<path d="M{W/2-150} 700 H{W/2+150}" stroke="#6fe3a6" stroke-width="3" stroke-linecap="round" opacity="0.7"/>
<circle cx="{W/2-150}" cy="700" r="6" fill="#0a2019" stroke="#6fe3a6" stroke-width="3"/><circle cx="{W/2+150}" cy="700" r="6" fill="#0a2019" stroke="#6fe3a6" stroke-width="3"/>''')
out.append('</svg>')
print('\n'.join(out))
