#!/usr/bin/env python3
"""TERM12 warm profile template ON the M1 (inside the workstation's gui.lock hold): one own Firefox on a fresh own
profile (-no-remote, crash reporter off), my test pages for <secs>, then the desk's application Quit (natural exit);
the profile is kept as /private/var/db/sft12/warm (copied fresh into every later run). Background services stay as
Firefox has them (no quiet prefs). warm12.py <name> <secs>"""
import json, subprocess, sys, time
sys.path.insert(0, '/private/var/db/sft12/tools')
from pop12lib import req, wins
name, secs = sys.argv[1], float(sys.argv[2])
T = '/private/var/db/sft12/warm'
R = dict(name=name)
before = {w['id'] for w in wins()}
prof = '/private/var/db/sft12/p-' + name
subprocess.run(['rm', '-rf', prof, T])
o = {'env': {'MOZ_CRASHREPORTER_DISABLE': '1'}, 'prefs': {'termsofuse.bypassNotification': True, 'browser.tabs.inTitlebar': 0,
     'media.autoplay.default': 0}}
r = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'launch', prof, json.dumps(o),
                    'file:///private/var/db/sft12/www/anim.html'], capture_output=True, text=True)
R['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
try:
    me = None; t0 = time.time()
    while time.time() - t0 < 45 and not me:
        ws = [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
        me = ws[0] if ws else None
        time.sleep(1)
    R['win'] = me
    time.sleep(secs)
    rows = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'mine'], capture_output=True, text=True).stdout.splitlines()
    par = [int(l.split()[0]) for l in rows if 'MacOS/firefox' in l and 'plugin-container' not in l and 'GPU Helper' not in l]
    R['parent'] = par[-1] if par else None
    if par:
        t1 = time.time(); R['quit_req'] = req('app %d quit' % par[-1])
        while time.time() - t1 < 15 and subprocess.run(['kill', '-0', str(par[-1])], capture_output=True).returncode == 0:
            time.sleep(0.2)
        R['gone_after_s'] = round(time.time() - t1, 2)
finally:
    left = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'mine'], capture_output=True, text=True).stdout
    R['left'] = left
    subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'quitall'], capture_output=True)
    time.sleep(2)
    subprocess.run('mv %s %s && rm -rf %s/cache2 %s/startupCache; du -sh %s' % (prof, T, T, T, T), shell=True)
    R['du'] = subprocess.run(['du', '-sh', T], capture_output=True, text=True).stdout
    subprocess.run(['mkdir', '-p', '/private/var/db/sft12/logs/' + name])
    open('/private/var/db/sft12/logs/%s/run.json' % name, 'w').write(json.dumps(R, indent=1))
print(json.dumps({k: R.get(k) for k in ('gone_after_s', 'du', 'parent')}))
