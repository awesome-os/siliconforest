#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""FXPERF hold B: tab switches (virtual-seat Command-1..9 between 10 static tabs) with and without the Gecko
profiler, 30 tabs idle + memory, then 3 warm start-ups (exec -> first presented frame) and one start-up profile."""
import json, re, sys, time
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/fxperf/tools')
from fxp import *

TAG = sys.argv[1] if len(sys.argv) > 1 else 'B1'
MODE = sys.argv[2] if len(sys.argv) > 2 else 'installed'
XENV = ''
if MODE == 'private-gpu':
    XENV = ',_LAUNCHER=%s/bin/firefox-perf,FXPERF_MANIFEST=%s/bin/release-private-gpu.json' % (B, B)
if len(sys.argv) > 3 and sys.argv[3]:
    XENV += ',' + sys.argv[3]
D = OUT / TAG; D.mkdir(parents=True, exist_ok=True)
RO = B + '/out/' + TAG
R = {'tag': TAG}
WWW = {'tab.html': '''<!doctype html><meta charset=utf-8><title>FXPERF tab</title>
<style>body{font:20px sans-serif;margin:40px}</style>
<h1 id=h>FXPERF tab</h1><p>Static page for tab-switch timing.</p>
<script>
const i=new URLSearchParams(location.search).get('i')||'0';document.title='FXPERF tab '+i;
document.body.style.background=['#fff','#eef','#efe','#fee','#ffe','#eff','#fef','#ddd','#cdf','#fdc'][i%10];
function post(n,o){fetch('/r/'+n,{method:'POST',body:JSON.stringify(o)}).catch(()=>{});}
document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='visible')return;const t0=Date.now(),p0=performance.now();
 requestAnimationFrame(()=>{const t1=Date.now();requestAnimationFrame(()=>{post('tab'+i,{i,vis:t0,raf1:t1,raf2:Date.now(),tori:performance.timeOrigin,p0});});});});
</script>''',
       'serve.py': '''import http.server, time
from pathlib import Path
W = Path('/private/var/db/sffxperf/www'); R = Path('/private/var/db/sffxperf/results'); R.mkdir(parents=True, exist_ok=True)
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(s, *a, **k): super().__init__(*a, directory=str(W), **k)
    def do_POST(s):
        n = int(s.headers.get('Content-Length', 0)); b = s.rfile.read(n)
        name = ''.join(c for c in s.path.rsplit('/', 1)[-1] if c.isalnum() or c in '-_') or 'r'
        (R / ('%s-%d.json' % (name, int(time.time() * 1000)))).write_bytes(b)
        s.send_response(204); s.end_headers()
    def log_message(s, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', 8766), H).serve_forever()
'''}


def step(name):
    R.setdefault('steps', []).append((name, round(time.time(), 3))); print(now(), name, flush=True)


def mn(*items, timeout=300):
    r = sh(MN + ' ' + ' '.join("'%s'" % i for i in items), timeout=timeout)
    print(r.stdout.strip()[-1500:], r.stderr.strip()[-800:], flush=True)
    return r.stdout


def mywin(before):
    for _ in range(60):
        ws, _t = wins()
        new = [w for w in ws if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
        if new:
            return new[0]
        time.sleep(0.4)
    return None


def wait_exit(secs=20):
    for _ in range(int(secs * 2)):
        if not sh(T + ' mine').stdout.strip():
            return ''
        time.sleep(0.5)
    left = sh(T + ' mine').stdout.strip()
    if left:
        print('left, TERM:', sh(T + ' quitall').stdout); time.sleep(2)
    return left


def switches(win, order, label):
    out = []
    for k in order:
        r = sh(T + ' req 10', inp='target %d\n' % win + keys('chord:cmd+%d' % k, 'wait:300'), timeout=40)
        m = re.search(r't_req=(\d+)', r.stdout); d = re.search(r't_done=(\d+)', r.stdout)
        out.append(dict(k=k, t_req=int(m[1]) if m else None, t_done=int(d[1]) if d else None, rc=r.returncode))
        time.sleep(0.9)
    R[label] = out
    return out


ENV = 'MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1,SFFX_FRAME_TRACE=1' + XENV
ORDER = [2, 3, 4, 5, 1, 6, 7, 8, 2, 3, 9, 4]
SWITCHONLY = 'SWITCHONLY' in sys.argv[3:]

with hold(9, 'FXPERF capture %s (%s): tab switches (Command-number, own window), 30 tabs idle, warm start-ups x3 + start-up profile' % (TAG, MODE)):
  try:
      for name, body in WWW.items():
          sh('mkdir -p %s/www && cat > %s/www/%s' % (B, B, name), inp=body, check=True)
      sh('mkdir -p %s; rm -rf %s/results; (nohup python3 %s/www/serve.py >/dev/null 2>&1 &) ; sleep 0.5' % (RO, B, B))
      R['desk_log_lines0'] = int(sh('wc -l < /var/log/desktop-session.log').stdout.split()[0])
      before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
      sh('rm -rf %s && mkdir -p %s && cat > %s/user.js' % (PROFILE, PROFILE, PROFILE), inp=USERJS, check=True)
      sh('/Applications/Firefox.app/Contents/MacOS/firefox-sf --sf-plan -no-remote --profile %s > %s/plan.json; '
         'shasum -a 256 /usr/local/lib/siliconforest/firefox/160-system1/release.json /Applications/Firefox.app/Contents/MacOS/firefox-sf > %s/runtime.sha; '
         'uname -v >> %s/runtime.sha' % (PROFILE, RO, RO, RO))
      step('launch tabs')
      R['launch1'] = sh(T + " launch %s '%s' --marionette 'http://127.0.0.1:8766/tab.html?i=0'" % (PROFILE, ENV), check=True).stdout.strip()
      if 'marionette up' not in mn('wait', 'tabs:9:http://127.0.0.1:8766/tab.html?i={i}', 'sleep:1'):
          sh(T + ' quitall'); sh("pkill -f 'sffxperf/www/serve.py'")
          raise SystemExit('no marionette')
      # the tabs got i=0..8; rename by order of the strip: first tab i=0 then 9 background tabs i=0..8
      me = mywin(before); R['win'] = me and me['line'][:300]; print('window', R['win'])
      time.sleep(4)
      comp0 = sh("ps -axo pid=,rss=,command= | grep chonkstep-darwin-desktop | grep -v grep | sort -k2 -n | tail -1 | awk '{print $1}'").stdout.strip()
      step('10 tabs idle stat 20s')
      print(sh(T + ' stat 20 %s/tabs10-stat.json %s' % (RO, comp0)).stdout)
      if not SWITCHONLY:
          step('switches with profiler')
          mn('prof:on:1')
          switches(me['id'], ORDER, 'switch_prof')
          mn('prof:dump:%s/switch.prof.json' % RO)
      step('switches without profiler')
      sh('rm -f %s/results/*' % B)
      switches(me['id'], ORDER, 'switch_plain')
      R['page_results'] = sh('cd %s/results && for f in $(ls); do echo "$f $(cat $f)"; done' % B).stdout
      json.dump(R, open(D / 'run.json', 'w'), indent=1)
      if SWITCHONLY:
          raise StopIteration('switch-only run')
      step('20 more tabs')
      mn('tabs:20:https://en.wikipedia.org/wiki/Special:Random', 'sleep:25')
      sh(T + ' req 10', inp='target %d\nkey e1 down\nwait 30\nkey e1 up' % me['id'], timeout=40); time.sleep(3)
      step('30 tabs idle stat')
      comp = sh("ps -axo pid=,rss=,command= | grep chonkstep-darwin-desktop | grep -v grep | sort -k2 -n | tail -1 | awk '{print $1}'").stdout.strip()
      print(sh(T + ' stat 30 %s/tabs30-stat.json %s' % (RO, comp)).stdout)
      mn('mem:%s/tabs30-mem.json.gz' % RO)
      par = sh(T + " mine | awk '$4==\"parent\"{print $1}'").stdout.split()
      if par:
          print(sh(T + ' regions %s %s/regions-parent-30.json' % (par[0], RO)).stdout[:2500])
      sh(T + ' req 10', inp='target %d\nkey e1 down\nwait 30\nkey e1 up' % me['id'], timeout=40)
      step('quit')
      mn('quit'); R['left1'] = wait_exit()
      pid = R['launch1'].split()[0]
      sh('cp $(ls -t /var/root/Library/Logs/SiliconForest/Firefox/*-%s.log | head -1) %s/firefox-tabs.log' % (pid, RO))
      # warm start-ups: no Marionette; exec -> first presented frame (FXWSPRESENTED), quit with Command-Q
      for n in range(4):
          prof = n == 3
          env = 'SFFX_FRAME_TRACE=1' + XENV
          if prof:
              env += (',MOZ_PROFILER_STARTUP=1,MOZ_PROFILER_STARTUP_INTERVAL=1,MOZ_PROFILER_STARTUP_FILTERS=*,'
                      'MOZ_PROFILER_STARTUP_FEATURES=js%stackwalk%cpuallthreads%processcpu%ipcmessages%markersallthreads,'
                      'MOZ_PROFILER_SHUTDOWN=' + RO + '/startup.prof.json')
          before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
          step('start %d' % n)
          lr = sh(T + " launch %s '%s' https://example.com/" % (PROFILE, env), check=True).stdout.strip()
          t0 = int(lr.split()[1])
          w = mywin(before)
          tmap = time.time()
          time.sleep(4 if not prof else 6)
          if w:
              sh(T + ' req 10', inp='target %d\n' % w['id'] + keys('chord:cmd+q', 'wait:300'), timeout=40)
          left = wait_exit(25)
          p = lr.split()[0]
          logf = sh('ls -t /var/root/Library/Logs/SiliconForest/Firefox/*-%s.log | head -1' % p).stdout.strip()
          first = sh("grep -m1 FXWSPRESENTED %s" % logf).stdout.strip()
          m = re.search(r'callback_ms=([\d.]+)', first); q = re.search(r'request_ms=([\d.]+)', first)
          R.setdefault('starts', []).append(dict(n=n, profiled=prof, t_exec_ms=t0,
              first_request_ms=round(float(q[1]) - t0, 1) if q else None,
              first_presented_ms=round(float(m[1]) - t0, 1) if m else None,
              window_seen_ms=round(tmap * 1000 - t0) if w else None, left=left))
          print(R['starts'][-1], flush=True)
          sh('cp %s %s/firefox-start%d.log' % (logf, RO, n))
      sh("pkill -f 'sffxperf/www/serve.py'")
      sh('tail -n +%d /var/log/desktop-session.log > %s/desktop.log' % (R['desk_log_lines0'], RO))
  except StopIteration:
      pass
  finally:
    json.dump(R, open(D / 'run.json', 'w'), indent=1)
    if sh(T + ' mine').stdout.strip():
        print('cleanup: own Firefox still running, Marionette quit then TERM')
        sh(MN + ' quit', timeout=40); time.sleep(3)
        if sh(T + ' mine').stdout.strip(): print(sh(T + ' quitall').stdout)
    sh("pkill -f 'sffxperf/www/serve.py'")
sh('cd %s && gzip -f -1 *.prof.json' % RO, timeout=300)
json.dump(R, open(D / 'run.json', 'w'), indent=1)
r = subprocess.run(SSH + ['tar -C %s -cf - %s' % (B + '/out', TAG)], capture_output=True, timeout=900)
subprocess.run(['tar', '-C', str(OUT), '-xf', '-'], input=r.stdout, check=True)
print('pulled', len(r.stdout))
