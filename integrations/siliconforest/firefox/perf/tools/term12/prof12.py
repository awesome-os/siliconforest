#!/usr/bin/env python3
"""TERM12 per-frame profile ON the M1 (inside the workstation's gui.lock hold): own Firefox (fresh own profile,
-no-remote, crash reporter off) on a page that animates / scrolls (wheel notches into MY window) / plays video;
after a settle, sf-sample of the parent and the GPU process (all threads), the compositor's WMPACING for my window.
prof12.py <name> <prefix|-> <anim|scroll|video|url> <seconds> [KEY=VAL,KEY=VAL]"""
import json, re, subprocess, sys, threading, time
sys.path.insert(0, '/private/var/db/sft12/tools')
import t12_m1 as M
from pop12lib import req, wins
name, prefix, case, secs = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
XENV = dict(kv.split('=', 1) for kv in sys.argv[5].split(',') if kv) if len(sys.argv) > 5 else {}
SETTLE = float(XENV.pop('SETTLE', 6)); WARM = XENV.pop('WARM', '1') == '1'
PAGES = {'anim': 'anim.html', 'scroll': 'scroll12.html', 'video': 'video12.html'}
URL = 'file:///private/var/db/sft12/www/' + PAGES[case] if case in PAGES else case
OUT = '/private/var/db/sft12/logs/' + name
SAMPLER = '/private/var/db/sffx/probes-v3/sf-sample'
subprocess.run(['mkdir', '-p', OUT])
R = dict(name=name, prefix=prefix, case=case, env=XENV, synthetic_input=case == 'scroll')
pre = int(subprocess.run("wc -l < /var/log/desktop-session.log", shell=True, capture_output=True, text=True).stdout.strip() or 0)
before = {w['id'] for w in wins()}
env = dict({'MOZ_CRASHREPORTER_DISABLE': '1'}, **XENV)
o = {'env': env, 'prefs': {'termsofuse.bypassNotification': True, 'browser.tabs.inTitlebar': 0, 'media.autoplay.default': 0}}
if prefix != '-':
    o['prepend_frameworks'] = o['prepend_libraries'] = prefix
prof = '/private/var/db/sft12/p-' + name
subprocess.run(['rm', '-rf', prof])
if WARM and subprocess.run(['test', '-d', '/private/var/db/sft12/warm']).returncode == 0:
    subprocess.run(['cp', '-R', '/private/var/db/sft12/warm', prof]); R['warm'] = True
r = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'launch', prof, json.dumps(o), URL],
                   capture_output=True, text=True)
R['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
me = None; t0 = time.time()
try:
    while time.time() - t0 < 45 and not me:
        ws = [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
        me = ws[0] if ws else None
        time.sleep(1)
    if not me:
        R['error'] = 'no window'; raise SystemExit
    R['win'] = me; time.sleep(SETTLE)
    th = None
    if case == 'scroll':
        x, y = me['x'] + me['w'] // 2, me['y'] + me['h'] // 2
        at = 'abs %d %d' % (round(x * 65535 / 2559), round(y * 65535 / 1599))
        n = int((secs + 6) * 30)
        th = threading.Thread(target=req, args=('target %d\n%s\n' % (me['id'], at) + 'wheel -1\nwait 33\n' * n, secs + 40))
        th.start(); time.sleep(1.5)
    rows = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'mine'], capture_output=True, text=True).stdout.splitlines()
    procs = {}
    for l in rows:
        pid = int(l.split()[0])
        if 'GPU Helper' in l:
            procs['gpu'] = pid
        elif 'MacOS/firefox' in l and 'contentproc' not in l:
            procs['parent'] = pid
    R['procs'] = procs; R['ps'] = rows
    ss = [subprocess.Popen([SAMPLER, str(pid), str(secs), '1', '-file', '%s/sample-%s.txt' % (OUT, k)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) for k, pid in procs.items()]
    for s in ss:
        s.wait()
    if th:
        th.join()
    time.sleep(1)
    lines = subprocess.run("n=$(wc -l < /var/log/desktop-session.log); s=%d; [ $n -lt $s ] && s=1; tail -n +$s /var/log/desktop-session.log | grep -a 'WMPACING.*window=%d '" % (pre + 1, me['id']),
                           shell=True, capture_output=True, text=True).stdout.splitlines()
    R['pacing'] = []
    for l in lines:
        m = re.search(r'over_ms=(\d+) .*?commits=(\d+) shown=(\d+) replaced_before_shown=(\d+)', l)
        if m:
            ms = int(m[1]); R['pacing'].append(dict(commits=round(int(m[2]) * 1000 / ms, 1), shown=round(int(m[3]) * 1000 / ms, 1),
                                                     replaced=round(int(m[4]) * 1000 / ms, 1)))
finally:
    subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'quitall'], capture_output=True)
    time.sleep(3)
    subprocess.run(['rm', '-rf', prof])
    if 'launch' in R:
        subprocess.run('cp %s %s/firefox.log' % (R['launch']['log'], OUT), shell=True)
    open(OUT + '/run.json', 'w').write(json.dumps(R, indent=1))
print(json.dumps(dict(pacing=[p['shown'] for p in R.get('pacing', [])], procs=R.get('procs'), error=R.get('error'))))
