#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""FXPERF2 A/B hold: the same page in own Firefox instances with different private framework sets, interleaved,
inside ONE gui.lock hold. Per run: fresh copy of a warm profile, launch with the URL (no Marionette, no tracing),
wait for the window, settle, per-process/thread CPU over <stat> s (fxperf2_m1.py stat, compositor included),
the compositor's WMPACING lines for that window, the parent's loaded images (region map paths), quit.

  fxperf2_ab.py push [<result>:<dir> ...]    copy runs/fxperf2/build/result-<result> to the M1's <prefix>/<dir>
                                             (default true:cand false:base)
  fxperf2_ab.py warm <url> [secs]            make the warm profile template (one launch, 40 s), inside a hold
  fxperf2_ab.py run <tag> <url> <runs> <settle> <stat> <name>=<ENV,...> [<name>=<ENV,...> ...] [capture]
  fxperf2_ab.py grid <tag> <trials> <name>=<ENV,...> [...]  FXLEAD's self-scrolling tile page (served from the prefix's
                                             www on 127.0.0.1:8776), capture when its title says done, every cell checked
  fxperf2_ab.py pix <tag> <a>=<ENV> <b>=<ENV> [url ...]  the same static local pages (PIX_PAGES, tile page) in both
                                             variants, captures compared pixel for pixel over the window's content
  fxperf2_ab.py tabs <tag> <rounds> <name>=<ENV,...> [...]  tab switches (FXPERF's method): 10 static tabs given on the
                                             command line, 12 Command-number switches into the own window per round;
                                             each page posts visibilitychange / 1st / 2nd rAF times (Date.now, the M1's
                                             clock, as the inject request time)
ENV entries KEY=VAL (commas separate; '%' stands for a comma inside a value). 'capture' takes one capture of each
variant's window in its first run (compositor capture, for the visual check); 'sample' takes a 5 s sf-sample of
the parent after each run's stat (profiling run: its CPU numbers are not used); 'wheel' sends a wheel notch every
33 ms into the own window during the stat (DESK9's scroll case; use with /scroll9.html)."""
import json, re, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fxp2 import *

TEMPLATE = B + '/p1-warm'
# background services of a young profile (safe-browsing lists, plugin downloads, studies, add-on updates, telemetry,
# region and connectivity checks) restart in every copy of the template and fill cache2/IndexedDB on the slow disk:
# off for every variant alike (they are unrelated to drawing)
QUIET = USERJS + ''.join('user_pref("%s", %s);\n' % kv for kv in [
    ('browser.safebrowsing.malware.enabled', 'false'), ('browser.safebrowsing.phishing.enabled', 'false'),
    ('browser.safebrowsing.downloads.enabled', 'false'), ('browser.safebrowsing.blockedURIs.enabled', 'false'),
    ('media.gmp-manager.updateEnabled', 'false'), ('media.gmp-widevinecdm.enabled', 'false'),
    ('media.gmp-gmpopenh264.enabled', 'false'), ('extensions.update.enabled', 'false'),
    ('extensions.getAddons.cache.enabled', 'false'), ('app.normandy.enabled', 'false'),
    ('app.shield.optoutstudies.enabled', 'false'), ('datareporting.healthreport.uploadEnabled', 'false'),
    ('toolkit.telemetry.unified', 'false'), ('browser.region.update.enabled', 'false'),
    ('network.captive-portal-service.enabled', 'false'), ('network.connectivity-service.enabled', 'false'),
    ('browser.newtabpage.activity-stream.feeds.topsites', 'false'), ('browser.newtabpage.activity-stream.feeds.section.topstories', 'false')])


def compositor_pid():
    return sh("ps -axo pid=,rss=,command= | grep chonkstep-darwin-desktop | grep -v grep | sort -k2 -n | tail -1 | awk '{print $1}'").stdout.strip()


def own_window(before):
    for _ in range(60):
        ws, _t = wins()
        new = [w for w in ws if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
        if new:
            return new[0]
        time.sleep(0.5)
    return None


def quit_own(win):
    try:
        if win is not None:
            inj('target %d\n' % win['id'] + keys('chord:cmd+q', 'wait:300'), timeout=20)
    except Exception as e:
        print('Command-Q not delivered:', e)
    for _ in range(40):
        if not sh(T + ' mine').stdout.strip():
            return ''
        time.sleep(0.5)
    left = sh(T + ' mine').stdout.strip()
    if left:
        print('left after Command-Q, TERM'); sh(T + ' quitall'); time.sleep(2)
    return left


def one(tag, name, env, url, settle, stat, capture, sample=False, wheel=False):
    RO = '%s/out/%s/%s' % (B, tag, name)
    r = dict(variant=name, env=env, url=url)
    before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
    sh('rm -rf {p} && cp -a {t} {p} && mkdir -p {ro}'.format(p=PROFILE, t=TEMPLATE, ro=RO), check=True)
    d0 = int(sh('wc -l < /var/log/desktop-session.log').stdout.split()[0])
    r['launch'] = sh(T + " launch %s '_LAUNCHER=%s/bin/firefox-perf2,%s' %s" % (PROFILE, B, env, url), check=True).stdout.strip()
    r['t_launch'] = time.time()
    win = own_window(before)
    if win is None:
        r['error'] = 'no window'; quit_own(None); return r
    r['win'] = win['line'][:300]; r['t_window'] = time.time()
    time.sleep(settle)
    comp = compositor_pid()
    d1 = int(sh('wc -l < /var/log/desktop-session.log').stdout.split()[0])
    if wheel:
        # DESK9's scroll case: one wheel notch down every 33 ms into the own window during the stat
        st = subprocess.Popen(SSH + [T + ' stat %g %s/stat.json %s | head -1' % (stat, RO, comp)], stdout=subprocess.PIPE, text=True)
        time.sleep(0.3)
        try:
            inj('target %d\n' % win['id'] + 'wheel -1\nwait 33\n' * int(stat * 30 - 15), timeout=int(stat) + 20)
        except Exception as e:
            r['wheel_error'] = str(e)[:200]
        r['stat'] = json.loads(st.communicate(timeout=stat + 60)[0].strip().splitlines()[0])
    else:
        r['stat'] = json.loads(sh(T + ' stat %g %s/stat.json %s | head -1' % (stat, RO, comp)).stdout.strip().splitlines()[0])
    d2 = int(sh('wc -l < /var/log/desktop-session.log').stdout.split()[0])
    st = json.loads(sh('cat %s/stat.json' % RO).stdout)
    for p in st['procs']:
        if p['role'] == 'parent' and p['mine']:
            r['parent_pct'] = p['cpu_pct']
            r['parent_main_ms'] = sum(t['cpu_ms'] for t in p['threads_top'] if t['name'] in ('MainThread', ''))
            r['parent_threads'] = [(t['name'], t['cpu_ms']) for t in p['threads_top'][:8]]
            r['parent_pid'] = p['pid']
        if not p['mine']:
            r['compositor_pct'] = p['cpu_pct']
    r['roles'] = {}
    for p in st['procs']:
        if p['mine']:
            r['roles'][p['role']] = round(r['roles'].get(p['role'], 0) + p['cpu_pct'], 2)
    pac = sh('sed -n "%d,%dp" /var/log/desktop-session.log | grep "WMPACING.*window=%d "' % (d1 + 1, d2 + 12, win['id'])).stdout
    r['wmpacing'] = [l.strip()[:300] for l in pac.splitlines()]
    if 'parent_pid' in r:
        img = sh('python3 %s/tools/fxperf2_m1.py regions %d %s/regions.json >/dev/null; python3 -c "import json;d=json.load(open(\'%s/regions.json\'));'
                 'print(sorted(set(x[\'path\'] for x in d[\'regions\'] if x[\'path\'].startswith((\'/private/var/db/sffxperf2\',\'/System/Library/Frameworks/QuartzCore\',\'/System/Library/Frameworks/OpenGL\',\'/System/Library/Frameworks/AppKit\',\'/usr/lib/libWindowServer\')))))"'
                 % (B, r['parent_pid'], RO, RO), timeout=120).stdout.strip()
        r['images'] = img
    if sample and 'parent_pid' in r:
        # after the clean stat: sf-sample of the parent, 5 s at 2 ms (changes the numbers; never reported as CPU)
        sh('sample %d 5 2 -file %s/sample-parent.txt >/dev/null 2>&1' % (r['parent_pid'], RO), timeout=120)
        r['sampled'] = True
    if capture:
        try:
            inj('target %d\nkey e1 down\nkey e1 up\nwait 400\ncapture fxperf2-%s-%s' % (win['id'], tag, name), timeout=40)
            fetch('fxperf2-%s-%s' % (tag, name), OUT / tag / ('capture-%s.png' % name))
            r['capture'] = 'capture-%s.png' % name
        except Exception as e:
            r['capture_error'] = str(e)[:200]
    r['left'] = quit_own(win)
    pid = r['launch'].split()[0]
    sh('cp $(ls -t /var/root/Library/Logs/SiliconForest/Firefox/*-%s.log | head -1) %s/firefox.log 2>/dev/null; sed -n "%d,\\$p" /var/log/desktop-session.log > %s/desktop.log' % (pid, RO, d0 + 1, RO))
    return r


# FXLEAD's tile checker (runs/fxlead/tools/fxlead.py check_tiles, same page and palette)
GRID = [[230,25,75],[60,180,75],[255,225,25],[0,130,200],[245,130,48],[145,30,180],[70,240,240],[240,50,230],
        [210,245,60],[250,190,212],[0,128,128],[220,190,255],[170,110,40],[255,250,200],[128,0,0],[170,255,195],[0,0,128]]


def check_tiles(path, dpr=1.5, win=None):
    from PIL import Image
    near = lambda a, b, tol=12: all(abs(int(a[i]) - int(b[i])) <= tol for i in range(3))
    im = Image.open(path).convert('RGB'); px = im.load(); w, h = im.size
    o = None
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            if px[x, y] == (255, 0, 255):
                while x > 0 and px[x - 1, y] == (255, 0, 255): x -= 1
                while y > 0 and px[x, y - 1] == (255, 0, 255): y -= 1
                o = (x, y); break
        if o: break
    if not o:
        return dict(error='marker not found')
    ox, oy = o[0] - int(200 * dpr), o[1]
    bits = ''.join('1' if sum(px[int(ox + (200 + i * 8 + 4) * dpr), int(oy + 28 * dpr)]) < 200 else '0' for i in range(16))
    dock = 200   # device px from the screen's left edge: the desk's dock covers it
    scroll = int(bits, 2)
    bx = int(ox + 300 * dpr); y = int(oy + 40 * dpr); bottom = None
    while y < im.height - 1:
        if near(px[bx, y], (160, 16, 16), 20):
            bottom = y; break
        y += 1
    right = (win['x'] + win['w']) if win else im.width
    res = dict(origin=o, scrollY=scroll, banner_top=bottom, bad=[], good=0)
    for r in range(400):
        cy = r * 120 - scroll
        y0, y1 = oy + (cy + 3) * dpr, oy + (cy + 30) * dpr
        if y0 < oy + 34 * dpr or bottom is None or y1 >= bottom - 2:
            continue
        for c in range(10):
            x0 = ox + (c * 128 + 3) * dpr
            if x0 + 6 * dpr >= right or x0 < dock:
                continue
            exp = GRID[(r * 5 + c) % 17]
            samples = [px[int(x0 + dx), int(y0 + dy)] for dx in (0, 4, 8) for dy in (0, 6, 12, 20)]
            if sum(near(s, exp) for s in samples) >= len(samples) - 1:
                res['good'] += 1
            else:
                res['bad'].append(dict(r=r, c=c, x=int(x0 - ox), y=int(y0 - oy), got=samples[0], exp=exp))
    if bottom is not None:
        res['banner_bad_x'] = [x - ox for x in range(max(ox + 4, dock), right - 4, 40) if not near(px[x, bottom + int(5 * dpr)], (160, 16, 16), 20)]
    return res


def grid_one(tag, name, env):
    r = dict(variant=name, env=env)
    before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
    sh('rm -rf {p} && cp -a {t} {p}'.format(p=PROFILE, t=TEMPLATE), check=True)
    r['launch'] = sh(T + " launch %s '_LAUNCHER=%s/bin/firefox-perf2,%s' http://127.0.0.1:8776/tiles.html" % (PROFILE, B, env), check=True).stdout.strip()
    win = None
    t0 = time.time()
    while time.time() - t0 < 40:
        ws, _t = wins()
        new = [w for w in ws if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
        if new and 'tiles-done' in new[0]['title']:
            win = new[0]; break
        time.sleep(1)
    if win is None:
        r['error'] = 'no done window'; r['left'] = quit_own(None); return r
    time.sleep(1.5)
    try:
        inj('target %d\nkey e1 down\nkey e1 up\nwait 400\ncapture fxperf2-%s-%s' % (win['id'], tag, name), timeout=40)
        cap = fetch('fxperf2-%s-%s' % (tag, name), OUT / tag / ('grid-%s.png' % name))
        c = check_tiles(cap, win=win)
        r['check'] = dict(scrollY=c.get('scrollY'), good=c.get('good'), bad=len(c.get('bad', [])), banner_bad=len(c.get('banner_bad_x', [])),
                          error=c.get('error'), first_bad=c.get('bad', [])[:4])
    except Exception as e:
        r['capture_error'] = str(e)[:200]
    r['left'] = quit_own(win)
    return r


TAB_HTML = '''<!doctype html><meta charset=utf-8><title>FXPERF tab</title>
<style>body{font:20px sans-serif;margin:40px}</style>
<h1 id=h>FXPERF tab</h1><p>Static page for tab-switch timing.</p>
<script>
const i=new URLSearchParams(location.search).get('i')||'0';document.title='FXPERF tab '+i;
document.body.style.background=['#fff','#eef','#efe','#fee','#ffe','#eff','#fef','#ddd','#cdf','#fdc'][i%10];
function post(n,o){fetch('/r/'+n,{method:'POST',body:JSON.stringify(o)}).catch(()=>{});}
document.addEventListener('visibilitychange',()=>{if(document.visibilityState!=='visible')return;const t0=Date.now(),p0=performance.now();
 requestAnimationFrame(()=>{const t1=Date.now();requestAnimationFrame(()=>{post('tab'+i,{i,vis:t0,raf1:t1,raf2:Date.now(),tori:performance.timeOrigin,p0});});});});
</script>'''
SERVE = '''import http.server, time, sys
from pathlib import Path
W = Path(sys.argv[1]); R = Path(sys.argv[2]); R.mkdir(parents=True, exist_ok=True)
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(s, *a, **k): super().__init__(*a, directory=str(W), **k)
    def do_POST(s):
        n = int(s.headers.get('Content-Length', 0)); b = s.rfile.read(n)
        name = ''.join(c for c in s.path.rsplit('/', 1)[-1] if c.isalnum() or c in '-_') or 'r'
        (R / ('%s-%d.json' % (name, int(time.time() * 1000)))).write_bytes(b)
        s.send_response(204); s.end_headers()
    def log_message(s, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', 8776), H).serve_forever()
'''
ORDER = [2, 3, 4, 5, 1, 6, 7, 8, 2, 3, 9, 4]
# a deterministic stand-in for an animating news page: static text plus three small CSS animations (a spinner, a
# ticker, a pulsing box), as cnn.com's are, without its network and ad variance
ANIM_HTML = '''<!doctype html><meta charset=utf-8><title>FXPERF2 anim</title>
<style>body{font:18px/1.5 serif;margin:30px 60px;background:#fafafa}h1{font:bold 34px sans-serif}
#spin{position:absolute;left:900px;top:140px;width:120px;height:120px;background:#c33;animation:r 2s linear infinite}
#tick{position:absolute;left:60px;top:300px;width:700px;height:34px;overflow:hidden;background:#222;color:#fff}
#tick span{display:inline-block;white-space:nowrap;animation:t 8s linear infinite;font:20px/34px sans-serif}
#pulse{position:absolute;left:1000px;top:420px;width:220px;height:110px;background:#36c;animation:p 1.5s ease-in-out infinite alternate}
@keyframes r{to{transform:rotate(360deg)}}@keyframes t{from{transform:translateX(700px)}to{transform:translateX(-900px)}}
@keyframes p{from{opacity:.25}to{opacity:1}}</style>
<h1>FXPERF2 animation page</h1><div id=spin></div><div id=tick><span>Markets: DOW +0.4 % NASDAQ +0.7 % S&amp;P +0.5 % - Weather: clear, 12 C - Sports: home team 3, visitors 1</span></div>
<div id=pulse></div><div style="margin-top:420px;width:760px">''' + ''.join('<p>Paragraph %d. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.</p>' % i for i in range(12)) + '</div>'


def tabs_one(tag, name, env, resdir):
    r = dict(variant=name, env=env)
    before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
    sh('rm -rf {p} && cp -a {t} {p} && rm -rf {r} && mkdir -p {r}'.format(p=PROFILE, t=TEMPLATE, r=resdir), check=True)
    urls = ' '.join("'http://127.0.0.1:8776/tab.html?i=%d'" % i for i in range(10))
    r['launch'] = sh(T + " launch %s '_LAUNCHER=%s/bin/firefox-perf2,%s' %s" % (PROFILE, B, env, urls), check=True).stdout.strip()
    win = own_window(before)
    if win is None:
        r['error'] = 'no window'; quit_own(None); return r
    time.sleep(8)
    sw = []
    for k in ORDER:
        q = sh(T + ' req 10', inp='target %d\n' % win['id'] + keys('chord:cmd+%d' % k, 'wait:300'), timeout=40)
        m = re.search(r't_req=(\d+)', q.stdout)
        sw.append(dict(k=k, t_req=int(m[1]) if m else None, rc=q.returncode))
        time.sleep(0.9)
    time.sleep(1)
    res = []
    for line in sh('for f in %s/*.json; do echo "$(basename $f) $(cat $f)"; done' % resdir).stdout.splitlines():
        try:
            res.append(json.loads(line.split(' ', 1)[1]))
        except Exception:
            pass
    out = []
    for s_ in sw:
        if not s_['t_req']:
            continue
        want = str(s_['k'] - 1) if s_['k'] < 9 else '9'
        c = [x for x in res if x['i'] == want and s_['t_req'] <= x['vis'] <= s_['t_req'] + 3000]
        if c:
            out.append(dict(k=s_['k'], vis=c[0]['vis'] - s_['t_req'], raf1=c[0]['raf1'] - s_['t_req'], raf2=c[0]['raf2'] - s_['t_req']))
    r['switches'] = out
    r['left'] = quit_own(win)
    return r


# static pages for the pixel comparison: every one draws a magenta marker at its top left, finishes by itself and then
# says so in its title ('pix-done'), so two variants can be compared pixel for pixel from the marker
PIX_PAGES = {
 'still.html': '''<!doctype html><meta charset=utf-8><title>pix</title><style>body{font:18px/1.5 serif;margin:30px 60px;background:#fafafa}
#mk{position:fixed;left:200px;top:0;width:12px;height:12px;background:#f0f;z-index:9}
.b{display:inline-block;width:180px;height:120px;margin:10px;border-radius:18px;box-shadow:4px 6px 12px rgba(0,0,0,.4)}
</style><div id=mk></div><h1>Static mix</h1><div class=b style="background:linear-gradient(45deg,#c33,#36c)"></div>
<div class=b style="background:#3a3;opacity:.55"></div><div class=b style="background:radial-gradient(#ff0,#f80)"></div>
<div class=b style="background:#123;transform:rotate(7deg)"></div>''' + ''.join('<p>Paragraph %d: <b>bold</b> <i>italic</i> <code>code()</code> text, more text, numbers 0123456789.</p>' % i for i in range(30)) + '''
<script>addEventListener('load',()=>setTimeout(()=>document.title='pix-done',1500))</script>''',
 'scrolled.html': '''<!doctype html><meta charset=utf-8><title>pix</title><style>body{margin:0;font:16px sans-serif}
#mk{position:fixed;left:200px;top:0;width:12px;height:12px;background:#f0f;z-index:9}
#bar{position:fixed;left:12px;right:0;top:0;height:40px;background:#334;color:#fff;z-index:8;line-height:40px;padding-left:10px}
.r{height:90px;border-bottom:1px solid #ccc;padding:20px 40px;box-sizing:border-box}</style><div id=mk></div><div id=bar>fixed bar</div>
''' + ''.join('<div class=r style="background:hsl(%d,60%%,85%%)">Row %d - scrolled content with a fixed bar over it</div>' % (i * 37 % 360, i) for i in range(120)) + '''
<script>addEventListener('load',()=>{let n=0;const s=()=>{scrollBy(0,700);if(++n<6)setTimeout(s,250);else setTimeout(()=>document.title='pix-done',1500)};setTimeout(s,800)})</script>''',
 'nested.html': '''<!doctype html><meta charset=utf-8><title>pix</title><style>body{margin:0;font:15px sans-serif;background:#eee}
#mk{position:fixed;left:200px;top:0;width:12px;height:12px;background:#f0f;z-index:9}
.box{position:absolute;overflow:auto;width:420px;height:300px;border:2px solid #444;border-radius:12px;background:#fff}
.in{height:1400px;background:repeating-linear-gradient(0deg,#fff 0 20px,#dde 20px 40px)}</style><div id=mk></div>
<div class=box id=a style="left:40px;top:60px"><div class=in>scroll box A</div></div>
<div class=box id=b style="left:520px;top:120px"><div class=in>scroll box B</div></div>
<div class=box id=c style="left:1000px;top:60px;border-radius:0"><div class=in>scroll box C</div></div>
<script>addEventListener('load',()=>setTimeout(()=>{a.scrollTop=333;b.scrollTop=777;c.scrollTop=555;setTimeout(()=>document.title='pix-done',1500)},800))</script>''',
}
PIX_URLS = ['http://127.0.0.1:8776/still.html', 'http://127.0.0.1:8776/scrolled.html', 'http://127.0.0.1:8776/nested.html',
            'http://127.0.0.1:8776/tiles.html?steps=0', 'http://127.0.0.1:8776/tiles.html', 'http://127.0.0.1:8776/anim.html?still=1']


def pix_one(tag, name, env, url, n):
    r = dict(variant=name, url=url)
    before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
    sh('rm -rf {p} && cp -a {t} {p}'.format(p=PROFILE, t=TEMPLATE), check=True)
    r['launch'] = sh(T + " launch %s '_LAUNCHER=%s/bin/firefox-perf2,%s' '%s'" % (PROFILE, B, env, url), check=True).stdout.strip()
    win = None; t0 = time.time()
    while time.time() - t0 < 40:
        ws, _t = wins()
        new = [w for w in ws if w['app'] == 'org.mozilla.firefox' and w['mapped'] and w['id'] not in before]
        if new and ('pix-done' in new[0]['title'] or 'tiles-done' in new[0]['title'] or (url.endswith('still=1') and time.time() - t0 > 8)):
            win = new[0]; break
        time.sleep(1)
    if win is None:
        r['error'] = 'not done'; r['left'] = quit_own(None); return r
    time.sleep(1.5)
    try:
        inj('target %d\nkey e1 down\nkey e1 up\nwait 400\ncapture fxperf2-%s-%s-%d' % (win['id'], tag, name, n), timeout=40)
        cap = fetch('fxperf2-%s-%s-%d' % (tag, name, n), OUT / tag / ('pix-%s-%d.png' % (name, n)))
        r['capture'] = str(cap); r['win'] = dict(x=win['x'], y=win['y'], w=win['w'], h=win['h'])
    except Exception as e:
        r['capture_error'] = str(e)[:200]
    r['left'] = quit_own(win)
    return r


def pix_marker(im, win):
    px = im.load()
    for y in range(max(0, win['y']), min(im.height, win['y'] + win['h']), 2):
        for x in range(max(0, win['x']), min(im.width, win['x'] + win['w']), 2):
            if px[x, y] == (255, 0, 255):
                while x > 0 and px[x - 1, y] == (255, 0, 255): x -= 1
                while y > 0 and px[x, y - 1] == (255, 0, 255): y -= 1
                return x, y
    return None


def pix_compare(a, b):
    # the page from its own top-left marker to the window's lower right: Firefox's bars above it (a session-restore
    # notice after the known quit crash, FX-003) differ between launches and are not what is compared
    from PIL import Image, ImageChops
    ia, ib = Image.open(a['capture']).convert('RGB'), Image.open(b['capture']).convert('RGB')
    wa, wb = a['win'], b['win']
    ma, mb = pix_marker(ia, wa), pix_marker(ib, wb)
    if not ma or not mb:
        return dict(error='marker not found', markers=(ma, mb))
    w = min(wa['x'] + wa['w'] - ma[0], wb['x'] + wb['w'] - mb[0], ia.width - ma[0], ib.width - mb[0])
    h = min(wa['y'] + wa['h'] - ma[1], wb['y'] + wb['h'] - mb[1], ia.height - ma[1], ib.height - mb[1])
    ca = ia.crop((ma[0], ma[1], ma[0] + w, ma[1] + h)); cb = ib.crop((mb[0], mb[1], mb[0] + w, mb[1] + h))
    d = ImageChops.difference(ca, cb).convert('L').point(lambda v: 255 if v > 8 else 0)
    box = d.getbbox()
    return dict(size=(w, h), markers=(ma, mb), differing=sum(1 for v in d.histogram()[255:256]), bbox=box,
                differing_pixels=d.histogram()[255])


def main():
    a = sys.argv[1:]
    if a[0] == 'push':
        want = a[1:] or ['true:cand', 'false:base']
        for src, dst in (x.split(':') for x in want):
            res = REPO / ('runs/fxperf2/build/result-' + src)
            tar = subprocess.run(['tar', '-C', str(res.resolve()), '-cf', '-', '.'], capture_output=True, check=True).stdout
            r = subprocess.run(SSH + ['rm -rf %s/%s && mkdir -p %s/%s && tar -C %s/%s -xf - && chmod -R u+w %s/%s && mkdir -p %s/%s/Frameworks/AppKit.framework/Versions/C && ln -sf ../A/AppKit %s/%s/Frameworks/AppKit.framework/Versions/C/AppKit && shasum -a 256 %s/%s/lib/* %s/%s/Frameworks/*.framework/Versions/A/* 2>/dev/null | cut -c1-12,65-'
                                    % ((B, dst) * 8)], input=tar, capture_output=True)
            print(dst, r.returncode, r.stdout.decode()[-800:], r.stderr.decode()[-300:])
        return
    if a[0] == 'warm':
        with hold(2, 'warm profile template for own Firefox (no input)'):
            sh('rm -rf %s %s && mkdir -p %s && cat > %s/user.js' % (PROFILE, TEMPLATE, PROFILE, PROFILE), inp=QUIET, check=True)
            before = {w['id'] for w in wins()[0] if w['app'] == 'org.mozilla.firefox'}
            print(sh(T + " launch %s '_LAUNCHER=%s/bin/firefox-perf2' %s" % (PROFILE, B, a[1]), check=True).stdout)
            win = own_window(before)
            time.sleep(float(a[2]) if len(a) > 2 else 40)
            print('left', quit_own(win))
            sh('cp -a %s %s' % (PROFILE, TEMPLATE), check=True)
        return
    if a[0] == 'pix':
        tag = a[1]
        variants = [x.split('=', 1) for x in a[2:] if '=' in x]
        urls = [x for x in a[2:] if x.startswith('http')] or PIX_URLS
        (OUT / tag).mkdir(parents=True, exist_ok=True)
        res = []
        with hold(max(1, round(len(urls) * len(variants) * 20 / 60)), 'FXPERF2 pix %s: own Firefox on %d static pages x %s, Shift tap + capture of own window' % (
                tag, len(urls), '/'.join(n for n, _ in variants))):
            sh('mkdir -p %s/www && cat > %s/www/anim.html' % (B, B), inp=ANIM_HTML.replace('<title>FXPERF2 anim</title>', '<title>pix</title><style>*{animation-play-state:paused!important;animation-delay:-1s!important}</style><div style="position:fixed;left:200px;top:0;width:12px;height:12px;background:#f0f;z-index:9"></div>'), check=True)
            for fn, html in PIX_PAGES.items():
                sh('cat > %s/www/%s' % (B, fn), inp=html, check=True)
            sh("sed -e 's/#mk{position:fixed;left:0;/#mk{position:fixed;left:200px;/' -e 's/#code{position:fixed;left:0;/#code{position:fixed;left:200px;/' "
               "/private/var/db/sffxlead/www/tiles.html > %s/www/tiles.html" % B, check=True)
            sh('cat > %s/tools/serve.py' % B, inp=SERVE, check=True)
            srv = shbg('exec python3 %s/tools/serve.py %s/www %s/results' % (B, B, B), OUT / tag / 'http.log')
            time.sleep(1.5)
            try:
                for n, url in enumerate(urls):
                    pair = []
                    for name, env in variants:
                        r = pix_one(tag, name, env, url, n); r['name'] = name; res.append(r); pair.append(r)
                    if len(pair) == 2 and all('capture' in x for x in pair):
                        pair[1]['compare'] = pix_compare(pair[0], pair[1])
                    print(now(), url, json.dumps([x.get('compare') or x.get('error') or x.get('capture_error') for x in pair])[:300], flush=True)
                    json.dump(res, open(OUT / tag / 'pix.json', 'w'), indent=1)
            finally:
                if sh(T + ' mine').stdout.strip():
                    sh(T + ' quitall')
                sh("pkill -f 'sffxperf2/tools/serve.py' || true")
                srv.terminate()
        return
    if a[0] == 'tabs':
        import statistics
        tag, rounds = a[1], int(a[2])
        variants = [x.split('=', 1) for x in a[3:] if '=' in x]
        (OUT / tag).mkdir(parents=True, exist_ok=True)
        sh('mkdir -p %s/www && cat > %s/www/tab.html' % (B, B), inp=TAB_HTML, check=True)
        sh('cat > %s/tools/serve.py' % B, inp=SERVE, check=True)
        res = []
        with hold(max(1, round(rounds * len(variants) * 35 / 60)), 'FXPERF2 tabs %s: own Firefox, 10 static tabs, 12 Command-number switches into own window per round, %d x %s' % (
                tag, rounds, '/'.join(n for n, _ in variants))):
            srv = shbg('exec python3 %s/tools/serve.py %s/www %s/results' % (B, B, B), OUT / tag / 'http.log')
            time.sleep(1.5)
            try:
                for i in range(rounds):
                    for name, env in variants:
                        r = tabs_one(tag, '%s-%d' % (name, i), env, B + '/results'); r['i'] = i; r['name'] = name; res.append(r)
                        v = sorted(x['raf2'] for x in r.get('switches', []))
                        print(now(), name, i, 'n=%d raf2 p50=%s' % (len(v), statistics.median(v) if v else '-'), r.get('error', ''), flush=True)
                        json.dump(res, open(OUT / tag / 'tabs.json', 'w'), indent=1)
            finally:
                if sh(T + ' mine').stdout.strip():
                    sh(T + ' quitall')
                sh("pkill -f 'sffxperf2/tools/serve.py' || true")
                srv.terminate()
        for name, _env in variants:
            for k in ('vis', 'raf1', 'raf2'):
                v = sorted(x[k] for r in res if r['name'] == name for x in r.get('switches', []))
                if v:
                    print('%s %s: n=%d p50=%d p95=%d min=%d max=%d ms' % (name, k, len(v), statistics.median(v), v[min(len(v) - 1, int(.95 * len(v)))], v[0], v[-1]))
        return
    if a[0] == 'grid':
        tag, trials = a[1], int(a[2])
        variants = [x.split('=', 1) for x in a[3:] if '=' in x]
        (OUT / tag).mkdir(parents=True, exist_ok=True)
        res = []
        with hold(max(1, round(trials * len(variants) * 18 / 60)), 'FXPERF2 grid %s: own Firefox on the self-scrolling tile page, %d x %s, Shift tap + capture of own window' % (
                tag, trials, '/'.join(n for n, _ in variants))):
            sh("sed -e 's/#mk{position:fixed;left:0;/#mk{position:fixed;left:200px;/' -e 's/#code{position:fixed;left:0;/#code{position:fixed;left:200px;/' "
               "/private/var/db/sffxlead/www/tiles.html > %s/www/tiles.html; grep -c 'left:200px' %s/www/tiles.html" % (B, B), check=True)
            sh('cat > %s/tools/serve.py' % B, inp=SERVE, check=True)
            srv = shbg('exec python3 %s/tools/serve.py %s/www %s/results' % (B, B, B), OUT / tag / 'http.log')
            time.sleep(1.5)
            try:
                for i in range(trials):
                    for name, env in variants:
                        r = grid_one(tag, '%s-%d' % (name, i), env); r['i'] = i; r['name'] = name; res.append(r)
                        print(now(), name, i, json.dumps(r.get('check') or r.get('error') or r.get('capture_error'))[:300], flush=True)
                        json.dump(res, open(OUT / tag / 'grid.json', 'w'), indent=1)
            finally:
                if sh(T + ' mine').stdout.strip():
                    sh(T + ' quitall')
                sh("pkill -f 'sffxperf2/tools/serve.py' || true")
                srv.terminate()
        for name, _env in variants:
            rows = [r for r in res if r['name'] == name]
            wrong = [r for r in rows if not r.get('check') or r['check'].get('error') or r['check']['bad'] or r['check']['banner_bad']]
            print('%s: %d trials, %d with wrong cells/banner or no check' % (name, len(rows), len(wrong)))
        return
    if a[0] == 'run':
        tag, url, runs, settle, stat = a[1], a[2], int(a[3]), float(a[4]), float(a[5])
        variants = [x.split('=', 1) for x in a[6:] if '=' in x]
        capture = 'capture' in a[6:]
        sample = 'sample' in a[6:]
        wheel = 'wheel' in a[6:]
        (OUT / tag).mkdir(parents=True, exist_ok=True)
        res = []
        what = 'FXPERF2 A/B %s: %s, %d x %s (own Firefox, no input but Command-Q/capture to own window)' % (
            tag, url, runs, '/'.join(n for n, _ in variants))
        est = max(1, round(runs * len(variants) * (settle + stat + 25) / 60))
        local = url.startswith('http://127.0.0.1:8776/')
        with hold(est, what):
            if local:
                sh('mkdir -p %s/www && cat > %s/www/anim.html' % (B, B), inp=ANIM_HTML, check=True)
                # DESK9's scroll and 1080p60 video pages (runs/m1-desktop-010/desk9-tests/m1), the video served from here
                for fn in ('scroll9.html', 'stats9.js', 'video9.html'):
                    src = REPO / 'runs/m1-desktop-010/desk9-tests/m1' / fn
                    if src.exists():
                        sh('cat > %s/www/%s' % (B, fn), inp=src.read_text().replace(
                            'file:///private/var/db/sffinch/gpu-nv12-one-copy-v2/fixtures/', '/'), check=True)
                sh('ln -sf /private/var/db/sffinch/gpu-nv12-one-copy-v2/fixtures/pattern-1080p60-h264.mp4 %s/www/pattern-1080p60-h264.mp4' % B)
                sh('cat > %s/tools/serve.py' % B, inp=SERVE, check=True)
                srv = shbg('exec python3 %s/tools/serve.py %s/www %s/results' % (B, B, B), OUT / tag / 'http.log')
                time.sleep(1.5)
            try:
                for i in range(runs):
                    for name, env in variants:
                        print(now(), 'run', i, name, flush=True)
                        r = one(tag, '%s-%d' % (name, i), env, url, settle, stat, capture and i == 0, sample, wheel)
                        r['i'] = i; r['name'] = name; res.append(r)
                        print(json.dumps({k: r.get(k) for k in ('name', 'i', 'parent_pct', 'parent_main_ms', 'roles', 'wmpacing', 'error')})[:900], flush=True)
                        json.dump(res, open(OUT / tag / 'ab.json', 'w'), indent=1)
            finally:
                if sh(T + ' mine').stdout.strip():
                    print('cleanup TERM'); sh(T + ' quitall')
                if local:
                    sh("pkill -f 'sffxperf2/tools/serve.py' || true")
                    srv.terminate()
        r = subprocess.run(SSH + ['tar -C %s/out -cf - %s' % (B, tag)], capture_output=True, timeout=900)
        subprocess.run(['tar', '-C', str(OUT), '-xf', '-'], input=r.stdout, check=True)
        # the M1's disk is small and shared: the pulled run and the last profile copy go
        sh('rm -rf %s/out/%s %s' % (B, tag, PROFILE))
        return
    raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
