#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Table of hold-C runs: family CPU, key threads, wake-ups, compositor frames, footprint.  dsummary.py <dir>..."""
import json, re, statistics, subprocess, sys
from pathlib import Path


def thread_ms(st, role, name):
    tot = 0
    for p in st['procs']:
        if p['mine'] and p['role'] == role:
            for t in p['threads_top']:
                if t['name'] == name:
                    tot += t['cpu_ms']
    return tot


def frames(d):
    rows = []
    if not (d / 'desktop.log').exists():
        return (None, None)
    for line in open(d / 'desktop.log', errors='replace'):
        m = re.match(r'WMPACING over_ms=(\d+) window=\d+ app="org.mozilla.firefox" commits=(\d+) shown=(\d+)', line)
        if m:
            ms, c, s = map(int, m.groups()); rows.append((c * 1000 / ms, s * 1000 / ms))
    use = rows[2:] or rows
    return (round(statistics.median(r[0] for r in use), 1), round(statistics.median(r[1] for r in use), 1)) if use else (None, None)


rows = []
for a in sys.argv[1:]:
    d = Path(a)
    try:
        st = json.load(open(d / 'stat.json'))
    except Exception:
        continue
    secs = st['secs']
    pct = lambda ms: round(100 * ms / 1000 / secs, 1)
    c, s = frames(d)
    rows.append(dict(run=d.name, family=st['family_cpu_pct'], parent_main=pct(thread_ms(st, 'parent', 'MainThread')),
                     parent_comp=pct(thread_ms(st, 'parent', 'Compositor')), gpu_render=pct(thread_ms(st, 'gpu', 'Renderer')),
                     csw=st['family_csw_per_s'], intwk=st['family_int_wkups_per_s'], commits=c, shown=s,
                     footprint=st['family_footprint_mb']))
keys = ['run', 'family', 'parent_main', 'parent_comp', 'gpu_render', 'csw', 'intwk', 'commits', 'shown', 'footprint']
print(' '.join('%12s' % k for k in keys))
for r in rows:
    print(' '.join('%12s' % r[k] for k in keys))
