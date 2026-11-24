#!/usr/bin/env python3
"""TERM12 helper (copied from FXLEAD) on the M1 (own processes and profiles only; never the owner's).

fxlead_m1.py guard [idle_s]                 exit 0 if owner idle >= idle_s, else 3
fxlead_m1.py req [timeout] < lines          inject request on the virtual seat ('seat virtual' prepended)
fxlead_m1.py launch <profile> <json-overrides> [args...]
      overrides = {"env": {...}, "unset": [...], "prefs": {...}}; runs the installed launcher's own plan()
      (same providers/flags as firefox-sf), then applies the overrides; logs to /private/var/db/sft12/logs
fxlead_m1.py mine                           own Firefox processes (descendants of recorded launches)
fxlead_m1.py quitall                        SIGTERM own processes only
"""
import importlib.machinery, importlib.util, json, os, re, signal, subprocess, sys, time
from pathlib import Path

INJ = Path('/private/var/run/gpu-desktop/inject')
OWNER = Path('/private/var/run/gpu-desktop/owner-input')
BASE = Path('/private/var/db/sft12')
PIDS = BASE / 'pids.txt'
LAUNCHER = '/Applications/Firefox.app/Contents/MacOS/firefox-sf'
DEFAULT_PREFS = {'browser.shell.checkDefaultBrowser': False, 'app.update.auto': False, 'webgl.disable-angle': True,
                 'browser.tabs.inTitlebar': 0, 'browser.aboutwelcome.enabled': False,
                 'browser.preonboarding.enabled': False, 'browser.startup.homepage_override.mstone': 'ignore',
                 'startup.homepage_welcome_url': '', 'startup.homepage_welcome_url.additional': '',
                 'datareporting.policy.dataSubmissionEnabled': False, 'toolkit.telemetry.enabled': False,
                 'browser.sessionstore.resume_from_crash': False, 'browser.startup.page': 0}


def owner_state():
    t = OWNER.read_text()
    f = {k: int(v) for k, v in re.findall(r'^([a-z_]+)=(\d+)$', t, re.M)}
    return f.get('active', 1), (time.time() * 1000 - f.get('last_real_input_unix_ms', 0)) / 1000


def guard(need=0.0):
    active, idle = owner_state()
    print('owner active=%d idle=%.0fs' % (active, idle))
    return active == 0 and idle >= need


def req(lines, timeout=30.0):
    for p in ('request', 'request.part'):
        if (INJ / p).exists():
            print('another request pending'); return 4
    (INJ / 'done').unlink(missing_ok=True)
    with (INJ / 'request.part').open('x') as s:
        s.write('seat virtual\n' + lines.strip() + '\n')
    os.replace(INJ / 'request.part', INJ / 'request')
    end = time.monotonic() + timeout
    while not (INJ / 'done').exists() and time.monotonic() < end:
        time.sleep(0.05)
    if not (INJ / 'done').exists():
        print('timeout'); return 4
    time.sleep(0.05)
    d = (INJ / 'done').read_text().strip(); print(d)
    return 0 if re.search(r'errors=0\b', d) else 4


def tree():
    out = subprocess.run(['ps', '-axo', 'pid=,ppid=,rss=,pcpu=,state=,command='], capture_output=True, text=True).stdout
    rows = {}
    for line in out.splitlines():
        p = line.split(None, 5)
        if len(p) == 6:
            rows[int(p[0])] = (int(p[1]), int(p[2]), float(p[3]), p[4], p[5])
    return rows


def mine():
    roots = {int(x.split()[0]) for x in PIDS.read_text().splitlines() if x.strip()} if PIDS.exists() else set()
    rows = tree()
    keep = set(r for r in roots if r in rows and 'sft12' in rows[r][4])
    changed = True
    while changed:
        changed = False
        for pid, row in rows.items():
            if row[0] in keep and pid not in keep:
                keep.add(pid); changed = True
    return {p: rows[p] for p in sorted(keep)}


def launch(profile, overrides, args):
    profile = Path(profile)
    assert str(profile).startswith('/private/var/db/sft12/'), 'own profiles only'
    profile.mkdir(mode=0o700, parents=True, exist_ok=True)
    prefs = dict(DEFAULT_PREFS); prefs.update(overrides.get('prefs', {}))
    (profile / 'user.js').write_text(''.join('user_pref(%s,%s);\n' % (json.dumps(k), json.dumps(v))
                                            for k, v in prefs.items()))
    loader = importlib.machinery.SourceFileLoader('firefox_sf', LAUNCHER)
    spec = importlib.util.spec_from_loader('firefox_sf', loader); mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    root = Path(json.loads(Path(LAUNCHER).with_name('system-runtime.json').read_text())['target'])
    manifest = json.loads((root / 'release.json').read_text())
    for k in overrides.get('manifest', {}):
        manifest[k] = overrides['manifest'][k]
    command, env = mod.plan(root, ['-no-remote', '--profile', str(profile)] + list(args), manifest)
    for k in overrides.get('unset', []):
        env.pop(k, None)
    env.update(overrides.get('env', {}))
    for key, name in (('prepend_frameworks', 'DYLD_FRAMEWORK_PATH'), ('prepend_libraries', 'DYLD_LIBRARY_PATH')):
        if overrides.get(key):
            assert all(x.startswith(('/private/var/db/sft12/',)) for x in overrides[key].split(':')), 'own prefixes only'
            env[name] = overrides[key] + ':' + env.get(name, '')
    if manifest.get('private_fonts') and not overrides.get('no_fontconfig'):
        env['FONTCONFIG_FILE'] = str(root / 'fonts/fonts.conf')
    if overrides.get('no_fontconfig'):
        env.pop('FONTCONFIG_FILE', None)
    env['MOZ_NO_REMOTE'] = '1'
    logs = BASE / 'logs'; logs.mkdir(parents=True, exist_ok=True)
    name = logs / ('ff-%d.log' % int(time.time()))
    out = open(name, 'w')
    cwd = Path(manifest.get('original_bundle')) / 'Contents/MacOS'
    p = subprocess.Popen(command, env=env, cwd=str(cwd), stdin=subprocess.DEVNULL, stdout=out, stderr=out,
                         start_new_session=True)
    with PIDS.open('a') as s:
        s.write('%d %d %s\n' % (p.pid, int(time.time() * 1000), profile))
    print(json.dumps(dict(pid=p.pid, log=str(name), sffx={k: v for k, v in env.items() if k.startswith('SFFX_')},
                          frameworks=env.get('DYLD_FRAMEWORK_PATH'), libraries=env.get('DYLD_LIBRARY_PATH'))))


def main():
    a = sys.argv[1:]
    if a[0] == 'guard':
        sys.exit(0 if guard(float(a[1]) if len(a) > 1 else 0) else 3)
    if a[0] == 'req':
        sys.exit(req(sys.stdin.read(), float(a[1]) if len(a) > 1 else 30))
    if a[0] == 'launch':
        launch(a[1], json.loads(a[2]), a[3:]); return
    if a[0] == 'mine':
        for p, (pp, rss, cpu, st, cmd) in mine().items():
            print(p, pp, rss, cpu, st, cmd[:150])
        return
    if a[0] == 'quitall':
        m = mine()
        for p in m:
            try: os.kill(p, signal.SIGTERM)
            except ProcessLookupError: pass
        print('sent TERM to', sorted(m)); return
    raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
