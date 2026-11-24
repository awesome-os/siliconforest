#!/usr/bin/env python3
"""TERM12 video-scroll artefact capture ON the M1 (inside the workstation's gui.lock hold): own Firefox (copy of my warm
profile, -no-remote, crash reporter off) on my page of looping muted videos (quad.mp4: red/green over blue/yellow,
4 px grey borders, white page), wheel notches into my own window every 33 ms; QuartzCore records a 32-px column of
every rendered frame (SF_CA_DUMP_STRIP) for the offline check. vid12.py <name> <prefix|-> [KEY=VAL,...]"""
import json, subprocess, sys, threading, time
sys.path.insert(0, '/private/var/db/sft12/tools')
from pop12lib import req, wins, at
name, prefix = sys.argv[1], sys.argv[2]
XENV = dict(kv.split('=', 1) for kv in sys.argv[3].split(',') if kv) if len(sys.argv) > 3 else {}
FRAMES = XENV.pop('FRAMES', '300'); WAIT = float(XENV.pop('WAIT', '1.5')); X = XENV.pop('STRIPX', '150'); DIR = XENV.pop('DIR', '-1')
OUT = '/private/var/db/sft12/logs/' + name
subprocess.run(['mkdir', '-p', OUT])
R = dict(name=name, prefix=prefix, env=XENV, synthetic_input=True)
before = {w['id'] for w in wins()}
env = dict({'MOZ_CRASHREPORTER_DISABLE': '1', 'SF_CA_DUMP_STRIP': '%s,32,%s,%s/strip.bin,%s' % (X, FRAMES, OUT, XENV.pop('AFTER', '2500'))}, **XENV)
o = {'env': env, 'prefs': {'termsofuse.bypassNotification': True, 'browser.tabs.inTitlebar': 0, 'media.autoplay.default': 0,
     'general.smoothScroll': True}}
if prefix != '-':
    o['prepend_frameworks'] = o['prepend_libraries'] = prefix
prof = '/private/var/db/sft12/p-' + name
subprocess.run(['rm', '-rf', prof]); subprocess.run(['cp', '-R', '/private/var/db/sft12/warm', prof])
r = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'launch', prof, json.dumps(o),
                    'file:///private/var/db/sft12/www/videos.html'], capture_output=True, text=True)
R['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
try:
    me = None; t0 = time.time()
    while time.time() - t0 < 45 and not me:
        ws = [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before and 't12' in w['title']]
        me = ws[0] if ws else None
        time.sleep(1)
    assert me, 'no window'
    R['win'] = me
    time.sleep(WAIT)
    pos = at(me['x'] + me['w'] // 2, me['y'] + me['h'] // 2)
    R['titles'] = []
    for k in range(8):
        req('target %d\n%s\n' % (me['id'], pos) + ('wheel %s\nwait 33\n' % DIR) * 30, 20)
        R['titles'] += [(round(time.time() - t0, 1), w['title']) for w in wins() if w['id'] == me['id']]
    time.sleep(2)
finally:
    subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'quitall'], capture_output=True)
    time.sleep(2.5)
    subprocess.run(['rm', '-rf', prof])
    if 'launch' in R:
        subprocess.run('grep -a -E "PD-CA|SIGSEGV" %s | tail -20 > %s/log.txt' % (R['launch']['log'], OUT), shell=True)
    R['strip_bytes'] = subprocess.run('ls -la %s/strip.bin 2>/dev/null' % OUT, shell=True, capture_output=True, text=True).stdout
    open(OUT + '/run.json', 'w').write(json.dumps(R, indent=1))
print(json.dumps(dict(win=R.get('win', {}).get('id'), strip=R.get('strip_bytes'))))
