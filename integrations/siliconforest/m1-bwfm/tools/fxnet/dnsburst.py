#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET DNS burst: back-to-back getaddrinfo (AF_UNSPEC) of fresh names under CNAME'd CDN domains, the pattern of a
# page with many sibling hosts. Per domain: p50/p90/max and how many took over 200 ms; address count of the last answer.
import socket, time, random, sys
tag = sys.argv[1] if len(sys.argv) > 1 else "x"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 30
for pat in ["fx%s.s3.amazonaws.com", "fx%s.cloudfront.net", "fx%s.wikipedia.org"]:
    v = []; last = 0
    for i in range(N):
        name = (pat % ("%08x" % random.getrandbits(32))).encode()
        t0 = time.perf_counter()
        try:
            last = len(socket.getaddrinfo(name, 443, socket.AF_UNSPEC, socket.SOCK_STREAM))
        except socket.gaierror:
            last = -1
        v.append((time.perf_counter() - t0) * 1000)
        time.sleep(0.02)
    s = sorted(v); q = lambda p: s[min(len(s) - 1, int(p * len(s)))]
    print("%s %-22s n=%d p50=%6.1f p90=%6.1f max=%7.1f over200=%d lastaddrs=%d" % (tag, pat, N, q(.5), q(.9), s[-1], sum(x > 200 for x in v), last), flush=True)
