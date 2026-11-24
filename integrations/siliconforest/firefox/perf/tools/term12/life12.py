#!/usr/bin/env python3
"""TERM12 window/tab/panel lifecycle gate ON the M1 (inside the workstation's gui.lock hold). <rounds> rounds, each a
fresh own Firefox (copy of my warm profile, -no-remote, crash reporter off, Marionette on a private port for state
only); synthetic input on the virtual seat into my own windows only (windows that appeared after my launch):
  new window (Command-N) and its close (Command-W); new tab (Command-T) and its close (Command-W); the menu panel (a
  click on the toolbar's menu button, Escape); resize (window.resizeTo, twice); maximise and restore; full screen and
  back (Control-Command-F); Command-W on the last window (Firefox stays, windowless) and the desk's reopen; scroll a
  little; Quit (Command-Q, the process must end by itself).
After every step: the parent alive, no new crash report (/var/root/Library/Logs/DiagnosticReports), my windows.
life12.py <name> <prefix|-> <rounds> [KEY=VAL,...]"""
import json, os, re, subprocess, sys, time
sys.path.insert(0, '/private/var/db/sft12/tools')
import t12_m1 as M
from pop12lib import Marionette, req, wins, at, capture
name, prefix, rounds = sys.argv[1], sys.argv[2], int(sys.argv[3])
XENV = dict(kv.split('=', 1) for kv in sys.argv[4].split(',') if kv) if len(sys.argv) > 4 else {}
CAPS = XENV.pop('CAPS', '1') == '1'
OUT = '/private/var/db/sft12/logs/' + name
CR = '/var/root/Library/Logs/DiagnosticReports'
PORT = 28331
subprocess.run(['mkdir', '-p', OUT])
R = dict(name=name, prefix=prefix, env=XENV, rounds=[], synthetic_input=True, virtual_seat=True)


def cap(tag):
    n = capture('%s-%s' % (name, tag))
    subprocess.run('mv /private/var/db/sft12/logs/%s.ppm.gz %s/ 2>/dev/null' % (n, OUT), shell=True)


def crashes():
    return set(os.listdir(CR)) if os.path.isdir(CR) else set()


def save():
    open(OUT + '/run.json', 'w').write(json.dumps(R, indent=1))


def keys(win, *codes, mods=()):
    s = 'target %d\n' % win
    for m in mods: s += 'key %s down\nwait 20\n' % m
    for c in codes: s += 'key %s down\nwait 20\nkey %s up\nwait 30\n' % (c, c)
    for m in reversed(mods): s += 'key %s up\nwait 20\n' % m
    return req(s + 'wait 100')


CMD, CTRL = 'e3', 'e0'
K = dict(n='11', w='1a', t='17', q='14', f='09', esc='29', pgdn='4e')
STATE = r'''const ws=[...Services.wm.getEnumerator("navigator:browser")];const w=Services.wm.getMostRecentWindow("navigator:browser");
const p=w?w.document.getElementById("appMenu-popup"):null;
return {windows:ws.length,tabs:w?w.gBrowser.tabs.length:0,state:w?w.windowState:0,full:w?w.fullScreen:null,
panel:p?p.state:null,w:w?w.outerWidth:0,h:w?w.outerHeight:0,dpr:w?w.devicePixelRatio:0};'''
BTN = r'''const w=Services.wm.getMostRecentWindow("navigator:browser");const e=w.document.querySelector("#PanelUI-menu-button");
const r=e.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height,iw:w.innerWidth,ih:w.innerHeight,dpr:w.devicePixelRatio};'''

