#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Most common caller chains of a leaf function in one thread: leafpaths.py <prof.gz> <pid> <thread> <leaf-substr> [depth]"""
import collections, gzip, json, sys
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/fxperf/tools')
import gprof
d = gprof.load(sys.argv[1]); pid = int(sys.argv[2]); tname = sys.argv[3]; leaf = sys.argv[4]; depth = int(sys.argv[5]) if len(sys.argv) > 5 else 8
cache = json.loads(gprof.CACHE.read_text())
for pr in gprof.walk(d):
    for th in pr['threads']:
        if th.get('pid') != pid or th['name'] != tname: continue
        t = gprof.Thread(th, pr.get('libs', []))
        ssch, sdata = gprof.table(th['samples'])
        c = collections.Counter(); n = 0
        missing = set()
        for row in sdata:
            if row[ssch['stack']] is None: continue
            names = [t.frame_name(f) for f in t.stack_frames(row[ssch['stack']])]
            nm = [gprof.name_of((x[0], x[1], x[2]) if isinstance(x, tuple) else x, cache) for x in names]
            if leaf not in nm[0]: continue
            n += 1
            c[' <- '.join(s.split('!')[-1][:70] for s in nm[1:depth + 1])] += 1
            for x in names[1:depth + 1]:
                if isinstance(x, tuple) and '%s+0x%x' % (x[1], x[2]) not in cache: missing.add((x[1], x[2]))
        if missing:
            libmap = {l['debugName'] or l['name']: l for p2 in gprof.walk(d) for l in p2.get('libs', [])}
            cache = gprof.symbolicate(missing, libmap)
            c = collections.Counter()
            for row in sdata:
                if row[ssch['stack']] is None: continue
                names = [t.frame_name(f) for f in t.stack_frames(row[ssch['stack']])]
                nm = [gprof.name_of((x[0], x[1], x[2]) if isinstance(x, tuple) else x, cache) for x in names]
                if leaf not in nm[0]: continue
                c[' <- '.join(s.split('!')[-1][:70] for s in nm[1:depth + 1])] += 1
        print('%s/%s: %d samples with leaf %s' % (pid, tname, n, leaf))
        for k, v in c.most_common(8):
            print('  %5d  %s' % (v, k))
