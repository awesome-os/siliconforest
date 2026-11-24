#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET DNS A/B: getaddrinfo (AF_UNSPEC, like Firefox's native resolver) on fresh names of four kinds, and the same
# names again (mdnsd cache), N each. Prints per kind p50/p90/max and an overall line.
import socket, time, random, sys
tag = sys.argv[1] if len(sys.argv) > 1 else "x"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 10
KINDS = ["fx%s.example.com", "fx%s.wikipedia.org", "fx%s.s3.amazonaws.com", "fx%s.cloudfront.net"]
def gai(name):
    t0 = time.perf_counter()
    try:
        socket.getaddrinfo(name.encode(), 443, socket.AF_UNSPEC, socket.SOCK_STREAM)
    except socket.gaierror:
        pass
    return (time.perf_counter() - t0) * 1000
def st(v):
    v = sorted(v); n = len(v); q = lambda p: v[min(n - 1, int(p * n))]
    return "n=%d p50=%6.1f p90=%6.1f max=%7.1f" % (n, q(.5), q(.9), v[-1])
allf, allr = [], []
for pat in KINDS:
    names = [pat % ("%08x" % random.getrandbits(32)) for _ in range(N)]
    f = []
    for nm in names:
        f.append(gai(nm)); time.sleep(0.1)
    r = [gai(nm) for nm in names]
    allf += f; allr += r
    print("%s %-24s fresh %s | repeat %s" % (tag, pat, st(f), st(r)), flush=True)
print("%s ALL fresh %s | repeat %s" % (tag, st(allf), st(allr)))
