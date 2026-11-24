# SPDX-License-Identifier: MIT
"""FXPERF2 workstation helpers (FXPERF's fxp.py for prefix /private/var/db/sffxperf2): gui.lock holds, ssh to the M1, own-Firefox launch/quit, captures."""
import fcntl, gzip, io, os, re, signal, subprocess, sys, time
from contextlib import contextmanager
from pathlib import Path

REPO = Path('/home/chrisk/src/darwin-lab')
OUT = REPO / 'runs/fxperf2'
LOG = REPO / 'runs/m1-coord/log.txt'
LOCK = REPO / 'runs/m1-coord/gui.lock'
KEYS = REPO / 'integrations/siliconforest/terminal-app/tools/sf_keys.py'
SSH = ['ssh', '-o', 'UserKnownHostsFile=' + os.path.expanduser('~/.ssh/known_hosts_m1_siliconforest'),
       '-o', 'GlobalKnownHostsFile=/dev/null', '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlias=m1-siliconforest',
       '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', 'root@10.1.1.211']
B = '/private/var/db/sffxperf2'
T = 'python3 %s/tools/fxperf2_m1.py' % B
MN = 'python3 %s/tools/fxperf_mn.py' % B
INJ = '/private/var/run/gpu-desktop/inject'
PROFILE = B + '/p1'


def now():
    return time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())


def log(text):
    with LOG.open('a') as s:
        s.write('%s FXPERF2 %s\n' % (now(), text))


def sh(cmd, inp=None, timeout=300, check=False):
    r = subprocess.run(SSH + [cmd], input=inp, capture_output=True, text=True, timeout=timeout)
    if check and r.returncode:
        raise RuntimeError('%s -> %d %s %s' % (cmd, r.returncode, r.stdout[-500:], r.stderr[-500:]))
    return r


def shbg(cmd, logfile):
    """Run an M1 command in the background over its own ssh session (output to a local file)."""
    f = open(logfile, 'w')
    return subprocess.Popen(SSH + [cmd], stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)


@contextmanager
def hold(minutes, what, wait=1800):
    f = LOCK.open('a')
    def _alarm(*a): raise TimeoutError('gui.lock busy')
    old = signal.signal(signal.SIGALRM, _alarm); signal.alarm(wait)
    try:
        fcntl.flock(f, fcntl.LOCK_EX)
    finally:
        signal.alarm(0); signal.signal(signal.SIGALRM, old)
    try:
        log('gui.lock: %s (own Firefox -no-remote profile %s, virtual seat, own window only), ~%d min' % (what, PROFILE, minutes))
        yield
    finally:
        log('gui.lock released')
        fcntl.flock(f, fcntl.LOCK_UN)
        f.close()


def keys(*items):
    out = subprocess.run([sys.executable, '-B', str(KEYS)] + list(items), capture_output=True, text=True, check=True)
    return out.stdout.strip().replace(';', '\n')


def inj(lines, timeout=30):
    r = sh(T + ' req %d' % timeout, inp=lines, timeout=timeout + 30)
    if r.returncode:
        raise RuntimeError('inject failed rc=%d: %s %s' % (r.returncode, r.stdout, r.stderr))
    return r.stdout


WIN_RE = re.compile(r'^window (\d+) app_id="([^"]*)" title="([^"]*)" mapped=(\w+) .*?focused=(\w+) content=Rect \{ pos: Point \{ x: (-?\d+), y: (-?\d+) \}, size: Size \{ w: (\d+), h: (\d+) \} \}')


def wins():
    inj('list fxperf2-l')
    text = sh('cat %s/fxperf2-l.txt; rm -f %s/fxperf2-l.txt' % (INJ, INJ)).stdout
    out = []
    for line in text.splitlines():
        m = WIN_RE.match(line)
        if m:
            out.append(dict(id=int(m[1]), app=m[2], title=m[3], mapped=m[4] == 'true', focused=m[5] == 'true',
                            x=int(m[6]), y=int(m[7]), w=int(m[8]), h=int(m[9]), line=line))
    return out, text


def fetch(name, dest):
    r = subprocess.run(SSH + ['gzip -1 -c %s/%s.ppm && rm -f %s/%s.ppm' % (INJ, name, INJ, name)], capture_output=True, timeout=120)
    if r.returncode:
        raise RuntimeError(r.stderr)
    from PIL import Image
    im = Image.open(io.BytesIO(gzip.decompress(r.stdout)))
    dest.parent.mkdir(parents=True, exist_ok=True)
    im.save(dest)
    return dest


def pull(remote, local):
    local = Path(local); local.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(SSH + ['cat %s' % remote], capture_output=True, timeout=600)
    local.write_bytes(r.stdout)
    return len(r.stdout)


USERJS = '''user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("app.update.auto", false);
user_pref("webgl.disable-angle", true);
user_pref("browser.tabs.inTitlebar", 0);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.preonboarding.enabled", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("startup.homepage_welcome_url", "");
user_pref("startup.homepage_welcome_url.additional", "");
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.enabled", false);
user_pref("marionette.port", 28291);
user_pref("remote.prefs.recommended", false);
user_pref("browser.warnOnQuit", false);
user_pref("browser.tabs.warnOnClose", false);
'''
