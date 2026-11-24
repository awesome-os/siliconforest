#!/usr/bin/env python3
"""sampsum.py <sample.txt> [thread-substring] [min]: per thread busy/wait, and for one thread the inclusive counts of
named frames (our libraries' symbols; XUL ??? collapsed), the top of stack (self) counts."""
import collections, re, sys
sys.path.insert(0, '/home/chrisk/src/darwin-lab/integrations/siliconforest/firefox/perf/tools')
from sampleparse import parse, WAIT
path = sys.argv[1]; want = sys.argv[2] if len(sys.argv) > 2 else 'MainThread'; mn = int(sys.argv[3]) if len(sys.argv) > 3 else 20
ths = parse(path)
def leaves(t):
    for n in t['nodes']:
        s = n['n'] - n['children']
        if s > 0: yield n, s
def chain(n):
    out = []
    while n is not None: out.append(n['fn']); n = n['parent']
    return out[::-1]
for t in ths:
    tot = t['nodes'][0]['n'] if t['nodes'] else 0
    busy = sum(s for n, s in leaves(t) if not WAIT.search(n['fn']))
    if busy > 0.02 * tot or want in t['name']:
        print('%-70s total %5d busy %5d (%.0f%%)' % (t['name'][:70], tot, busy, 100.0 * busy / max(tot, 1)))
for t in ths:
    if want not in t['name']: continue
    inc = collections.Counter(); self_ = collections.Counter()
    for n, s in leaves(t):
        c = chain(n); seen = set()
        for f in c:
            k = re.sub(r'\?\?\? \(in XUL\)|XRE_GetBootstrap \(in XUL\)|uprofiler_get \(in XUL\)', 'XUL', f)
            if k in seen: continue
            seen.add(k); inc[k] += s
        self_[re.sub(r'\?\?\? \(in XUL\).*', 'XUL', c[-1])] += s
    print('--- inclusive (%s)' % t['name'][:60])
    for k, v in inc.most_common(80):
        if v >= mn and 'XUL' != k: print('%6d %s' % (v, k[:150]))
    print('--- self')
    for k, v in self_.most_common(40):
        if v >= mn: print('%6d %s' % (v, k[:150]))
