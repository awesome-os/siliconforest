"""FXLEAD workstation driver: gui.lock holds, own Firefox launches, renderer captures, tile checker."""
import fcntl, gzip, io, json, os, subprocess, sys, time
from contextlib import contextmanager
from pathlib import Path

REPO = Path('/home/chrisk/src/darwin-lab')
LOG = REPO / 'runs/m1-coord/log.txt'
LOCK = REPO / 'runs/m1-coord/gui.lock'
CAPS = REPO / 'runs/sf-term12/caps'
SSH = ['ssh', '-o', 'UserKnownHostsFile=' + os.path.expanduser('~/.ssh/known_hosts_m1_siliconforest'),
       '-o', 'GlobalKnownHostsFile=/dev/null', '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlias=m1-siliconforest',
       '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', 'root@10.1.1.211']
PY = '/opt/homebrew/bin/python3'
M1 = PY + ' /private/var/db/sft12/tools/t12_m1.py'
INJ = '/private/var/run/gpu-desktop/inject'
P = [[230,25,75],[60,180,75],[255,225,25],[0,130,200],[245,130,48],[145,30,180],[70,240,240],[240,50,230],
     [210,245,60],[250,190,212],[0,128,128],[220,190,255],[170,110,40],[255,250,200],[128,0,0],[170,255,195],[0,0,128]]


def now():
    return time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())


def log(text):
    with LOG.open('a') as s:
        s.write('%s TERM12 %s\n' % (now(), text))


def sh(cmd, inp=None, timeout=120, check=False):
    r = subprocess.run(SSH + [cmd], input=inp, capture_output=True, text=True, timeout=timeout)
    if check and r.returncode:
        raise RuntimeError('%s -> %d %s %s' % (cmd, r.returncode, r.stdout, r.stderr))
    return r


class Overrun(Exception):
    pass


@contextmanager
def hold(minutes, what, wait_s=600):
    """gui.lock (waits at most wait_s), announced in log.txt; a watchdog ends the hold at 2x minutes."""
    import signal
    def late(*_):
        raise TimeoutError('gui.lock not free after %ds' % wait_s)
    f = LOCK.open('a')
    old0 = signal.signal(signal.SIGALRM, late); signal.alarm(wait_s)
    try:
        fcntl.flock(f, fcntl.LOCK_EX)   # blocking: a waiter in the queue like the others (flock -w)
    except TimeoutError:
        f.close(); raise
    finally:
        signal.alarm(0); signal.signal(signal.SIGALRM, old0)
    cap = int(max(1, minutes) * 120)

    def over(*_):
        raise Overrun('hold cap %ds reached' % cap)
    old = signal.signal(signal.SIGALRM, over); signal.alarm(cap)
    try:
        log('gui.lock: %s (own Firefox -no-remote, own profile under /private/var/db/sft12, synthetic input on the '
            'virtual seat into my own window only), ~%d min, cap %d s' % (what, minutes, cap))
        yield
    finally:
        signal.alarm(0); signal.signal(signal.SIGALRM, old)
        log('gui.lock released')
        fcntl.flock(f, fcntl.LOCK_UN); f.close()


def inj(lines, timeout=30):
    r = sh(M1 + ' req %d' % timeout, inp=lines, timeout=timeout + 30)
    if r.returncode:
        raise RuntimeError('inject failed: ' + r.stdout + r.stderr)
    return r.stdout.strip()


def wins():
    import re
    name = 't12-l'
    inj('list ' + name)
    text = sh('cat %s/%s.txt; rm -f %s/%s.txt' % (INJ, name, INJ, name)).stdout
    rx = re.compile(r'^window (\d+) app_id="([^"]*)" title="([^"]*)" mapped=(\w+) .*?content=Rect \{ pos: Point \{ '
                    r'x: (-?\d+), y: (-?\d+) \}, size: Size \{ w: (\d+), h: (\d+) \} \}')
    out = []
    for line in text.splitlines():
        m = rx.match(line)
        if m:
            out.append(dict(id=int(m[1]), app=m[2], title=m[3], mapped=m[4] == 'true', x=int(m[5]), y=int(m[6]),
                            w=int(m[7]), h=int(m[8]), line=line))
    return out


