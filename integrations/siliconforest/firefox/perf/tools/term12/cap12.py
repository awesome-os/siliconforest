#!/usr/bin/env python3
"""TERM12 capture ON the M1 (inside the workstation's gui.lock hold): own Firefox (copy of my warm profile, -no-remote,
crash reporter off) on <page> (my www), wait until its title says 'done' (or <secs>), one compositor capture of the
screen, quit. cap12.py <name> <prefix|-> <page> <secs> [KEY=VAL,...]"""
import json, subprocess, sys, time
sys.path.insert(0, '/private/var/db/sft12/tools')
from pop12lib import req, wins, capture
name, prefix, page, secs = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
XENV = dict(kv.split('=', 1) for kv in sys.argv[5].split(',') if kv) if len(sys.argv) > 5 else {}
OUT = '/private/var/db/sft12/logs/' + name
subprocess.run(['mkdir', '-p', OUT])
R = dict(name=name, prefix=prefix, page=page, env=XENV)
before = {w['id'] for w in wins()}
TB = int(XENV.pop('TITLEBAR', '0'))
o = {'env': dict({'MOZ_CRASHREPORTER_DISABLE': '1'}, **XENV), 'prefs': {'termsofuse.bypassNotification': True, 'browser.tabs.inTitlebar': TB}}
if prefix != '-':
    o['prepend_frameworks'] = o['prepend_libraries'] = prefix
prof = '/private/var/db/sft12/p-' + name
subprocess.run(['rm', '-rf', prof]); subprocess.run(['cp', '-R', '/private/var/db/sft12/warm', prof])
r = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'launch', prof, json.dumps(o),
                    'file:///private/var/db/sft12/www/' + page], capture_output=True, text=True)
R['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
try:
    me = None; t0 = time.time()
    while time.time() - t0 < 45:
        ws = [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before and 't12' in w['title']]
        me = ws[0] if ws else me
        if me and ('done' in me['title'] or time.time() - t0 > secs):
            break
        time.sleep(0.5)
    R['win'] = me; time.sleep(1)
    n = capture(name + '-cap')
    subprocess.run('mv /private/var/db/sft12/logs/%s.ppm.gz %s/' % (n, OUT), shell=True)
finally:
    subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'quitall'], capture_output=True)
    time.sleep(2.5); subprocess.run(['rm', '-rf', prof])
    open(OUT + '/run.json', 'w').write(json.dumps(R, indent=1))
print(json.dumps(dict(win=R.get('win'))))
