#!/usr/bin/env python3
"""vscheck.py <run-dir>...: stripcheck over the page area only (below the content top, which differs with bars)."""
import sys, json, collections
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/sf-term12/tools')
from stripcheck import frames, analyse
for d in sys.argv[1:]:
    n = moved = 0; last = None; bad = []; top = None
    for fn, ms, h, px in frames(d + '/strip.bin'):
        if len(px) < 32 * h * 4: break
        if top is None:
            # the content's top: the first row below 100 where the page (W) starts after the chrome
            line0, runs0, f0 = analyse(fn, h, px, top=0)
            top = 128
            for c, a, b in runs0:
                if a > 100 and c in 'WRBG': top = a + 3; break
        n += 1
        line, runs, find = analyse(fn, h, px, top=top)
        sig = ''.join(line[top::20])
        if last is not None and sig != last: moved += 1
        last = sig
        f = [x for x in find if x[1] > top + 2 and not (x[0] == 'border-thick' and int(x[2].split('x')[1]) > 40) and x[0] != 'rowsplit']
        if f: bad.append((fn, ms, f[:3]))
    kinds = collections.Counter(k[0] for b in bad for k in b[2])
    print(d.split('/')[-1], 'content top', top, 'frames', n, 'moved', moved, 'frames with findings', len(bad), dict(kinds))
    for b in bad[:5]: print('   ', b)
