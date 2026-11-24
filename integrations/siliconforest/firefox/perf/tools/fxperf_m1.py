#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""FXPERF helper on the M1 (own Firefox only; reads, never signals, other processes).

fxperf_m1.py launch <profile> <KEY=VAL,...|-> [firefox args]   start own Firefox, print launcher pid + ms
fxperf_m1.py mine                       own Firefox processes (descendants of launchers in pids.txt)
fxperf_m1.py quitall                    SIGTERM own processes only
fxperf_m1.py stat <secs> <out.json> [extra pids]   per-process + per-thread CPU/csw/wakeups/RSS deltas
fxperf_m1.py regions <pid> <out.json>   vm region map summary (vmmap-lite via proc_pidinfo)
fxperf_m1.py req [timeout]  < lines     inject request through the virtual seat (prepends 'seat virtual')
"""
import ctypes, json, os, re, signal, subprocess, sys, time
from pathlib import Path

BASE = Path('/private/var/db/sffxperf')
PIDS = BASE / 'pids.txt'
INJ = Path('/private/var/run/gpu-desktop/inject')
OWNER = Path('/private/var/run/gpu-desktop/owner-input')
libc = ctypes.CDLL('/usr/lib/libSystem.dylib', use_errno=True)
libc.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
libc.proc_pid_rusage.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_void_p]


class TB(ctypes.Structure):
    _fields_ = [('numer', ctypes.c_uint32), ('denom', ctypes.c_uint32)]


_tb = TB(); libc.mach_timebase_info(ctypes.byref(_tb))
ABS_NS = _tb.numer / _tb.denom
PAGE = os.sysconf('SC_PAGESIZE')


def taskinfo(pid):
    b = ctypes.create_string_buffer(96)
    n = libc.proc_pidinfo(pid, 4, 0, b, 96)
    if n != 96:
        return None
    q = list(ctypes.cast(b, ctypes.POINTER(ctypes.c_uint64))[0:6])
    i = list(ctypes.cast(ctypes.byref(b, 48), ctypes.POINTER(ctypes.c_int32))[0:12])
    keys = ['policy', 'faults', 'pageins', 'cow_faults', 'msgs_sent', 'msgs_recv', 'sys_mach', 'sys_unix', 'csw',
            'threads', 'running', 'priority']
    d = dict(zip(keys, i)); d.update(vsize=q[0], rss=q[1])
    return d


RU = ['user', 'system', 'pkg_idle_wkups', 'interrupt_wkups', 'pageins', 'wired', 'resident', 'phys_footprint',
      'start_abs', 'exit_abs', 'c_user', 'c_system', 'c_pkg', 'c_int', 'c_pageins', 'c_elapsed', 'dread', 'dwritten',
      'qos_default', 'qos_maint', 'qos_bg', 'qos_util', 'qos_legacy', 'qos_ui_init', 'qos_ui_inter', 'billed_sys',
      'serviced_sys', 'logical_writes', 'lifetime_max_fp', 'instructions', 'cycles', 'billed_energy',
      'serviced_energy', 'interval_max_fp', 'runnable']


def rusage(pid):
    b = ctypes.create_string_buffer(16 + 8 * len(RU))
    if libc.proc_pid_rusage(pid, 4, b) != 0:
        return None
    v = list(ctypes.cast(ctypes.byref(b, 16), ctypes.POINTER(ctypes.c_uint64))[0:len(RU)])
    d = dict(zip(RU, v))
    for k in ['user', 'system', 'qos_default', 'qos_maint', 'qos_bg', 'qos_util', 'qos_legacy', 'qos_ui_init',
              'qos_ui_inter', 'runnable']:
        d[k] = d[k] * ABS_NS  # ns
    return d


def threads(pid):
    n = 4096
    b = (ctypes.c_uint64 * n)()
    got = libc.proc_pidinfo(pid, 6, 0, b, ctypes.sizeof(b))
    out = {}
    for h in list(b)[:max(0, got) // 8]:
        t = ctypes.create_string_buffer(112)
        if libc.proc_pidinfo(pid, 5, h, t, 112) != 112:
            continue
        q = list(ctypes.cast(t, ctypes.POINTER(ctypes.c_uint64))[0:2])
        i = list(ctypes.cast(ctypes.byref(t, 16), ctypes.POINTER(ctypes.c_int32))[0:8])
        name = t.raw[48:112].split(b'\0')[0].decode('utf8', 'replace')
        out[h] = dict(name=name, user=q[0], system=q[1], run_state=i[2], flags=i[3], curpri=i[5], priority=i[6])
    return out


def ps_rows():
    out = subprocess.run(['ps', '-axo', 'pid=,ppid=,rss=,command='], capture_output=True, text=True).stdout
    rows = {}
    for line in out.splitlines():
        p = line.split(None, 3)
        if len(p) == 4:
            rows[int(p[0])] = (int(p[1]), int(p[2]), p[3])
    return rows


def mine():
    roots = set()
    if PIDS.exists():
        roots = {int(x.split()[0]) for x in PIDS.read_text().split('\n') if x.strip()}
    rows = ps_rows()
    keep = set(r for r in roots if r in rows and 'sffxperf' in rows[r][2])
    changed = True
    while changed:
        changed = False
        for pid, (pp, _r, _c) in rows.items():
            if pp in keep and pid not in keep:
                keep.add(pid); changed = True
    return {p: rows[p] for p in sorted(keep)}


def role(cmd):
    if 'gpu-helper' in cmd: return 'gpu'
    if '-isForBrowser' in cmd: return 'content'
    if 'plugin-container' in cmd: return 'utility'
    if 'iosurfaced' in cmd: return 'iosurfaced'
    if 'surface-session' in cmd: return 'session'
    if 'MacOS/firefox ' in cmd or cmd.endswith('MacOS/firefox'): return 'parent'
    if 'crashreporter' in cmd: return 'crashreporter'
    return 'other'


def snapshot(pids):
    s = {}
    for p in pids:
        s[p] = dict(t=time.time(), ru=rusage(p), ti=taskinfo(p), th=threads(p))
    return s


def stat(secs, out, extra=()):
    m = mine(); rows = ps_rows()
    pids = list(m) + [p for p in extra if p in rows]
    a = snapshot(pids); t0 = time.time(); time.sleep(secs); b = snapshot(pids); dt = time.time() - t0
    procs = []
    tot = dict(cpu_ms=0, csw=0, int_wk=0, idle_wk=0, footprint_mb=0, rss_mb=0)
    for p in pids:
        if a[p]['ru'] is None or b[p]['ru'] is None:
            continue
        ra, rb, ta, tb = a[p]['ru'], b[p]['ru'], a[p]['ti'] or {}, b[p]['ti'] or {}
        cpu = (rb['user'] + rb['system'] - ra['user'] - ra['system']) / 1e6
        d = dict(pid=p, role=role(rows.get(p, (0, 0, ''))[2]), mine=p in m, cpu_ms=round(cpu, 1),
                 cpu_pct=round(100 * cpu / 1e3 / dt, 2),
                 user_ms=round((rb['user'] - ra['user']) / 1e6, 1), sys_ms=round((rb['system'] - ra['system']) / 1e6, 1),
                 int_wkups=rb['interrupt_wkups'] - ra['interrupt_wkups'], idle_wkups=rb['pkg_idle_wkups'] - ra['pkg_idle_wkups'],
                 csw=tb.get('csw', 0) - ta.get('csw', 0), msgs_sent=tb.get('msgs_sent', 0) - ta.get('msgs_sent', 0),
                 msgs_recv=tb.get('msgs_recv', 0) - ta.get('msgs_recv', 0),
                 sys_unix=tb.get('sys_unix', 0) - ta.get('sys_unix', 0), sys_mach=tb.get('sys_mach', 0) - ta.get('sys_mach', 0),
                 faults=tb.get('faults', 0) - ta.get('faults', 0), threads=tb.get('threads'),
                 rss_mb=round(rb['resident'] / 2**20, 1), footprint_mb=round(rb['phys_footprint'] / 2**20, 1),
                 runnable_ms=round((rb['runnable'] - ra['runnable']) / 1e6, 1),
                 qos_ms={k: round((rb[k] - ra[k]) / 1e6, 1) for k in RU if k.startswith('qos_') and rb[k] - ra[k] > 1e6})
        th = []
        for h, x in b[p]['th'].items():
            y = a[p]['th'].get(h)
            c = (x['user'] + x['system'] - ((y['user'] + y['system']) if y else 0)) / 1e6
            if c > 0.05 or y is None:
                th.append(dict(name=x['name'], cpu_ms=round(c, 2), new=y is None, pri=x['curpri']))
        th.sort(key=lambda r: -r['cpu_ms'])
        d['threads_top'] = th[:25]; d['cmd'] = rows.get(p, (0, 0, ''))[2][:160]
        procs.append(d)
        if p in m:
            tot['cpu_ms'] += cpu; tot['csw'] += d['csw']; tot['int_wk'] += d['int_wkups']; tot['idle_wk'] += d['idle_wkups']
            tot['footprint_mb'] += d['footprint_mb']; tot['rss_mb'] += d['rss_mb']
    res = dict(secs=round(dt, 2), family_cpu_pct=round(100 * tot['cpu_ms'] / 1e3 / dt, 2),
               family_csw_per_s=round(tot['csw'] / dt, 1), family_int_wkups_per_s=round(tot['int_wk'] / dt, 1),
               family_idle_wkups_per_s=round(tot['idle_wk'] / dt, 1), family_rss_mb=round(tot['rss_mb']),
               family_footprint_mb=round(tot['footprint_mb']), procs=procs)
    Path(out).write_text(json.dumps(res, indent=1))
    print(json.dumps({k: v for k, v in res.items() if k != 'procs'}))
    for d in procs:
        print('%6d %-9s cpu=%6.2f%% csw=%6d intwk=%5d rss=%7.1f fp=%7.1f top=%s' % (
            d['pid'], d['role'], d['cpu_pct'], d['csw'], d['int_wkups'], d['rss_mb'], d['footprint_mb'],
            ', '.join('%s:%.0f' % (t['name'] or '?', t['cpu_ms']) for t in d['threads_top'][:6])))


TAGS = {1: 'malloc', 2: 'malloc_small', 3: 'malloc_large', 4: 'malloc_huge', 5: 'sbrk', 6: 'realloc', 7: 'malloc_tiny',
        8: 'malloc_large_reusable', 9: 'malloc_large_reused', 10: 'analysis', 11: 'malloc_nano', 12: 'malloc_medium',
        20: 'mach_msg', 21: 'iokit', 30: 'stack', 31: 'guard', 32: 'shared_pmap', 33: 'dylib', 34: 'objc_dispatchers',
        35: 'unshared_pmap', 40: 'appkit', 41: 'foundation', 42: 'coregraphics', 43: 'coreservices', 44: 'carbon',
        45: 'java', 46: 'coredata', 47: 'coredata_objectids', 50: 'ats', 51: 'layerkit', 52: 'cgimage', 53: 'tcmalloc',
        54: 'cg_data', 55: 'cg_shared', 56: 'cg_framebuffers', 57: 'cg_backingstores', 58: 'cg_xalloc', 60: 'dyld',
        61: 'dyld_malloc', 62: 'sqlite', 63: 'javascript_core', 64: 'jit', 65: 'jsc_jit_reg', 66: 'glsl',
        67: 'opencl', 68: 'coreimage', 69: 'webcore_purgeable', 70: 'imageio', 71: 'coreprofile', 72: 'assetsd',
        73: 'os_once_alloc', 74: 'libdispatch', 75: 'accelerate', 76: 'coreui', 77: 'coreuifile', 78: 'genealogy',
        79: 'rawcamera', 80: 'corpse', 81: 'tcmalloc', 82: 'libnetwork', 83: 'audio', 84: 'videobitstream',
        85: 'cm_xpc', 86: 'cm_rpc', 87: 'cm_memorypool', 88: 'cm_readcache', 89: 'cm_crabs', 90: 'quicklook',
        91: 'accounts', 92: 'sanitizer', 93: 'iosaccel', 94: 'skywalk', 95: 'ioskit_mm', 98: 'shm_ipc',
        99: 'cm_ioservice', 100: 'appkit_memory_quarantine', 239: 'rosetta', 240: 'application_specific_1'}
SM = {1: 'COW', 2: 'PRV', 3: 'NUL', 4: 'SHM', 5: 'TSH', 6: 'P/A', 7: 'S/A', 8: 'LPG'}


def regions(pid, out):
    size = 96 + 1176
    addr = 0; regs = []
    while True:
        b = ctypes.create_string_buffer(size)
        n = libc.proc_pidinfo(pid, 8, addr, b, size)
        if n <= 0:
            break
        u32 = lambda off: int.from_bytes(b.raw[off:off + 4], 'little')
        u64 = lambda off: int.from_bytes(b.raw[off:off + 8], 'little')
        prot, maxp, inh, flags = u32(0), u32(4), u32(8), u32(12)
        f = [u32(24 + 4 * k) for k in range(14)]
        tag, resident, shared_now_private, swapped, dirtied, ref, shadow, share, priv_res, shr_res = f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]
        a, s = u64(80), u64(88)
        path = b.raw[96 + 152:96 + 1176].split(b'\0')[0].decode('utf8', 'replace')
        regs.append(dict(a=a, s=s, prot=prot, tag=tag, res=resident * PAGE, prv=priv_res * PAGE, shr=shr_res * PAGE,
                         dirty=dirtied * PAGE, sw=swapped * PAGE, share=SM.get(share, str(share)), path=path))
        if s == 0:
            break
        addr = a + s
    # summary by category
    cat = {}
    for r in regs:
        if r['path']:
            p = r['path']
            k = 'file:' + (re.sub(r'^/nix/store/[a-z0-9]{32}-', '/nix/store/*-', p))
        else:
            k = 'anon:' + TAGS.get(r['tag'], 'tag%d' % r['tag']) + ':' + r['share']
        c = cat.setdefault(k, dict(n=0, vsize=0, res=0, prv=0, shr=0, dirty=0, sw=0))
        c['n'] += 1; c['vsize'] += r['s']; c['res'] += r['res']; c['prv'] += r['prv']; c['shr'] += r['shr']
        c['dirty'] += r['dirty']; c['sw'] += r['sw']
    tot = dict(vsize=sum(r['s'] for r in regs), res=sum(r['res'] for r in regs), prv=sum(r['prv'] for r in regs),
               dirty=sum(r['dirty'] for r in regs), n=len(regs))
    Path(out).write_text(json.dumps(dict(pid=pid, page=PAGE, total=tot, categories=cat, regions=regs)))
    print('pid %d regions %d vsize %.0f MB resident %.0f MB private-resident %.0f MB dirty %.0f MB' % (
        pid, tot['n'], tot['vsize'] / 2**20, tot['res'] / 2**20, tot['prv'] / 2**20, tot['dirty'] / 2**20))
    for k, c in sorted(cat.items(), key=lambda kv: -kv[1]['res'])[:25]:
        print('  %-90s n=%5d res=%8.1fMB prv=%8.1fMB dirty=%8.1fMB vsz=%9.1fMB' % (
            k[:90], c['n'], c['res'] / 2**20, c['prv'] / 2**20, c['dirty'] / 2**20, c['vsize'] / 2**20))


def owner_state():
    t = OWNER.read_text()
    f = {k: int(v) for k, v in re.findall(r'^([a-z_]+)=(\d+)$', t, re.M)}
    return f.get('active', 1), (time.time() * 1000 - f.get('last_real_input_unix_ms', 0)) / 1000


def req(lines, timeout=30.0):
    active, idle = owner_state()
    print('owner active=%d idle=%.0fs' % (active, idle))
    for p in ('request', 'request.part'):
        if (INJ / p).exists():
            print('another request pending'); return 4
    (INJ / 'done').unlink(missing_ok=True)
    with (INJ / 'request.part').open('x') as s:
        s.write('seat virtual\n' + lines.strip() + '\n')
    os.replace(INJ / 'request.part', INJ / 'request')
    print('t_req=%d' % int(time.time() * 1000), flush=True)
    end = time.monotonic() + timeout
    while not (INJ / 'done').exists() and time.monotonic() < end:
        time.sleep(0.02)
    if not (INJ / 'done').exists():
        print('timeout'); return 4
    time.sleep(0.03)
    d = (INJ / 'done').read_text().strip()
    print('t_done=%d' % int(time.time() * 1000)); print(d)
    if re.search(r'owner_seen_active=1\b', d): return 3
    if not re.search(r'errors=0\b', d): return 4
    return 0


def launch(profile, envspec, args):
    Path(profile).mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(MOZ_NO_REMOTE='1', XDG_RUNTIME_DIR='/private/var/run/gpu-desktop', WAYLAND_DISPLAY='wayland-0')
    if envspec != '-':
        for kv in envspec.split(','):
            k, _, v = kv.partition('=')
            env[k] = v.replace('%', ',')
    launcher = env.pop('_LAUNCHER', '/Applications/Firefox.app/Contents/MacOS/firefox-sf')
    assert launcher in ('/Applications/Firefox.app/Contents/MacOS/firefox-sf', '/private/var/db/sffxperf/bin/firefox-perf')
    cmd = [launcher, '-no-remote', '--profile', profile] + args
    t = time.time()
    p = subprocess.Popen(cmd, env=env, cwd='/', stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    with PIDS.open('a') as s:
        s.write('%d %d %s\n' % (p.pid, int(t * 1000), profile))
    print(p.pid, int(t * 1000))


def main():
    a = sys.argv[1:]
    if a[0] == 'launch':
        launch(a[1], a[2], a[3:]); return
    if a[0] == 'mine':
        for p, (pp, rss, cmd) in mine().items():
            print(p, pp, rss, role(cmd), cmd[:150])
        return
    if a[0] == 'quitall':
        m = mine()
        for p in m:
            try: os.kill(p, signal.SIGTERM)
            except ProcessLookupError: pass
        print('sent TERM to', sorted(m)); return
    if a[0] == 'stat':
        stat(float(a[1]), a[2], [int(x) for x in a[3:]]); return
    if a[0] == 'regions':
        regions(int(a[1]), a[2]); return
    if a[0] == 'req':
        sys.exit(req(sys.stdin.read(), float(a[1]) if len(a) > 1 else 30))
    raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
