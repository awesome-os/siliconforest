#!/usr/bin/env python3
"""TERM12 presented-frame pacing ON the M1 (inside the workstation's gui.lock hold): own Firefox per run (fresh own
profile, -no-remote, crash reporter off), a page that animates / scrolls (wheel notches every 33 ms into MY window on
the virtual seat) / plays 1080p60 video; the compositor's WMPACING lines for my window (shown/s per 10 s).
pace12.py <name> <prefix|-> <anim|scroll|video> <runs> [envjson]"""
import json, re, subprocess, sys, threading, time, io, contextlib
sys.path.insert(0, '/private/var/db/sft12/tools')
import t12_m1 as M
from pop12lib import req, wins
name, prefix, case, runs = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
XENV = dict(kv.split('=', 1) for kv in sys.argv[5].split(',') if kv) if len(sys.argv) > 5 else {}
WARM = XENV.pop('WARM', '1') == '1'; SETTLE = float(XENV.pop('SETTLE', 4)); MEASURE = float(XENV.pop('MEASURE', 22))
PAGE = {'anim': 'anim.html', 'scroll': 'scroll12.html', 'video': 'video12.html'}[case]
def analyse(log, win):
    """per-frame trace of the last 15 s: display-link ticks, commits, flips (sequence gaps), flushes not presented"""
    txt = subprocess.run("grep -a -E '^(CVTICK|WSTIME)' %s | tail -12000" % log, shell=True, capture_output=True, text=True).stdout.splitlines()
    ev = []
    for l in txt:
        m = re.match(r'CVTICK at=([\d.]+) ran=([\d.]+)', l)
        if m:
            ev.append((float(m[2]), 'tick', float(m[1]))); continue
        m = re.match(r'WSTIME ([\d.]+) (\S+)(.*)', l)
        if m:
            ev.append((float(m[1]), m[2], m[3].strip()))
    if not ev:
        return dict(error='no trace lines')
    end = max(e[0] for e in ev); ev = [e for e in ev if e[0] >= end - 15000]
    out = dict(span_ms=round(end - min(e[0] for e in ev)) if ev else 0, counts={})
    for e in ev:
        out['counts'][e[1]] = out['counts'].get(e[1], 0) + 1
    seqs = [int(re.search(r'flip=(\d+)', e[2])[1]) for e in ev if e[1] == 'presented' and re.search(r'flip=(\d+)', e[2])]
    gaps = {}
    for a, b in zip(seqs, seqs[1:]):
        g = b - a; gaps[g] = gaps.get(g, 0) + 1
    out['flip_seq_gaps'] = dict(sorted(gaps.items())[:8])
    ticks = [e[0] for e in ev if e[1] == 'tick']
    ti = sorted(round(b - a, 1) for a, b in zip(ticks, ticks[1:]))
    if ti:
        out['tick_interval_ms'] = dict(p10=ti[len(ti) // 10], p50=ti[len(ti) // 2], p90=ti[len(ti) * 9 // 10], max=ti[-1])
    out['sample'] = ['%.1f %s %s' % e for e in ev[-260:]]
    return out


R = dict(name=name, prefix=prefix, case=case, env=XENV, runs=[], synthetic_input=case == 'scroll')
for i in range(runs):
    run = dict(i=i); R['runs'].append(run)
    pre = int(subprocess.run("wc -l < /var/log/desktop-session.log", shell=True, capture_output=True, text=True).stdout.strip() or 0)
    before = {w['id'] for w in wins()}
    env = dict({'MOZ_CRASHREPORTER_DISABLE': '1'}, **XENV)
    cvdir = None
    if 'SF_CV_TRACE' in env:
        cvdir = '/private/var/db/sft12/logs/cv-%s-%d' % (name, i)
        subprocess.run(['mkdir', '-p', cvdir]); env['SF_CV_TRACE'] = cvdir
    o = {'env': env, 'prefs': {'termsofuse.bypassNotification': True,
         'browser.tabs.inTitlebar': 0, 'media.autoplay.default': 0}}
    if prefix != '-':
        o['prepend_frameworks'] = o['prepend_libraries'] = prefix
    prof = '/private/var/db/sft12/p-%s-%d' % (name, i)
    subprocess.run(['rm', '-rf', prof])
    if WARM and subprocess.run(['test', '-d', '/private/var/db/sft12/warm']).returncode == 0:
        subprocess.run(['cp', '-R', '/private/var/db/sft12/warm', prof]); run['warm'] = True
    r = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'launch', prof, json.dumps(o),
                        'file:///private/var/db/sft12/www/' + PAGE], capture_output=True, text=True)
    run['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
    me = None; t0 = time.time()
    try:
        while time.time() - t0 < 45 and not me:
            ws = [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before and 't12' in w['title']]
            me = ws[0] if ws else None
            time.sleep(1)
        if not me:
            run['error'] = 'no window'; continue
        run['win'] = me['id']; time.sleep(SETTLE)
        if case == 'scroll':
            x, y = me['x'] + me['w'] // 2, me['y'] + me['h'] // 2
            at = 'abs %d %d' % (round(x * 65535 / 2559), round(y * 65535 / 1599))
            th = threading.Thread(target=req, args=('target %d\n%s\n' % (me['id'], at) + 'wheel -1\nwait 33\n' * int(MEASURE * 30 + 30), MEASURE + 40))
            th.start(); time.sleep(MEASURE); th.join()
        else:
            time.sleep(MEASURE)
        lines = subprocess.run("n=$(wc -l < /var/log/desktop-session.log); s=%d; [ $n -lt $s ] && s=1; tail -n +$s /var/log/desktop-session.log | grep -a 'WMPACING.*window=%d '" % (pre + 1, me['id']),
                               shell=True, capture_output=True, text=True).stdout.splitlines()
        rows = []
        for l in lines:
            m = re.search(r'over_ms=(\d+) .*?commits=(\d+) shown=(\d+) replaced_before_shown=(\d+)', l)
            if m:
                ms = int(m[1]); rows.append(dict(commits=int(m[2]) * 1000 / ms, shown=int(m[3]) * 1000 / ms, replaced=int(m[4]) * 1000 / ms))
        run['pacing'] = rows
        run['line'] = lines[-1][:300] if lines else None
        run['frames'] = subprocess.run("grep -a 'PD-CA-FRAMES' %s | tail -6" % run['launch']['log'], shell=True, capture_output=True, text=True).stdout.splitlines()
        run['crash'] = subprocess.run("ls -t /var/root/Library/Logs/DiagnosticReports/ 2>/dev/null | head -3; grep -a -c -E 'SIGSEGV|EXC_BAD|Assertion' %s" % run['launch']['log'], shell=True, capture_output=True, text=True).stdout
        if 'SF_WS_TIMING' in XENV or 'SF_CV_TRACE' in XENV:
            run['trace'] = analyse(run['launch']['log'], me['id'])
        if cvdir:
            ticks = {}
            for f in subprocess.run(['ls', cvdir], capture_output=True, text=True).stdout.split():
                ts = [float(m[1]) for m in re.finditer(r'CVTICK at=([\d.]+)', open(cvdir + '/' + f).read())]
                ts = ts[-900:]
                iv = sorted(round(b - a, 1) for a, b in zip(ts, ts[1:]))
                ticks[f] = dict(n=len(ts), per_s=round(len(ts) / ((ts[-1] - ts[0]) / 1000), 1) if len(ts) > 2 else None,
                                p10=iv[len(iv) // 10] if iv else None, p50=iv[len(iv) // 2] if iv else None,
                                p90=iv[len(iv) * 9 // 10] if iv else None, last=[round(b - a, 1) for a, b in zip(ts[-40:], ts[-39:])])
            run['cvticks'] = ticks
            subprocess.run(['rm', '-rf', cvdir])
        if i == 0:
            # which CoreVideo/AppKit/QuartzCore the parent really loaded
            rows = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'mine'], capture_output=True, text=True).stdout.splitlines()
            par = [int(l.split()[0]) for l in rows if 'MacOS/firefox' in l and 'contentproc' not in l]
            if par:
                f = '/private/var/db/sft12/logs/img-%s.txt' % name
                subprocess.run(['/private/var/db/sffx/probes-v3/sf-sample', str(par[-1]), '.05', '1', '-file', f], capture_output=True)
                run['images'] = subprocess.run("sed -n '/^Binary Images:/,$p' %s | grep -E 'CoreVideo|AppKit|QuartzCore|WindowServer' | awk '{print $NF}'; rm -f %s" % (f, f),
                                               shell=True, capture_output=True, text=True).stdout
    finally:
        subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'quitall'], capture_output=True)
        time.sleep(2.5)
        subprocess.run(['rm', '-rf', prof])
        open('/private/var/db/sft12/logs/%s.json' % name, 'w').write(json.dumps(R, indent=1))
print(json.dumps([[round(p['shown'], 1) for p in r.get('pacing', [])] for r in R['runs']]))
