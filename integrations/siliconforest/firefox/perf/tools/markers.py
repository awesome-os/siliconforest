#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Marker rates per thread: markers.py <profile.json.gz> <thread-substr|*> [name-substr ...]"""
import collections, gzip, json, statistics, sys
d = json.loads(gzip.open(sys.argv[1]).read())
want_thread = sys.argv[2]; names = sys.argv[3:]
procs = [d] + [json.loads(p) if isinstance(p, str) else p for p in d.get('processes', [])]
t0, t1 = d['meta']['profilingStartTime'], d['meta']['profilingEndTime']
dur = (t1 - t0) / 1000
for pr in procs:
    base = pr['meta']['startTime'] - d['meta']['startTime']
    for th in pr['threads']:
        if want_thread != '*' and want_thread not in th['name'] and want_thread not in (th.get('processName') or ''):
            continue
        st = th.get('stringTable') or th.get('stringArray')
        sch = th['markers']['schema']
        c = collections.defaultdict(list)
        for m in th['markers']['data']:
            n = st[m[sch['name']]]
            if names and not any(x in n for x in names):
                continue
            s = m[sch['startTime']]; e = m[sch['endTime']]; ph = m[sch['phase']]
            if s is None: s = e
            if s is None or not (t0 <= s + base <= t1):
                continue
            c[(n, ph)].append((s, (e - s) if (e is not None and m[sch['startTime']] is not None) else None, m[sch['data']] if len(m) > sch['data'] else None))
        if not c:
            continue
        print('== %s/%s pid=%s' % (th.get('processName') or th.get('processType'), th['name'], th.get('pid')))
        for (n, ph), v in sorted(c.items(), key=lambda kv: -len(kv[1]))[:25]:
            durs = [x[1] for x in v if x[1] is not None]
            print('   %-45s phase=%d n=%5d (%.1f/s) dur_ms p50=%s sum=%s' % (n[:45], ph, len(v), len(v) / dur,
                  round(statistics.median(durs), 2) if durs else '-', round(sum(durs)) if durs else '-'))
