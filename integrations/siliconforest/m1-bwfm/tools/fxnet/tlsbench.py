#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET: TCP connect and TLS handshake times to real sites with the CA store loaded once, before timing, so only
# the network round trips and the handshake crypto are measured (curl/OpenSSL reload their CA files per process).
# argv: TAG ROUNDS; addresses are resolved once up front (DNS measured elsewhere), IPv4.
import socket, ssl, sys, time
TAG = sys.argv[1] if len(sys.argv) > 1 else "x"
R = int(sys.argv[2]) if len(sys.argv) > 2 else 3
HOSTS = ["www.wikipedia.org", "www.bbc.co.uk", "www.cnn.com", "www.nytimes.com", "github.com", "www.reddit.com",
         "www.mozilla.org", "news.ycombinator.com", "www.theguardian.com", "www.apple.com"]
ctx = ssl.create_default_context()
try:
    ctx.load_verify_locations("/etc/ssl/cert.pem")
except Exception:
    pass
ctx.set_alpn_protocols(["h2", "http/1.1"])
addr = {h: socket.getaddrinfo(h.encode(), 443, socket.AF_INET, socket.SOCK_STREAM)[0][4] for h in HOSTS}
tcp, tls = [], []
for r in range(R):
    for h in HOSTS:
        t0 = time.perf_counter()
        s = socket.create_connection(addr[h], timeout=10)
        t1 = time.perf_counter()
        ss = ctx.wrap_socket(s, server_hostname=h)
        t2 = time.perf_counter()
        ss.close()
        tcp.append((t1 - t0) * 1000); tls.append((t2 - t1) * 1000)
def st(v):
    v = sorted(v); n = len(v); q = lambda p: v[min(n - 1, int(p * n))]
    return "n=%d p50=%6.1f p90=%6.1f max=%6.1f" % (n, q(.5), q(.9), v[-1])
print(TAG, "tcp", st(tcp)); print(TAG, "tls", st(tls)); print(TAG, "tcp+tls", st([a + b for a, b in zip(tcp, tls)]))
