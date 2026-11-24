#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""FXPERF hold A: idle example.com + idle cnn.com in my own Firefox: per-thread CPU/wakeups, frame trace,
Gecko profiles (all processes), sample(1) stacks, renderer captures, memory reports + region maps."""
import json, sys, time
sys.path.insert(0, '/home/chrisk/src/darwin-lab/runs/fxperf/tools')
from fxp import *

TAG = sys.argv[1] if len(sys.argv) > 1 else 'A1'
MODE = sys.argv[2] if len(sys.argv) > 2 else 'installed'   # installed | private-gpu
WARM = float(sys.argv[3]) if len(sys.argv) > 3 else 8
D = OUT / TAG
D.mkdir(parents=True, exist_ok=True)
RO = B + '/out/' + TAG
R = {'tag': TAG}


def step(name):
    R.setdefault('steps', []).append((name, round(time.time(), 3)))
    print(now(), name, flush=True)


def mn(*items, timeout=300):
    r = sh(MN + ' ' + ' '.join("'%s'" % i for i in items), timeout=timeout)
    print(r.stdout.strip()[-1500:], r.stderr.strip()[-800:], flush=True)
    return r.stdout


ENV = 'MOZ_REMOTE_ALLOW_SYSTEM_ACCESS=1,SFFX_FRAME_TRACE=1,SFFX_GPU_PHASE_TRACE=1'
LAUNCHER = '/Applications/Firefox.app/Contents/MacOS/firefox-sf'
if MODE == 'private-gpu':
    LAUNCHER = B + '/bin/firefox-perf'
    ENV += ',_LAUNCHER=%s,FXPERF_MANIFEST=%s/bin/release-private-gpu.json,SFFXPERF_INSERT=%s/lib/libsffxperf-fontprobe.dylib' % (LAUNCHER, B, B)
elif MODE == 'installed-perf':   # the installed manifest (system UI), through my launcher copy so candidates can go first
    LAUNCHER = B + '/bin/firefox-perf'
    ENV += ',_LAUNCHER=%s' % LAUNCHER
if len(sys.argv) > 4 and sys.argv[4]:
    ENV += ',' + sys.argv[4]
PLANENV = ' '.join(kv for kv in ENV.split(',') if kv.startswith(('FXPERF_', 'SFFXPERF_')))

with hold(6, 'FXPERF capture %s (%s): idle example.com + idle cnn.com: Gecko profiles, sample, per-thread CPU, frame trace, memory maps' % (TAG, MODE)):
    st = sh('ls -d %s && pgrep -f chonkstep-darwin-desktop | head -3; wc -l < /var/log/desktop-session.log; '
            'ps -axo pid=,command= | grep "MacOS/firefox " | grep -v grep | wc -l' % INJ).stdout.split()
    print('state', st)
    R['desk_log_lines0'] = int(st[-2])
    before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
    sh('rm -rf %s && mkdir -p %s %s && cat > %s/user.js' % (PROFILE, RO, PROFILE, PROFILE), inp=USERJS, check=True)
    sh('env {pe} {la} --sf-plan -no-remote --profile {pr} > {ro}/plan.json; shasum -a 256 /usr/local/lib/siliconforest/firefox/160-system1/release.json /Applications/Firefox.app/Contents/MacOS/firefox-sf {b}/bin/* {b}/lib/* > {ro}/runtime.sha; uname -v >> {ro}/runtime.sha'.format(pe=PLANENV, la=LAUNCHER, pr=PROFILE, ro=RO, b=B))
    step('launch')
    r = sh(T + " launch %s '%s' --marionette https://example.com/" % (PROFILE, ENV), check=True)
    R['mode'] = MODE; R['env'] = ENV
    R['launch'] = r.stdout.strip(); print(R['launch'])
    if 'marionette up' not in mn('wait', 'startup:%s/startup.json' % RO):
        print('Firefox did not come up:', sh('head -20 $(ls -t /var/root/Library/Logs/SiliconForest/Firefox/*-%s.log | head -1)' % R['launch'].split()[0]).stdout)
        sh(T + ' quitall')
        raise SystemExit('no marionette')
    step('marionette')
    me = None
    for _ in range(40):
        ws, text = wins()
        new = [w for w in ws if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
        if new:
            me = new[0]; break
        time.sleep(0.5)
    R['win'] = me and me['line'][:300]; print('window', R['win'])
    comp = sh("ps -axo pid=,rss=,command= | grep chonkstep-darwin-desktop | grep -v grep | sort -k2 -n | tail -1 | awk '{print $1}'").stdout.strip()
    R['compositor_pid'] = comp
    def wake():
        try:
            inj('target %d\nkey e1 down\nwait 30\nkey e1 up' % me['id'], timeout=15)
        except Exception as e:
            print('wake failed', e)
    wake()
    time.sleep(WARM)
    step('example idle stat 30s')
    print(sh(T + ' stat 30 %s/example-stat.json %s' % (RO, comp)).stdout)
    step('example profile 15s')
    mn('prof:on:2', 'sleep:15', 'prof:dump:%s/example.prof.json' % RO)
    step('nav cnn')
    mn('nav:https://www.cnn.com/')
    time.sleep(17); wake(); time.sleep(3)
    step('cnn idle stat 30s')
    print(sh(T + ' stat 30 %s/cnn-stat.json %s' % (RO, comp)).stdout)
    step('cnn captures')
    wake(); time.sleep(1)
    try:
        inj('capture fxperf-%s-c1\nwait 1000\ncapture fxperf-%s-c2' % (TAG, TAG), timeout=40)
    except Exception as e:
        print('capture failed', e); R['capture_error'] = repr(e)
    step('cnn profile 15s')
    mn('prof:on:2', 'sleep:15', 'prof:dump:%s/cnn.prof.json' % RO)
    step('sample')
    fam = sh(T + ' mine').stdout
    print(fam)
    rows = [l.split(None, 4) for l in fam.splitlines() if l.strip()]
    stat = json.loads(sh('cat %s/cnn-stat.json' % RO).stdout or '{}')
    pick = {}
    for p in stat.get('procs', []):
        if p['mine'] and (p['role'] not in pick or p['cpu_pct'] > pick[p['role']]['cpu_pct']):
            pick[p['role']] = p
    targets = [(k, pick[k]['pid']) for k in ('parent', 'gpu', 'content') if k in pick]
    R['sampled'] = targets
    cmds = ' & '.join('sample %d 6 2 -file %s/sample-%s.txt >/dev/null 2>&1' % (pid, RO, k) for k, pid in targets)
    sh(cmds + ' & wait', timeout=120)
    step('memory')
    mn('mem:%s/cnn-mem.json.gz' % RO, 'proc:%s/proc.json' % RO)
    for k, pid in targets:
        print(sh(T + ' regions %d %s/regions-%s.json' % (pid, RO, k)).stdout[:3000])
    step('quit')
    mn('quit')
    for _ in range(30):
        if not sh(T + ' mine').stdout.strip():
            break
        time.sleep(0.5)
    left = sh(T + ' mine').stdout.strip()
    if left:
        print('left after quit, TERM:', sh(T + ' quitall').stdout); time.sleep(2)
    R['left'] = left
    step('collect')
    pid = R['launch'].split()[0]
    logf = sh('ls -t /var/root/Library/Logs/SiliconForest/Firefox/*-%s.log | head -1' % pid).stdout.strip()
    R['fxlog'] = logf
    sh('cp %s %s/firefox.log; tail -n +%d /var/log/desktop-session.log > %s/desktop.log' % (logf, RO, R['desk_log_lines0'], RO))
for n in ('c1', 'c2'):
    try:
        fetch('fxperf-%s-%s' % (TAG, n), D / ('capture-%s.png' % n))
    except Exception as e:
        print('fetch failed', n, e)
sh('cd %s && gzip -f -1 *.prof.json' % RO, timeout=300)
json.dump(R, open(D / 'run.json', 'w'), indent=1)
r = subprocess.run(SSH + ['tar -C %s -cf - %s' % (B + '/out', TAG)], capture_output=True, timeout=900)
subprocess.run(['tar', '-C', str(OUT), '-xf', '-'], input=r.stdout, check=True)
print('pulled', len(r.stdout))