c0 = crashes()
for i in range(rounds):
    row = dict(i=i, steps=[]); R['rounds'].append(row)
    before = {w['id'] for w in wins()}
    o = {'env': dict({'MOZ_CRASHREPORTER_DISABLE': '1', 'MOZ_REMOTE_ALLOW_SYSTEM_ACCESS': '1'}, **XENV),
         'prefs': {'termsofuse.bypassNotification': True, 'marionette.port': PORT, 'browser.tabs.inTitlebar': 0,
                   'browser.warnOnQuit': False, 'browser.warnOnQuitShortcut': False, 'browser.tabs.warnOnClose': False,
                   'browser.sessionstore.resume_from_crash': False, 'media.autoplay.default': 0}}
    if prefix != '-':
        o['prepend_frameworks'] = o['prepend_libraries'] = prefix
    prof = '/private/var/db/sft12/p-%s-%d' % (name, i)
    subprocess.run(['rm', '-rf', prof])
    if os.path.isdir('/private/var/db/sft12/warm'):
        subprocess.run(['cp', '-R', '/private/var/db/sft12/warm', prof])
    r = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'launch', prof, json.dumps(o),
                        '--marionette', 'file:///private/var/db/sft12/www/anim.html'], capture_output=True, text=True)
    row['launch'] = json.loads(r.stdout.strip().splitlines()[-1])
    parent = None; mc = None

    def mine():
        return [w for w in wins() if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]

    def step(what, ok, **kw):
        alive = parent is not None and subprocess.run(['kill', '-0', str(parent)], capture_output=True).returncode == 0
        new = sorted(crashes() - c0)
        e = dict(what=what, ok=bool(ok), alive=alive, new_crash_reports=new, t=round(time.time(), 2)); e.update(kw)
        row['steps'].append(e); save()
        print(i, what, 'ok' if ok else 'FAIL', 'alive' if alive else 'DEAD', new or '', flush=True)
        if not alive:
            raise RuntimeError('parent gone after ' + what)

    try:
        me = None; t0 = time.time()
        while time.time() - t0 < 50 and not me:
            ws = mine(); me = ws[0] if ws else None; time.sleep(1)
        assert me, 'no window of mine'
        rows = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'mine'], capture_output=True, text=True).stdout.splitlines()
        par = [int(l.split()[0]) for l in rows if 'MacOS/firefox' in l and 'plugin-container' not in l and 'GPU Helper' not in l]
        parent = par[-1] if par else None; row['parent'] = parent
        time.sleep(3)
        for k in range(20):
            try:
                mc = Marionette(PORT); break
            except OSError:
                time.sleep(0.5)
        mc.cmd('WebDriver:NewSession', {}); mc.cmd('Marionette:SetContext', {'value': 'chrome'})
        st = lambda: mc.js(STATE)
        s0 = st(); step('launched', s0['windows'] == 1, state=s0, win=me)
        # new window and its close
        keys(me['id'], K['n'], mods=(CMD,)); time.sleep(2.5)
        s = st(); w2 = [w for w in mine() if w['id'] != me['id']]
        step('new window', s['windows'] == 2 and w2, state=s, wins=[w['id'] for w in mine()])
        if w2:
            keys(w2[0]['id'], K['w'], mods=(CMD,)); time.sleep(2)
        s = st(); step('close window', s['windows'] == 1, state=s)
        # new tab and its close
        keys(me['id'], K['t'], mods=(CMD,)); time.sleep(1.5)
        s = st(); step('new tab', s['tabs'] == 2, state=s)
        keys(me['id'], K['w'], mods=(CMD,)); time.sleep(1.5)
        s = st(); step('close tab', s['tabs'] == 1 and s['windows'] == 1, state=s)
        # the menu panel
        me = [w for w in mine() if w['id'] == me['id']][0]
        b = mc.js(BTN); oy = me['h'] - b['ih'] * b['dpr']; ox = me['w'] - b['iw'] * b['dpr']
        bx, by = me['x'] + ox + (b['x'] + b['w'] / 2) * b['dpr'], me['y'] + oy + (b['y'] + b['h'] / 2) * b['dpr']
        req('target %d\n%s\nwait 40\nbuttons 1\nwait 50\nbuttons 0\nwait 60' % (me['id'], at(bx, by)))
        t1 = time.time(); s = st()
        while s['panel'] != 'open' and time.time() - t1 < 2.5:
            time.sleep(0.1); s = st()
        time.sleep(0.4); s2 = st()
        if CAPS and i == 0: cap('panel')
        step('panel open', s2['panel'] == 'open', state=s2)
        keys(me['id'], K['esc']); time.sleep(0.8)
        s = st(); step('panel closed', s['panel'] in ('closed', None), state=s)
        # resize twice
        mc.js('const w=Services.wm.getMostRecentWindow("navigator:browser");w.resizeTo(1100,800);return 1;'); time.sleep(1.5)
        s = st(); step('resize smaller', abs(s['w'] - 1100) <= 40 and abs(s['h'] - 800) <= 60, state=s)
        if CAPS and i == 0: cap('resized')
        mc.js('const w=Services.wm.getMostRecentWindow("navigator:browser");w.resizeTo(1400,950);return 1;'); time.sleep(1.5)
        s = st(); step('resize larger', abs(s['w'] - 1400) <= 40 and abs(s['h'] - 950) <= 60, state=s)
        # maximise and restore
        def until(cond, limit=5.0):
            t3 = time.time(); s = st()
            while not cond(s) and time.time() - t3 < limit:
                time.sleep(0.25); s = st()
            s['waited_s'] = round(time.time() - t3, 2); return s
        mc.js('Services.wm.getMostRecentWindow("navigator:browser").maximize();return 1;')
        s = until(lambda s: s['state'] == 1); step('maximise', s['state'] == 1, state=s)
        time.sleep(0.5)
        mc.js('Services.wm.getMostRecentWindow("navigator:browser").restore();return 1;')
        s = until(lambda s: s['state'] == 3); step('restore', s['state'] == 3, state=s)
        # full screen and back (Control-Command-F, as on macOS)
        keys(me['id'], K['f'], mods=(CTRL, CMD)); time.sleep(2.5)
        s = st(); step('full screen', s['full'] is True or s['state'] == 4, state=s)
        if CAPS and i == 0: cap('full')
        keys(me['id'], K['f'], mods=(CTRL, CMD)); time.sleep(2.5)
        s = st(); step('full screen off', s['full'] is False and s['state'] != 4, state=s)
        # a little scrolling of the page (frames on the render thread meanwhile)
        cur = [w for w in mine() if w['id'] == me['id']]
        if cur:
            me = cur[0]
            req('target %d\n%s\n' % (me['id'], at(me['x'] + me['w'] // 2, me['y'] + me['h'] // 2)) + 'wheel -1\nwait 33\n' * 30)
        step('scrolled', True)
        # Command-W on the last window: Firefox stays without a window; the desk's reopen gives a new one
        keys(me['id'], K['w'], mods=(CMD,)); time.sleep(2.5)
        left = mine(); step('closed last window', not left, wins=[w['id'] for w in left])
        req('app %d reopen' % parent); time.sleep(3)
        for k in range(10):
            try:
                hs = mc.cmd('WebDriver:GetWindowHandles')
                if hs:
                    mc.cmd('WebDriver:SwitchToWindow', {'handle': hs[0]}); break
            except Exception:
                pass
            time.sleep(0.5)
        s = st(); again = mine(); step('reopened', s['windows'] == 1 and again, state=s, wins=[w['id'] for w in again])
        me = again[0] if again else me
        # Quit
        t2 = time.time(); keys(me['id'], K['q'], mods=(CMD,))
        while time.time() - t2 < 15 and subprocess.run(['kill', '-0', str(parent)], capture_output=True).returncode == 0:
            time.sleep(0.2)
        gone = subprocess.run(['kill', '-0', str(parent)], capture_output=True).returncode != 0
        row['quit_s'] = round(time.time() - t2, 2)
        new = sorted(crashes() - c0)
        row['steps'].append(dict(what='quit', ok=gone, gone_after_s=row['quit_s'], new_crash_reports=new)); save()
        print(i, 'quit', 'ok' if gone else 'FAIL (still alive)', row['quit_s'], new or '', flush=True)
    except Exception as e:
        row['error'] = str(e)[:400]; print(i, 'ERROR', row['error'], flush=True)
    finally:
        left = subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'mine'], capture_output=True, text=True).stdout
        row['family_left'] = len([l for l in left.splitlines() if l.strip()])
        subprocess.run(['/opt/homebrew/bin/python3', '/private/var/db/sft12/tools/t12_m1.py', 'quitall'], capture_output=True)
        time.sleep(2)
        log = row['launch']['log'] if 'launch' in row else None
        if log:
            row['log_faults'] = subprocess.run("grep -a -c -E 'SIGSEGV|EXC_BAD_ACCESS|Assertion failure|objc\\[.*\\]: .*(freed|deallocated)' %s" % log,
                                               shell=True, capture_output=True, text=True).stdout.strip()
            subprocess.run('cp %s %s/ff-%d.log' % (log, OUT, i), shell=True)
        subprocess.run(['rm', '-rf', prof])
        save()
fails = [(r['i'], s['what']) for r in R['rounds'] for s in r['steps'] if not s['ok']]
print(json.dumps(dict(rounds=len(R['rounds']), steps=sum(len(r['steps']) for r in R['rounds']), fails=fails,
                      errors=[r.get('error') for r in R['rounds'] if r.get('error')], new_crash_reports=sorted(crashes() - c0),
                      log_faults=[r.get('log_faults') for r in R['rounds']])))
