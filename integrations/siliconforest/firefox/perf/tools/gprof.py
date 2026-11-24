#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarise a raw Gecko profile (gzipped JSON from getProfileDataAsGzippedArrayBuffer / MOZ_PROFILER_SHUTDOWN).
gprof.py <profile.json[.gz]> [--threads N] [--frames N] [--markers] [--sym] [--focus THREADNAME]
Native frames are mapped to lib+offset; --sym symbolicates XUL (and other Mozilla libs) through Mozilla's
symbolication API and our own libraries with llvm-symbolizer on copies under runs/fxperf/bin (fetched on demand)."""
import collections, gzip, json, os, re, subprocess, sys, urllib.request
from pathlib import Path

BIN = Path('/home/chrisk/src/darwin-lab/runs/fxperf/bin')
CACHE = Path('/home/chrisk/src/darwin-lab/runs/fxperf/symcache.json')
SSH = ['ssh', '-o', 'UserKnownHostsFile=' + os.path.expanduser('~/.ssh/known_hosts_m1_siliconforest'),
       '-o', 'GlobalKnownHostsFile=/dev/null', '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlias=m1-siliconforest',
       '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', 'root@10.1.1.211']


def load(p):
    b = open(p, 'rb').read()
    if b[:2] == b'\x1f\x8b':
        b = gzip.decompress(b)
    return json.loads(b)


def walk(prof, out=None, depth=0):
    out = [] if out is None else out
    out.append(prof)
    for c in prof.get('processes', []):
        if isinstance(c, str):
            c = json.loads(c)
        walk(c, out, depth + 1)
    return out


def table(t):
    sch = t['schema']; return sch, t['data']


class Thread:
    def __init__(s, th, libs):
        s.th = th; s.libs = sorted(libs, key=lambda l: l['start'])
        s.strings = th.get('stringTable') or th.get('stringArray') or []
        s.fsch, s.frames = table(th['frameTable'])
        s.ssch, s.stacks = table(th['stackTable'])

    def frame_name(s, fi):
        loc = s.frames[fi][s.fsch['location']]
        name = s.strings[loc] if isinstance(loc, int) else str(loc)
        if name.startswith('0x'):
            a = int(name, 16)
            for l in s.libs:
                if l['start'] <= a < l['end']:
                    return ('NATIVE', l['debugName'] or l['name'], a - l['start'] + l.get('offset', 0), l)
            return ('NATIVE', '?', a, None)
        return name

    def stack_frames(s, si):
        out = []
        while si is not None:
            row = s.stacks[si]
            out.append(row[s.ssch['frame']])
            si = row[s.ssch['prefix']]
        return out  # leaf first


def symbolicate(natives, libmap):
    """natives: set of (libname, offset) -> dict key->name"""
    cache = json.loads(CACHE.read_text()) if CACHE.exists() else {}
    want = collections.defaultdict(set)
    libinfo = {}
    for name, off in natives:
        lib = libmap.get(name)
        k = '%s+0x%x' % (name, off)
        if k in cache or lib is None:
            continue
        want[name].add(off); libinfo[name] = lib
    moz = [n for n in want if libinfo[n].get('breakpadId') and ('/Firefox.app/' in libinfo[n].get('path', '') or n in ('XUL', 'firefox', 'plugin-container', 'libmozglue.dylib', 'libnss3.dylib', 'libmozavcodec.dylib', 'libmozavutil.dylib', 'libgkcodecs.dylib', 'liblgpllibs.dylib', 'libsoftokn3.dylib', 'libfreebl3.dylib', 'libmozsandbox.dylib', 'libipcclientcerts.dylib', 'libosclientcerts.dylib'))]
    if moz:
        mm = [[n, libinfo[n]['breakpadId']] for n in moz]
        offs = [[[i, o] for o in sorted(want[n])] for i, n in enumerate(moz)]
        stacks = [sum(offs, [])]
        body = json.dumps({'jobs': [{'memoryMap': mm, 'stacks': stacks}]}).encode()
        try:
            req = urllib.request.Request('https://symbolication.services.mozilla.com/symbolicate/v5', body,
                                         {'Content-Type': 'application/json'})
            res = json.loads(urllib.request.urlopen(req, timeout=120).read())
            for fr in res['results'][0]['stacks'][0]:
                n = moz[fr['module_index']] if 'module_index' in fr else None
                off = int(fr['module_offset'], 16)
                lib = fr.get('module') or n
                if fr.get('function'):
                    cache['%s+0x%x' % (lib, off)] = fr['function']
        except Exception as e:
            print('symbolication failed', e, file=sys.stderr)
    for n in want:
        if n in moz:
            continue
        lib = libinfo[n]; path = lib.get('path') or lib.get('name')
        local = BIN / path.lstrip('/')
        if not local.exists():
            local.parent.mkdir(parents=True, exist_ok=True)
            r = subprocess.run(SSH + ['cat "%s"' % path], capture_output=True, timeout=300)
            if r.returncode or not r.stdout:
                continue
            local.write_bytes(r.stdout)
        offs = sorted(want[n])
        # offsets are file-relative for the __TEXT segment start (vmaddr of __TEXT added by llvm-symbolizer --adjust-vma)
        try:
            vm = subprocess.run(['llvm-objdump', '--macho', '--private-headers', str(local)], capture_output=True, text=True).stdout
            m = re.search(r'segname __TEXT\n\s+vmaddr (0x[0-9a-f]+)', vm)
            base = int(m[1], 16) if m else 0
            inp = '\n'.join('0x%x' % (base + o) for o in offs)
            r = subprocess.run(['llvm-symbolizer', '--obj=' + str(local), '--functions=linkage', '--no-inlines', '--demangle'],
                               input=inp, capture_output=True, text=True, timeout=300)
            names = [blk.split('\n')[0] for blk in r.stdout.strip().split('\n\n')]
            for o, nm in zip(offs, names):
                if nm and nm != '??':
                    cache['%s+0x%x' % (n, o)] = nm
        except Exception as e:
            print('llvm-symbolizer failed', n, e, file=sys.stderr)
    CACHE.write_text(json.dumps(cache))
    return cache


def name_of(fr, cache):
    if isinstance(fr, tuple):
        k = '%s+0x%x' % (fr[1], fr[2])
        n = cache.get(k)
        return '%s!%s' % (fr[1].split('/')[-1], n) if n else k
    return fr


BUSY = '--busy' in sys.argv
WAITS = set()


def main():
    a = sys.argv[1:]
    if BUSY:
        cache = json.loads(CACHE.read_text()) if CACHE.exists() else {}
        for k, v in cache.items():
            if k.startswith('libSystem') and any(w in v for w in ('cvwait', 'semaphore_wait', 'mach_msg', 'kevent', '__select', 'mutexwait', 'workq_kernreturn', 'ulock_wait', '__recvfrom', 'swtch_pri', 'semwait', '__wait4', 'poll')):
                n, _, off = k.partition('+0x'); WAITS.add((n, int(off, 16)))
    p = a[0]
    nthreads = int(a[a.index('--threads') + 1]) if '--threads' in a else 12
    nframes = int(a[a.index('--frames') + 1]) if '--frames' in a else 15
    focus = a[a.index('--focus') + 1] if '--focus' in a else None
    prof = load(p)
    procs = walk(prof)
    rows = []
    natives = set()
    per = []
    for pr in procs:
        libs = pr.get('libs', [])
        meta = pr.get('meta', {})
        unit = (meta.get('sampleUnits') or {}).get('threadCPUDelta', '?')
        for th in pr.get('threads', []):
            t = Thread(th, libs)
            ssch, sdata = table(th['samples'])
            ci = ssch.get('threadCPUDelta')
            cpu = 0; n = len(sdata); self_c = collections.Counter(); incl = collections.Counter()
            for row in sdata:
                w = ((row[ci] if len(row) > ci else 0) or 0) if (ci is not None and not BUSY) else 1
                if row[ssch['stack']] is None:
                    continue
                fs = t.stack_frames(row[ssch['stack']])
                names = [t.frame_name(f) for f in fs]
                if BUSY and isinstance(names[0], tuple) and names[0][1].startswith('libSystem') and (names[0][1], names[0][2]) in WAITS:
                    continue
                cpu += w
                self_c[names[0] if not isinstance(names[0], tuple) else (names[0][0], names[0][1], names[0][2])] += w
                seen = set()
                for nm in names:
                    key = nm if not isinstance(nm, tuple) else (nm[0], nm[1], nm[2])
                    if key in seen: continue
                    seen.add(key); incl[key] += w
            msch, mdata = table(th['markers'])
            mk = collections.Counter(); mdur = collections.Counter()
            for m in mdata:
                nm = t.strings[m[msch['name']]] if isinstance(m[msch['name']], int) else m[msch['name']]
                mk[nm] += 1
                st, en = m[msch['startTime']], m[msch['endTime']]
                if st is not None and en is not None:
                    mdur[nm] += en - st
            per.append(dict(proc=th.get('processName') or th.get('processType'), pid=th.get('pid'), name=th['name'], tid=th.get('tid'),
                            samples=n, cpu=cpu, unit=unit, self=self_c, incl=incl, markers=mk, mdur=mdur, t=t, libs=libs))
    # symbolicate everything we will print
    libmap = {}
    for pr in procs:
        for l in pr.get('libs', []):
            libmap[l['debugName'] or l['name']] = l
    keys = set()
    for d in per:
        for k, _ in d['self'].most_common(nframes) + d['incl'].most_common(nframes * 2):
            if isinstance(k, tuple):
                keys.add((k[1], k[2]))
    cache = symbolicate(keys, libmap) if '--sym' in a else {}
    dur = prof['meta'].get('profilingEndTime', 0) - prof['meta'].get('profilingStartTime', 0) if prof['meta'].get('profilingEndTime') else None
    print('profile', p, 'interval', prof['meta'].get('interval'), 'ms; duration', dur, 'ms; processes', len(procs))
    per.sort(key=lambda d: -d['cpu'])
    for d in per[:nthreads] if not focus else [d for d in per if focus in d['name']]:
        print('\n== %s pid=%s  %s tid=%s  samples=%d cpu=%s %s' % (d['proc'], d['pid'], d['name'], d['tid'], d['samples'], round(d['cpu']), d['unit']))
        tot = d['cpu'] or 1
        print('  self:')
        for k, w in d['self'].most_common(nframes):
            print('   %5.1f%%  %s' % (100 * w / tot, name_of(k, cache)[:150]))
        print('  inclusive:')
        for k, w in d['incl'].most_common(nframes * 2):
            print('   %5.1f%%  %s' % (100 * w / tot, name_of(k, cache)[:150]))
        if '--markers' in a:
            print('  markers:', ', '.join('%s:%d(%.0fms)' % (k, v, d['mdur'][k]) for k, v in d['markers'].most_common(25)))


if __name__ == '__main__':
    main()
