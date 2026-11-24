#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET DNS 4: libinfo's own trace (RES_DEBUG) with timestamps, through a pty so it is line buffered.
import os, pty, sys, time, random, socket
pat = sys.argv[1] if len(sys.argv) > 1 else "fx%s.s3.amazonaws.com"
n = int(sys.argv[2]) if len(sys.argv) > 2 else 10
pid, fd = pty.fork()
if pid == 0:
    os.environ["RES_DEBUG"] = "1"
    for i in range(n):
        name = pat % ("%08x" % random.getrandbits(32))
        print("@@ start %s" % name, flush=True)
        t0 = time.perf_counter()
        try:
            r = len(socket.getaddrinfo(name.encode(), 443, socket.AF_UNSPEC, socket.SOCK_STREAM))
        except socket.gaierror as e:
            r = "err%d" % e.errno
        print("@@ end %s %.1f ms res=%s" % (name, (time.perf_counter() - t0) * 1000, r), flush=True)
        time.sleep(0.2)
    os._exit(0)
t0 = time.perf_counter(); buf = b""
while True:
    try:
        d = os.read(fd, 65536)
    except OSError:
        break
    if not d:
        break
    buf += d
    while b"\n" in buf:
        l, buf = buf.split(b"\n", 1)
        print("%8.1f %s" % ((time.perf_counter() - t0) * 1000, l.decode(errors="replace").rstrip()[:180]), flush=True)
os.waitpid(pid, 0)
