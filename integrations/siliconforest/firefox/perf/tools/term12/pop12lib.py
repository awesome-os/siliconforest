"""shared helpers for TERM12's M1-side tests (own windows only, virtual seat)"""
import json, re, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, '/private/var/db/sft12/tools')
import t12_m1 as M
INJ = Path('/private/var/run/gpu-desktop/inject')
OUT = Path('/private/var/db/sft12/logs')
name = 'x'

class Marionette:
    def __init__(self, port):
        import socket
        self.s = socket.create_connection(('127.0.0.1', port), timeout=15); self.n = 0; self.recv()

    def recv(self):
        h = b''
        while True:
            c = self.s.recv(1)
            if not c: raise EOFError
            if c == b':': break
            h += c
        n = int(h); p = b''
        while len(p) < n:
            p += self.s.recv(n - len(p))
        return json.loads(p)

    def cmd(self, name, params=None):
        self.n += 1
        b = json.dumps([0, self.n, name, params or {}]).encode()
        self.s.sendall(str(len(b)).encode() + b':' + b)
        r = self.recv()
        if r[2]: raise RuntimeError(r[2])
        v = r[3]
        return v['value'] if isinstance(v, dict) and set(v) == {'value'} else v

    def js(self, src):
        return self.cmd('WebDriver:ExecuteScript', dict(script=src, args=[], newSandbox=False))


def req(lines, t=30):
    for i in range(50):
        rc = M.req(lines, t)
        if rc != 4 or not ((INJ / 'request').exists() or (INJ / 'request.part').exists()):
            return rc
        time.sleep(0.2)
    return rc


def wins():
    req('list t12l')
    p = INJ / 't12l.txt'; text = p.read_text(); p.unlink()
    rx = re.compile(r'^window (\d+) app_id="([^"]*)" title="([^"]*)" mapped=(\w+) .*?content=Rect \{ pos: Point \{ '
                    r'x: (-?\d+), y: (-?\d+) \}, size: Size \{ w: (\d+), h: (\d+) \} \}')
    out = []
    for line in text.splitlines():
        m = rx.match(line)
        if m:
            out.append(dict(id=int(m[1]), app=m[2], title=m[3], mapped=m[4] == 'true', x=int(m[5]), y=int(m[6]),
                            w=int(m[7]), h=int(m[8])))
    return out


def at(x, y):
    return 'abs %d %d' % (round(x * 65535 / 2559), round(y * 65535 / 1599))


def capture(tag):
    n = tag
    req('capture ' + n)
    subprocess.run('gzip -1 -c %s/%s.ppm > %s/%s.ppm.gz; rm -f %s/%s.ppm' % (INJ, n, OUT, n, INJ, n), shell=True)
    return n


