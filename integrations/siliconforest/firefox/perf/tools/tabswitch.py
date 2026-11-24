#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tab-switch latency from a hold-B run: key request (virtual seat) -> visibilitychange / first / second rAF of the
newly selected tab (Date.now on the M1, same clock as t_req).  tabswitch.py <B dir>"""
import json, re, statistics, sys
from pathlib import Path

d = Path(sys.argv[1]); run = json.loads((d / 'run.json').read_text())
res = []
for line in run.get('page_results', '').splitlines():
    name, _, body = line.partition(' ')
    try:
        res.append(json.loads(body))
    except Exception:
        pass
res.sort(key=lambda r: r['vis'])
out = []
for s in run.get('switch_plain', []):
    if not s['t_req']:
        continue
    want = str(s['k'] - 1) if s['k'] < 9 else '9'   # Command-1 = first tab (i=0), Command-9 = last tab (i=9)
    cand = [r for r in res if r['i'] == want and s['t_req'] <= r['vis'] <= s['t_req'] + 3000]
    if cand:
        r = cand[0]
        out.append(dict(k=s['k'], vis=r['vis'] - s['t_req'], raf1=r['raf1'] - s['t_req'], raf2=r['raf2'] - s['t_req'],
                        inj=(s['t_done'] or s['t_req']) - s['t_req']))
for o in out:
    print(o)
if out:
    for k in ('vis', 'raf1', 'raf2'):
        v = sorted(o[k] for o in out)
        print('%s: n=%d p50=%d p95=%d min=%d max=%d ms' % (k, len(v), statistics.median(v), v[min(len(v) - 1, int(.95 * len(v)))], v[0], v[-1]))
