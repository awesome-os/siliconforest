#!/usr/bin/env python3
"""TERM12 tab switch timing ON the M1 (FXPERF's method; inside the workstation's gui.lock hold): own Firefox (copy of my
warm profile, -no-remote, crash reporter off) with 10 static tabs from my own local server (127.0.0.1:8786), 12
Command-number switches into my own window (synthetic, virtual seat); each page posts its visibilitychange and the
first and second requestAnimationFrame after it (Date.now, the M1's clock, as the request time here).
tabs12.py <name> <prefix|-> [KEY=VAL,...]"""
import json, os, subprocess, sys, time
sys.path.insert(0, '/private/var/db/sft12/tools')
from pop12lib import req, wins
name, prefix = sys.argv[1], sys.argv[2]
XENV = dict(kv.split('=', 1) for kv in sys.argv[3].split(',') if kv) if len(sys.argv) > 3 else {}
OUT = '/private/var/db/sft12/logs/' + name; RES = OUT + '/r'; WWW = '/private/var/db/sft12/www'
subprocess.run(['mkdir', '-p', RES])
TAB = '''<!doctype html><meta charset=utf-8><title>t12 tab</title>
<style>body{font:20px sans-serif;margin:40px}</style><h1>t12 tab</h1><p>Static page for tab-switch timing.</p>
<script>
const i=new URLSearchParams(location.search).get('i')||'0';document.title='t12 tab '+i;
document.body.style.background=['#fff','#eef','#efe','#fee','#ffe','#eff','#fef','#ddd','#cdf','#fdc'][i%10];
function post(n,o){fetch('/r/'+n,{method:'POST',body:JSON.stringify(o)}).catch(()=>{});}
document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='visible')return;const t0=Date.now();
 requestAnimationFrame(()=>{const t1=Date.now();requestAnimationFrame(()=>{post('tab'+i,{i,vis:t0,raf1:t1,raf2:Date.now()});});});});
</script>'''
open(WWW + '/tab.html', 'w').write(TAB)
SERVE = '''import http.server, time, sys
from pathlib import Path
W = Path(sys.argv[1]); R = Path(sys.argv[2])
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(s, *a, **k): super().__init__(*a, directory=str(W), **k)
    def do_POST(s):
        n = int(s.headers.get('Content-Length', 0)); b = s.rfile.read(n)
        nm = ''.join(c for c in s.path.rsplit('/', 1)[-1] if c.isalnum()) or 'r'
        (R / ('%s-%d.json' % (nm, int(time.time() * 1000)))).write_bytes(b)
        s.send_response(204); s.end_headers()
    def log_message(s, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', 8786), H).serve_forever()
'''
open(OUT + '/serve.py', 'w').write(SERVE)
srv = subprocess.Popen(['/opt/homebrew/bin/python3', OUT + '/serve.py', WWW, RES], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
ORDER = [2, 3, 4, 5, 1, 6, 7, 8, 2, 3, 9, 4]
DIGIT = {1: '1e', 2: '1f', 3: '20', 4: '21', 5: '22', 6: '23', 7: '24', 8: '25', 9: '26'}
R = dict(name=name, prefix=prefix, env=XENV, synthetic_input=True, switches=[])
before = {w['id'] for w in wins()}
o = {'env': dict({'MOZ_CRASHREPORTER_DISABLE': '1'}, **XENV), 'prefs': {'termsofuse.bypassNotification': True, 'browser.tabs.inTitlebar': 0}}
if prefix != '-':
    o['prepend_frameworks'] = o['prepend_libraries'] = prefix
prof = '/private/var/db/sft12/p-' + name
subprocess.run(['rm', '-rf', prof]); subprocess.run(['cp', '-R', '/private/var/db/sft12/warm', prof])
try:
    time.sleep(0.5)
    urls = ['http://127.0.0.1:8786/tab.html?i=%d' % i for i in range(10)]
    r = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'launch', prof, json.dumps(o)] + urls,
                       capture_output=True, text=True)
    R['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
    me = None; t0 = time.time()
    while time.time() - t0 < 45 and not me:
        ws = [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before and 't12' in w['title']]
        me = ws[0] if ws else None
        time.sleep(1)
    assert me, 'no window'
    time.sleep(8)
    for k in ORDER:
        t_req = int(time.time() * 1000)
        rc = req('target %d\nkey e3 down\nwait 20\nkey %s down\nwait 20\nkey %s up\nwait 20\nkey e3 up\nwait 300' % (me['id'], DIGIT[k], DIGIT[k]), 10)
        R['switches'].append(dict(k=k, t_req=t_req, rc=rc))
        time.sleep(0.9)
    time.sleep(1)
    res = []
    for f in os.listdir(RES):
        try: res.append(json.load(open(RES + '/' + f)))
        except Exception: pass
    out = []
    for s in R['switches']:
        want = str(s['k'] - 1) if s['k'] < 9 else '9'
        c = [x for x in res if x['i'] == want and s['t_req'] <= x['vis'] <= s['t_req'] + 3000]
        if c:
            out.append(dict(k=s['k'], vis=c[0]['vis'] - s['t_req'], raf1=c[0]['raf1'] - s['t_req'], raf2=c[0]['raf2'] - s['t_req']))
    R['result'] = out
finally:
    subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'quitall'], capture_output=True)
    srv.terminate(); time.sleep(2.5); subprocess.run(['rm', '-rf', prof])
    open(OUT + '/run.json', 'w').write(json.dumps(R, indent=1))
r2 = sorted(x['raf2'] for x in R.get('result', []))
print(json.dumps(dict(n=len(r2), raf2_p50=r2[len(r2) // 2] if r2 else None, raf2=r2, vis=sorted(x['vis'] for x in R.get('result', [])))))
