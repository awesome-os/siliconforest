#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET DNS 2: uncached names (random labels) through libc getaddrinfo vs raw UDP to the resolver, per kind.
import socket, struct, time, random, sys, os
tag = sys.argv[1] if len(sys.argv) > 1 else "x"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 20
srv = os.environ.get("RESOLVER") or [l.split()[1] for l in open("/etc/resolv.conf") if l.startswith("nameserver")][0]
KINDS = {"nx": "fx%s.example.com", "nx-wiki": "fx%s.wikipedia.org", "cname-s3": "fx%s.s3.amazonaws.com", "nipio": "10-1-1-%s.nip.io"}

def raw(name, qtype):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(3)
    tid = random.getrandbits(16)
    pkt = struct.pack(">HHHHHH", tid, 0x0100, 1, 0, 0, 0) + b"".join(bytes([len(x)]) + x.encode() for x in name.split(".")) + b"\0" + struct.pack(">HH", qtype, 1)
    t0 = time.perf_counter(); s.sendto(pkt, (srv, 53))
    try:
        while struct.unpack(">H", s.recv(4096)[:2])[0] != tid: pass
        return (time.perf_counter() - t0) * 1000
    except socket.timeout:
        return None
    finally:
        s.close()

def gai(name, fam=socket.AF_UNSPEC, flags=0):
    t0 = time.perf_counter()
    try:
        socket.getaddrinfo(name, 443, fam, socket.SOCK_STREAM, 0, flags)
    except socket.gaierror:
        pass
    return (time.perf_counter() - t0) * 1000

def st(v):
    v = sorted(x for x in v if x is not None); n = len(v)
    if not n: return "n=0"
    q = lambda p: v[min(n - 1, int(p * n))]
    return "n=%d p50=%6.1f p90=%6.1f max=%6.1f" % (n, q(.5), q(.9), v[-1])

for kind, pat in KINDS.items():
    r = {"rawA": [], "rawAAAA": [], "gaiUNSPEC": [], "gaiINET": [], "gaiADDRCONFIG": []}
    for i in range(N):
        lab = lambda: ("%d" % random.randint(2, 250)) if kind == "nipio" else "%08x" % random.getrandbits(32)
        r["rawA"].append(raw(pat % lab(), 1))
        r["rawAAAA"].append(raw(pat % lab(), 28))
        r["gaiUNSPEC"].append(gai(pat % lab()))
        r["gaiINET"].append(gai(pat % lab(), socket.AF_INET))
        r["gaiADDRCONFIG"].append(gai(pat % lab(), socket.AF_UNSPEC, socket.AI_ADDRCONFIG))
        time.sleep(0.05)
    for k, v in r.items():
        print("%s %-9s %-13s %s" % (tag, kind, k, st(v)), flush=True)
