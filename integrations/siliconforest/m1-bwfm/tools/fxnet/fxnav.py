#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET: my own Firefox (fresh profile under /private/var/db/sffxnet, -no-remote, crash reporter off), cold loads of
# real sites via Marionette, Navigation + Resource Timing per load. Runs ON THE M1, script fed on stdin.
# argv: TAG [headless|window] [MOZLOG(0/1)] [extra env k=v,...]
import json, os, socket, subprocess, sys, time, shutil, signal

TAG = sys.argv[1] if len(sys.argv) > 1 else 't'
MODE = sys.argv[2] if len(sys.argv) > 2 else 'headless'
MOZLOG = len(sys.argv) > 3 and sys.argv[3] == '1'
EXTRA = sys.argv[4] if len(sys.argv) > 4 else ''
BASE = '/private/var/db/sffxnet'
PROFILE = BASE + '/p1'
OUT = BASE + '/out/' + TAG
PORT = 28317
SITES = ['https://www.wikipedia.org/', 'https://www.bbc.co.uk/', 'https://www.cnn.com/', 'https://www.nytimes.com/',
         'https://github.com/', 'https://www.reddit.com/', 'https://www.mozilla.org/en-US/', 'https://news.ycombinator.com/',
         'https://www.theguardian.com/international', 'https://www.apple.com/']
if os.environ.get('FXSITES'):
    SITES = os.environ['FXSITES'].split(',')
USERJS = '''user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("app.update.auto", false);
user_pref("app.update.enabled", false);
user_pref("webgl.disable-angle", true);
user_pref("browser.tabs.inTitlebar", 0);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("startup.homepage_welcome_url", "");
user_pref("startup.homepage_welcome_url.additional", "");
user_pref("browser.startup.page", 0);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.enabled", false);
user_pref("marionette.port", %d);
user_pref("remote.prefs.recommended", false);
user_pref("browser.warnOnQuit", false);
user_pref("browser.tabs.warnOnClose", false);
''' % PORT
import base64
for kv in base64.b64decode(os.environ.get('FXPREFS_B64', '')).decode().split(';'):
    if kv.strip():
        USERJS += 'user_pref(%s);\n' % kv.strip()


class M:
    def __init__(s):
        s.k = socket.create_connection(('127.0.0.1', PORT), timeout=180); s.n = 0; s.rx()
    def rx(s):
        h = b''
        while True:
            c = s.k.recv(1)
            if not c: raise EOFError
            if c == b':': break
            h += c
        n = int(h); d = b''
        while len(d) < n:
            c = s.k.recv(n - len(d))
            if not c: raise EOFError
            d += c
        return json.loads(d)
    def cmd(s, name, p=None):
        s.n += 1
        b = json.dumps([0, s.n, name, p or {}]).encode()
        s.k.sendall(str(len(b)).encode() + b':' + b)
        r = s.rx()
        if r[2]: raise RuntimeError(r[2])
        v = r[3]
        return v['value'] if isinstance(v, dict) and set(v) == {'value'} else v
    def js(s, script, ctx='content', args=()):
        s.cmd('Marionette:SetContext', {'value': ctx})
        return s.cmd('WebDriver:ExecuteScript', {'script': script, 'args': list(args), 'newSandbox': True})


NAVJS = '''
const n = performance.getEntriesByType('navigation')[0];
const o = n ? n.toJSON() : {};
const r = performance.getEntriesByType('resource');
let firstReq = null, dnsMax = 0, conMax = 0, tlsMax = 0, ttfbMax = 0, lastEnd = 0, hosts = new Set(), conns = 0;
for (const e of r) {
  try { hosts.add(new URL(e.name).host); } catch (x) {}
  if (e.domainLookupEnd > e.domainLookupStart) dnsMax = Math.max(dnsMax, e.domainLookupEnd - e.domainLookupStart);
  if (e.connectEnd > e.connectStart) { conns++; conMax = Math.max(conMax, e.connectEnd - e.connectStart); }
  if (e.secureConnectionStart > 0 && e.connectEnd > e.secureConnectionStart) tlsMax = Math.max(tlsMax, e.connectEnd - e.secureConnectionStart);
  if (e.responseStart > e.requestStart && e.requestStart > 0) ttfbMax = Math.max(ttfbMax, e.responseStart - e.requestStart);
  lastEnd = Math.max(lastEnd, e.responseEnd);
}
const fcp = performance.getEntriesByName('first-contentful-paint')[0];
return {nav: o, res: {count: r.length, hosts: hosts.size, conns: conns, dnsMax: dnsMax, conMax: conMax, tlsMax: tlsMax,
        ttfbMax: ttfbMax, lastEnd: lastEnd, protos: [...new Set(r.map(e => e.nextHopProtocol))]},
        fcp: fcp ? fcp.startTime : null, url: location.href};
'''


