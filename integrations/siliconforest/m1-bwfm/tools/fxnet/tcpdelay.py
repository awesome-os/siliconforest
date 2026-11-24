#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET delayed TCP echo, for an ssh -R tunnel (the lab's firewall takes no inbound UDP).
# serve PORT : one line "<id> <delay_ms>\n" in, the same line back after delay_ms
# probe PORT TAG [delays] [n] [gap_s] : connects to 127.0.0.1:PORT, prints rtt - delay per delay
import socket, sys, time, threading, random

def serve(port):
    ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", port)); ls.listen(4)
    def conn(c):
        c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        f = c.makefile("rb")
        for line in f:
            try:
                d = float(line.split()[1])
            except Exception:
                break
            if d > 0:
                time.sleep(d / 1000.0)
            c.sendall(line)
        c.close()
    while True:
        c, _ = ls.accept()
        threading.Thread(target=conn, args=(c,), daemon=True).start()

def probe(port, tag, delays, n, gap):
    s = socket.create_connection(("127.0.0.1", port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    f = s.makefile("rb")
    for d in delays:
        ex = []
        for i in range(n):
            time.sleep(gap * (0.5 + random.random()))
            t0 = time.perf_counter()
            s.sendall(("%d-%d %g\n" % (d, i, d)).encode())
            f.readline()
            ex.append((time.perf_counter() - t0) * 1000 - d)
        ex.sort(); k = len(ex); q = lambda p: ex[min(k - 1, int(p * k))]
        print("%s delay=%4dms n=%d extra: min=%.1f p50=%.1f p90=%.1f max=%.1f" % (tag, d, k, ex[0], q(.5), q(.9), ex[-1]), flush=True)

if __name__ == "__main__":
    if sys.argv[1] == "serve":
        serve(int(sys.argv[2]))
    else:
        delays = [int(x) for x in (sys.argv[4] if len(sys.argv) > 4 else "0,5,10,20,50,100,150,200,250,300,500").split(",")]
        probe(int(sys.argv[2]), sys.argv[3], delays, int(sys.argv[5]) if len(sys.argv) > 5 else 20,
              float(sys.argv[6]) if len(sys.argv) > 6 else 0.05)
