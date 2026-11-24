#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""FXPERF hold C: one page idle WITHOUT Marionette (URL on the command line), presented frames, per-thread CPU,
Gecko profile from start-up (dumped at Command-Q), sample of the parent, region map.
holdC.py <tag> <url> [mode: installed-perf|private-gpu] [settle s] [extra env KEY=VAL,...]"""
import json, re, sys, time
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/fxperf/tools')
from fxp import *

TAG, URL = sys.argv[1], sys.argv[2]
MODE = sys.argv[3] if len(sys.argv) > 3 else 'installed-perf'
SETTLE = float(sys.argv[4]) if len(sys.argv) > 4 else 30
D = OUT / TAG; D.mkdir(parents=True, exist_ok=True)
RO = B + '/out/' + TAG
R = {'tag': TAG, 'url': URL, 'mode': MODE}
# no tracing by default: per-frame log lines cost a write() each (~5 ms on this file system) and the profiler's
# sampler costs ~15 % of a core per process; frames are counted by the compositor (WMPACING) instead
ENV = '_LAUNCHER=' + B + '/bin/firefox-perf'
EXTRA = sys.argv[5] if len(sys.argv) > 5 else ''
if 'PROF' in EXTRA.split(','):
    ENV += (',MOZ_PROFILER_STARTUP=1,MOZ_PROFILER_STARTUP_INTERVAL=4,MOZ_PROFILER_STARTUP_ENTRIES=16000000,'
            'MOZ_PROFILER_STARTUP_FILTERS=GeckoMain%Compositor%Renderer%WR%IPC%Vsync%Media%ImageBridge,'
            'MOZ_PROFILER_STARTUP_FEATURES=js%stackwalk%cpuallthreads%ipcmessages,MOZ_PROFILER_SHUTDOWN=' + RO + '/prof.json')
if 'TRACE' in EXTRA.split(','):
    ENV += ',SFFX_FRAME_TRACE=1,SFFX_GPU_PHASE_TRACE=1'
KEEP = 'KEEP' in EXTRA.split(',')
NOSAMPLE = 'NOSAMPLE' in EXTRA.split(',')
EXTRA = ','.join(x for x in EXTRA.split(',') if x and x not in ('PROF', 'TRACE', 'KEEP', 'NOSAMPLE'))
if MODE == 'private-gpu':
    ENV += ',FXPERF_MANIFEST=%s/bin/release-private-gpu.json' % B
if EXTRA:
    ENV += ',' + EXTRA
R['env'] = ENV


def step(name):
    R.setdefault('steps', []).append((name, round(time.time(), 3))); print(now(), name, flush=True)


with hold(4, 'FXPERF capture %s: %s idle without Marionette (%s), frames, CPU, start-up profile' % (TAG, URL, MODE)):
  try:
      R['desk_log_lines0'] = int(sh('wc -l < /var/log/desktop-session.log').stdout.split()[0])
      before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
      if KEEP:
          sh('rm -rf %s && mkdir -p %s %s' % (RO, RO, PROFILE), check=True)
      else:
          sh('rm -rf %s %s && mkdir -p %s %s && cat > %s/user.js' % (PROFILE, RO, RO, PROFILE, PROFILE), inp=USERJS, check=True)
      planenv = ' '.join(kv for kv in ENV.split(',') if kv.startswith(('FXPERF_', 'SFFXPERF_')))
      sh('env {pe} {b}/bin/firefox-perf --sf-plan -no-remote --profile {p} > {ro}/plan.json; shasum -a 256 '
         '/usr/local/lib/siliconforest/firefox/160-system1/release.json /Applications/Firefox.app/Contents/MacOS/firefox-sf '
         '{b}/bin/* /System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore.pd /usr/lib/libWindowServer.dylib '
         '/System/Library/Frameworks/AppKit.framework/Versions/A/AppKit > {ro}/runtime.sha; uname -v >> {ro}/runtime.sha'.format(
             pe=planenv, b=B, p=PROFILE, ro=RO))
      step('launch')
      R['launch'] = sh(T + " launch %s '%s' %s" % (PROFILE, ENV, URL), check=True).stdout.strip()
      me = None
      for _ in range(60):
          ws, _t = wins()
          new = [w for w in ws if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
          if new:
              me = new[0]; break
          time.sleep(0.5)
      if me is None:
          print('no window'); sh(T + ' quitall'); raise SystemExit('no window')
      R['win'] = me['line'][:300]; step('window')
      comp = sh("ps -axo pid=,rss=,command= | grep chonkstep-darwin-desktop | grep -v grep | sort -k2 -n | tail -1 | awk '{print $1}'").stdout.strip()
      time.sleep(max(0, SETTLE - 3))
      inj('target %d\nkey e1 down\nwait 30\nkey e1 up' % me['id'], timeout=15)
      time.sleep(3)
      step('idle stat 30s')
      print(sh(T + ' stat 30 %s/stat.json %s' % (RO, comp)).stdout)
      step('captures')
      try:
          inj('capture fxperf-%s-c1\nwait 1000\ncapture fxperf-%s-c2' % (TAG, TAG), timeout=40)
      except Exception as e:
          print('capture failed', e)
      step('sample')
      fam = sh(T + ' mine').stdout
      par = [l.split()[0] for l in fam.splitlines() if ' parent ' in l]
      if par and not NOSAMPLE:
          sh('sample %s 5 2 -file %s/sample-parent.txt >/dev/null 2>&1; python3 %s/tools/fxperf_m1.py regions %s %s/regions-parent.json > /dev/null' % (par[0], RO, B, par[0], RO), timeout=120)
      step('quit')
      try:
          inj('target %d\n' % me['id'] + keys('chord:cmd+q', 'wait:300'), timeout=20)
      except Exception as e:
          print('Command-Q not delivered (window gone?):', e); R['quit_error'] = repr(e)[:300]
      for _ in range(60):
          if not sh(T + ' mine').stdout.strip():
              break
          time.sleep(0.5)
      left = sh(T + ' mine').stdout.strip()
      if left:
          print('left after Command-Q, TERM:', sh(T + ' quitall').stdout); time.sleep(2)
      R['left'] = left
      step('collect')
      pid = R['launch'].split()[0]
      sh('cp $(ls -t /var/root/Library/Logs/SiliconForest/Firefox/*-%s.log | head -1) %s/firefox.log; tail -n +%d /var/log/desktop-session.log > %s/desktop.log'
         % (pid, RO, R['desk_log_lines0'], RO))
  finally:
    if sh(T + ' mine').stdout.strip():
        print('cleanup: own Firefox still running, TERM'); time.sleep(5)
        if sh(T + ' mine').stdout.strip(): print(sh(T + ' quitall').stdout)
for n in ('c1', 'c2'):
    try:
        fetch('fxperf-%s-%s' % (TAG, n), D / ('capture-%s.png' % n))
    except Exception as e:
        print('fetch failed', n, e)
sh('cd %s && [ -f prof.json ] && gzip -f -1 prof.json' % RO, timeout=300)
json.dump(R, open(D / 'run.json', 'w'), indent=1)
r = subprocess.run(SSH + ['tar -C %s -cf - %s' % (B + '/out', TAG)], capture_output=True, timeout=900)
subprocess.run(['tar', '-C', str(OUT), '-xf', '-'], input=r.stdout, check=True)
print('pulled', len(r.stdout))