def main():
    os.makedirs(OUT, exist_ok=True)
    shutil.rmtree(PROFILE, ignore_errors=True)
    os.makedirs(PROFILE)
    open(PROFILE + '/user.js', 'w').write(USERJS)
    env = os.environ.copy()
    env.update(MOZ_CRASHREPORTER_DISABLE='1', MOZ_NO_REMOTE='1', MOZ_REMOTE_ALLOW_SYSTEM_ACCESS='1', XDG_RUNTIME_DIR='/private/var/run/gpu-desktop',
               WAYLAND_DISPLAY='wayland-0')
    if MOZLOG:
        env.update(MOZ_LOG='timestamp,sync,nsHttp:3,nsHostResolver:5,nsSocketTransport:4,TRR:5,nsNotifyAddr:5,'
                   'NetworkConnectivityService:5', MOZ_LOG_FILE=OUT + '/moz.log')
    for kv in EXTRA.split(','):
        if '=' in kv:
            k, _, v = kv.partition('='); env[k] = v
    args = ['/Applications/Firefox.app/Contents/MacOS/firefox-sf', '-no-remote', '--profile', PROFILE, '--marionette']
    if MODE == 'headless':
        args.append('--headless')
    t0 = time.time()
    p = subprocess.Popen(args, env=env, cwd='/', stdin=subprocess.DEVNULL, stdout=open(OUT + '/stdout.log', 'w'),
                         stderr=subprocess.STDOUT, start_new_session=True)
    open(BASE + '/pids.txt', 'a').write('%d %d %s\n' % (p.pid, int(t0 * 1000), TAG))
    m = None
    for _ in range(240):
        try:
            m = M(); break
        except OSError:
            if p.poll() is not None: break
            time.sleep(0.5)
    if m is None:
        print('NO MARIONETTE rc=%s after %.1fs' % (p.poll(), time.time() - t0)); print(open(OUT + '/stdout.log').read()[-3000:])
        return
    m.cmd('WebDriver:NewSession', {'pageLoadStrategy': 'normal', 'timeouts': {'pageLoad': 60000, 'script': 60000}})
    print('%s marionette up %.1fs' % (TAG, time.time() - t0), flush=True)
    rows = []
    for u in SITES:
        tn = time.time()
        err = None
        try:
            m.cmd('Marionette:SetContext', {'value': 'content'})
            m.cmd('WebDriver:Navigate', {'url': u})
        except Exception as e:
            err = repr(e)[:200]
        wall = (time.time() - tn) * 1000
        time.sleep(1.0)
        try:
            r = m.js(NAVJS)
        except Exception as e:
            r = {'error': repr(e)[:200]}
        r['wall_ms'] = wall; r['site'] = u; r['nav_error'] = err
        rows.append(r)
        n = r.get('nav', {})
        def d(a, b):
            return (n.get(b, 0) - n.get(a, 0)) if n.get(b) else -1
        print('%s %-42s wall=%6.0f dns=%5.0f tcp=%5.0f tls=%5.0f req->resp=%6.0f resp=%5.0f dcl=%6.0f load=%6.0f fcp=%s res=%d hosts=%d dnsMax=%.0f conMax=%.0f tlsMax=%.0f proto=%s %s' % (
            TAG, u[:42], wall, d('domainLookupStart', 'domainLookupEnd'), d('connectStart', 'connectEnd'),
            (n.get('connectEnd', 0) - n.get('secureConnectionStart', 0)) if n.get('secureConnectionStart') else -1,
            d('requestStart', 'responseStart'), d('responseStart', 'responseEnd'), n.get('domContentLoadedEventEnd', -1),
            n.get('loadEventEnd', -1), ('%.0f' % r['fcp']) if r.get('fcp') else '-', r.get('res', {}).get('count', -1),
            r.get('res', {}).get('hosts', -1), r.get('res', {}).get('dnsMax', -1), r.get('res', {}).get('conMax', -1),
            r.get('res', {}).get('tlsMax', -1), n.get('nextHopProtocol'), err or ''), flush=True)
    try:
        dns = m.js('''const d = Cc["@mozilla.org/network/dashboard;1"].getService(Ci.nsIDashboard);
          return await new Promise(res => d.requestDNSInfo(x => res(x)));''', ctx='chrome')
    except Exception as e:
        dns = {'error': repr(e)[:300]}
    try:
        prefs = m.js('''const out = {}; for (const k of ["network.trr.mode","network.dns.disableIPv6","network.http.http3.enable",
          "network.proxy.type","network.dns.native_https_query","network.captive-portal-service.enabled",
          "network.connectivity-service.enabled","network.dns.get-ttl","network.http.speculative-parallel-limit",
          "security.enterprise_roots.enabled","network.dns.native-is-localhost"]) {
          const t = Services.prefs.getPrefType(k); out[k] = t == 128 ? Services.prefs.getBoolPref(k) : t == 64 ? Services.prefs.getIntPref(k) : t == 32 ? Services.prefs.getStringPref(k) : null; }
          const ncs = Cc["@mozilla.org/network/network-connectivity-service;1"].getService(Ci.nsINetworkConnectivityService);
          out.ncs = {dns4: ncs.DNSv4, dns6: ncs.DNSv6, ipv4: ncs.IPv4, ipv6: ncs.IPv6, nat64: ncs.NAT64};
          out.offline = Services.io.offline; out.linkUp = Services.io.connectivity;
          return out;''', ctx='chrome')
    except Exception as e:
        prefs = {'error': repr(e)[:300]}
    print('%s prefs %s' % (TAG, json.dumps(prefs)))
    json.dump({'rows': rows, 'dns': dns, 'prefs': prefs, 'mode': MODE}, open(OUT + '/result.json', 'w'), indent=1)
    try:
        m.cmd('Marionette:Quit', {'flags': ['eForceQuit']})
    except Exception:
        pass
    for _ in range(40):
        if p.poll() is not None: break
        time.sleep(0.5)
    if p.poll() is None:
        os.killpg(p.pid, signal.SIGTERM); time.sleep(2)
    print('%s done rc=%s total %.1fs' % (TAG, p.poll(), time.time() - t0))


main()