def capture(name):
    inj('capture ' + name)
    from PIL import Image
    r = subprocess.run(SSH + ['gzip -1 -c %s/%s.ppm && rm -f %s/%s.ppm' % (INJ, name, INJ, name)],
                       capture_output=True, timeout=120)
    if r.returncode:
        raise RuntimeError(r.stderr)
    im = Image.open(io.BytesIO(gzip.decompress(r.stdout)))
    CAPS.mkdir(parents=True, exist_ok=True)
    p = CAPS / (name + '.png'); im.save(p)
    im.resize((im.width // 2, im.height // 2), Image.LANCZOS).save(CAPS / (name + '-half.png'))
    return p


def find_marker(im):
    px = im.load(); w, h = im.size
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            if px[x, y] == (255, 0, 255):
                while x > 0 and px[x - 1, y] == (255, 0, 255): x -= 1
                while y > 0 and px[x, y - 1] == (255, 0, 255): y -= 1
                return x, y
    return None


def near(a, b, tol=12):
    return all(abs(int(a[i]) - int(b[i])) <= tol for i in range(3))


def check_tiles(path, dpr=1.5, win=None):
    """Compare every fully visible grid cell's top-left patch with the colour the page put there."""
    from PIL import Image
    im = Image.open(path).convert('RGB'); px = im.load()
    o = find_marker(im)
    if not o:
        return dict(error='marker not found')
    ox, oy = o
    bits = ''.join('1' if sum(px[int(ox + (i * 8 + 4) * dpr), int(oy + 28 * dpr)]) < 200 else '0' for i in range(16))
    scroll = int(bits, 2)
    # viewport bottom: the banner (160,16,16) starts there + 60 CSS px
    bx = int(ox + 300 * dpr); y = int(oy + 40 * dpr); bottom = None
    while y < im.height - 1:
        if near(px[bx, y], (160, 16, 16), 20):
            bottom = y; break
        y += 1
    right = (win['x'] + win['w']) if win else im.width
    res = dict(origin=o, scrollY=scroll, banner_top=bottom, bad=[], good=0)
    for r in range(400):
        cy = r * 120 - scroll
        y0, y1 = oy + (cy + 3) * dpr, oy + (cy + 30) * dpr
        if y0 < oy + 34 * dpr or bottom is None or y1 >= bottom - 2:
            continue
        for c in range(10):
            x0 = ox + (c * 128 + 3) * dpr
            if x0 + 6 * dpr >= right:
                continue
            exp = P[(r * 5 + c) % 17]
            samples = [px[int(x0 + dx), int(y0 + dy)] for dx in (0, 4, 8) for dy in (0, 6, 12, 20)]
            ok = sum(near(s, exp) for s in samples)
            if ok >= len(samples) - 1:
                res['good'] += 1
            else:
                res['bad'].append(dict(r=r, c=c, x=int(x0 - ox), y=int(y0 - oy), got=samples[0], exp=exp))
    # banner: every 40 device px across the viewport at mid height
    if bottom is not None:
        bad = [x for x in range(ox + 4, right - 4, 40) if not near(px[x, bottom + int(5 * dpr)], (160, 16, 16), 20)]
        res['banner_bad_x'] = [x - ox for x in bad]
    return res


if __name__ == '__main__':
    print(json.dumps(check_tiles(sys.argv[1]), indent=1))


def mine_windows():
    return [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and
            ('fxlead tiles' in w['title'] or 'tiles-done' in w['title'])]


def wake_capture(win, name):
    inj('target %d\nkey e1 down\nkey e1 up\nwait 400\ncapture %s' % (win, name))
    from PIL import Image
    r = subprocess.run(SSH + ['gzip -1 -c %s/%s.ppm && rm -f %s/%s.ppm' % (INJ, name, INJ, name)],
                       capture_output=True, timeout=120)
    if r.returncode:
        raise RuntimeError(r.stderr)
    im = Image.open(io.BytesIO(gzip.decompress(r.stdout)))
    p = CAPS / (name + '.png'); im.save(p)
    im.resize((im.width // 2, im.height // 2), Image.LANCZOS).save(CAPS / (name + '-half.png'))
    return p


SAMPLE = r'''for p in $(/opt/homebrew/bin/python3 /private/var/db/sft12/tools/t12_m1.py mine | awk '{print $1}'); do
 c=$(ps -p $p -o command=); case "$c" in *MacOS/firefox\ *|*GPU\ Helper*) /private/var/db/sffx/probes-v3/sf-sample $p .05 10 -file /private/var/db/sft12/logs/img-$p.txt >/dev/null 2>&1;
 echo "== $p ${c:0:60}"; sed -n '/^Binary Images:/,$p' /private/var/db/sft12/logs/img-$p.txt | grep -E ' (AppKit|QuartzCore|QuartzCore\.pd|libWindowServer\.dylib|CoreVideo|IOSurface|OpenGL) ' | awk '{print $4, $NF}';; esac; done'''


def run(name, profile, overrides, query='', settle=(1.5, 5.0), sample=True):
    """One own Firefox on the tile page: launch, wait for the page's own scroll to end, captures, check, quit."""
    R = dict(name=name, overrides=overrides, t=now())
    url = 'http://127.0.0.1:8766/tiles.html' + query
    r = sh(M1 + " launch %s '%s' %s" % (profile, json.dumps(overrides), url), check=True)
    R['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
    t0 = time.time(); me = None
    try:
        while time.time() - t0 < 45:
            ws = mine_windows()
            if ws:
                me = ws[0]
                if 'tiles-done' in me['title']:
                    break
            time.sleep(1)
        R['t_done'] = round(time.time() - t0, 1); R['win'] = me and me['line'][:300]
        if not me:
            R['error'] = 'no window'; return R
        if sample:
            R['images'] = sh(SAMPLE, timeout=60).stdout
        for i, delay in enumerate(settle):
            time.sleep(delay if i == 0 else delay - settle[i - 1])
            cname = '%s-%d' % (name, i)
            p = wake_capture(me['id'], cname)
            R['check%d' % i] = check_tiles(p, win=me)
            R['check%d' % i]['bad_n'] = len(R['check%d' % i].get('bad', []))
    finally:
        R['quit'] = sh(M1 + ' quitall').stdout.strip()
        time.sleep(3)
        R['left'] = sh(M1 + ' mine').stdout.strip()
        (REPO / 'runs/sf-term12' / (name + '.json')).write_text(json.dumps(R, indent=1))
    return R


def summary(R):
    out = [R['name'], 't_done=%s' % R.get('t_done'), (R.get('win') or '')[:120]]
    for k in ('check0', 'check1'):
        c = R.get(k)
        if c:
            xs = sorted({b['x'] for b in c.get('bad', [])})
            out.append('%s scrollY=%s good=%s bad=%s banner_bad=%d bad_x=%s' % (k, c.get('scrollY'), c.get('good'),
                       c.get('bad_n'), len(c.get('banner_bad_x', [])), xs[:12]))
    return '\n'.join(out)
