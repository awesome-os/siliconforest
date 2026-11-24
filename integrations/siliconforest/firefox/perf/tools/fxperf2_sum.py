#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summary of an FXPERF2 A/B tag: per variant the median and range over its runs of family CPU (% of a core), the
parent process and its main thread, the GPU and content processes, the compositor process, and from the
compositor's WMPACING lines commits/s, shown/s and replaced-unshown/s; from the parent's log (damage builds) the
damage and composed bytes per presented frame.  fxperf2_sum.py <runs/fxperf2/<tag>>"""
import json, re, statistics, sys
from pathlib import Path

d = Path(sys.argv[1])
runs = json.loads((d / 'ab.json').read_text())


def wm(lines):
    c = s = r = ms = 0
    for l in lines:
        m = re.search(r'over_ms=(\d+) .*?commits=(\d+) shown=(\d+) replaced_before_shown=(\d+)', l)
        if m:
            o, cc, ss, rr = map(int, m.groups())
            ms += o; c += cc; s += ss; r += rr
    return (c * 1000 / ms, s * 1000 / ms, r * 1000 / ms) if ms else (None, None, None)


def damage(run):
    p = d / run['variant'] / 'firefox.log'
    if not p.exists():
        return None, None
    rows = []
    for l in p.read_text(errors='replace').splitlines():
        m = re.match(r'FXWSPRESENT .*frames=(\d+) .*damage_bytes=(\d+) composed_bytes=(\d+)', l)
        if m:
            rows.append(tuple(map(int, m.groups())))
    if len(rows) < 2:
        return None, None
    (f0, d0, c0), (f1, d1, c1) = rows[-2], rows[-1]
    return (d1 - d0) / max(1, f1 - f0), (c1 - c0) / max(1, f1 - f0)


by = {}
for r in runs:
    if r.get('error'):
        print('ERROR', r['name'], r['i'], r['error']); continue
    c, s, rep = wm(r.get('wmpacing', []))
    dm, cm = damage(r)
    st = r['stat']
    row = dict(family=st['family_cpu_pct'], parent=r.get('parent_pct'), main=r.get('parent_main_ms', 0) / (st['secs'] * 10),
               gpu=r['roles'].get('gpu'), content=r['roles'].get('content'), comp=r.get('compositor_pct'),
               commits=c, shown=s, replaced=rep, dmg_mb_frame=dm / 1e6 if dm is not None else None,
               composed_mb_frame=cm / 1e6 if cm is not None else None, csw=st['family_csw_per_s'])
    by.setdefault(r['name'], []).append(row)
keys = ['family', 'parent', 'main', 'gpu', 'content', 'comp', 'commits', 'shown', 'replaced', 'dmg_mb_frame', 'composed_mb_frame', 'csw']
print('%-10s %s' % ('variant', ' '.join('%-17s' % k for k in keys)))
for name, rows in by.items():
    cells = []
    for k in keys:
        v = [x[k] for x in rows if x[k] is not None]
        cells.append('%-17s' % ('%.1f [%.1f-%.1f]' % (statistics.median(v), min(v), max(v)) if v else '-'))
    print('%-10s %s  (n=%d)' % (name, ' '.join(cells), len(rows)))
