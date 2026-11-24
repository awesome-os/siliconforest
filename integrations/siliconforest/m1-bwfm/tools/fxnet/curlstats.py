#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# summarise curlbench output: per phase deltas in ms (dns, tcp, tls, server/ttfb, transfer)
import sys, collections
rows = collections.defaultdict(list)
fails = 0
for l in sys.stdin:
    p = l.split()
    if len(p) < 10 or p[2] == "FAIL":
        fails += 'FAIL' in l; continue
    tag = p[0]
    nl, co, ap, st, to = map(float, p[2:7])
    rows[tag].append((nl*1e3, (co-nl)*1e3, (ap-co)*1e3 if ap else 0, (st-(ap or co))*1e3, (to-st)*1e3, to*1e3, p[9], p[-1]))
def s(v):
    v = sorted(v); n = len(v); q = lambda f: v[min(n-1, int(f*n))]
    return "p50=%6.1f p90=%6.1f max=%6.1f" % (q(.5), q(.9), v[-1])
for tag, r in rows.items():
    print("%s n=%d fails=%d" % (tag, len(r), fails))
    for i, name in enumerate(["dns", "tcp", "tls", "ttfb", "xfer", "total"]):
        print("  %-5s %s" % (name, s([x[i] for x in r])))
    v6 = sum(1 for x in r if ':' in x[6]); print("  ipv6 remote:", v6, "of", len(r))
