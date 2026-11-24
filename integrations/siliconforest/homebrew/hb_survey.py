#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Static survey: what do unmodified Homebrew bottles ask of the system, and does a
SiliconForest system root have it?

For every Mach-O in the unpacked bottles (runs/homebrew/kegs/<tag>/) this records the
libraries it links (system ones under /usr/lib and /System/Library; Homebrew ones under
the prefix), its minimum OS and SDK, and every symbol it imports from each system library.
Each system library is then looked up in a system root (a directory with usr/lib and
System/Library as the image has them) and each imported symbol in that library's export
trie, following re-exports.

This is static analysis: it predicts what dyld will refuse at launch (bottles use chained
fixups, so every non-weak import must resolve before main). It says nothing about whether
a present symbol behaves correctly, about system calls, or about files a program expects.

  hb_survey.py --sysroot runs/homebrew/sysroot/p0 --name p0
writes runs/homebrew/survey/<name>.json and <name>.md.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hb_macho  # noqa: E402

LAB = Path(__file__).resolve().parents[3]
RUNS = LAB / 'runs/homebrew'
SYSTEM_PREFIXES = ('/usr/lib/', '/System/Library/', '/Library/')


class Sysroot:
    def __init__(self, root):
        self.root = Path(root)
        self.cache = {}

    def resolve(self, name, depth=0):
        """Follow symlinks inside the system root (absolute links are relative to it)."""
        p = self.root / name.lstrip('/')
        parts, cur = Path(name.lstrip('/')).parts, self.root
        for i, part in enumerate(parts):
            cur = cur / part
            if cur.is_symlink():
                if depth > 20:
                    return None
                target = os.readlink(cur)
                rest = '/'.join(parts[i + 1:])
                base = target if target.startswith('/') else os.path.normpath(os.path.join('/', *parts[:i], target))
                return self.resolve(base + ('/' + rest if rest else ''), depth + 1)
        return p if p.is_file() else None

    def exports(self, name, depth=0):
        """Every symbol a client linking `name` can bind: own exports plus, recursively,
        re-exported libraries and individually re-exported symbols. None if absent."""
        if name in self.cache:
            return self.cache[name]
        path = self.resolve(name)
        if path is None:
            self.cache[name] = None
            return None
        self.cache[name] = set()               # cycle guard
        try:
            m = hb_macho.load(path)
        except (ValueError, OSError):
            self.cache[name] = None
            return None
        own, re = m.exported()
        out = set(own)
        if depth < 8:
            for d in m.dylibs:
                if d['kind'] == 'reexport':
                    out |= self.exports(d['name'], depth + 1) or set()
            for sym, (ordinal, target) in re.items():
                if 0 < ordinal <= len(m.dylibs):
                    if target in (self.exports(m.dylibs[ordinal - 1]['name'], depth + 1) or set()):
                        out.add(sym)
        self.cache[name] = out
        return out


def short(lib):
    """/usr/lib/libz.1.dylib -> libz.1; a framework binary -> Name.framework."""
    if '.framework/' in lib:
        return lib.split('.framework/')[0].rsplit('/', 1)[1] + '.framework'
    if '/' not in lib:
        return lib
    return lib.rsplit('/', 1)[1].replace('.dylib', '')


def scan_keg(keg):
    """Yield (relative path, MachO) for every Mach-O file of an unpacked bottle."""
    for dirpath, _dirs, files in os.walk(keg):
        for f in files:
            p = Path(dirpath) / f
            if p.is_symlink() or f.endswith(('.a', '.o')):
                continue
            try:
                with open(p, 'rb') as fh:
                    head = fh.read(8)
                if not hb_macho.is_macho(head):
                    continue
                m = hb_macho.load(p)
            except (ValueError, OSError, IndexError):
                continue
            if m.filetype in (2, 6, 8):
                yield str(p.relative_to(keg)), m


def shebangs(keg):
    """Interpreters outside the prefix that scripts in bin/ and sbin/ name."""
    out = collections.Counter()
    for sub in ('bin', 'sbin'):
        for version_dir in keg.iterdir():
            d = version_dir / sub
            if not d.is_dir():
                continue
            for p in d.iterdir():
                try:
                    with open(p, 'rb') as fh:
                        line = fh.readline(200)
                except OSError:
                    continue
                if line.startswith(b'#!'):
                    interp = line[2:].decode('utf-8', 'replace').split()
                    if interp:
                        name = interp[0] + (' ' + interp[1] if interp[0].endswith('/env') and len(interp) > 1 else '')
                        if not name.startswith(('@@HOMEBREW', '/opt/homebrew')):
                            out[name] += 1
    return dict(out)


def survey(tag, sysroot):
    closure = {r['name']: r for r in json.loads((RUNS / ('closure-%s.json' % tag)).read_text())}
    kegs = Kegs(tag, closure)
    formulae = {}
    for name, rec in sorted(closure.items()):
        keg = RUNS / 'kegs' / tag / name
        f = dict(version=rec.get('version'), requested=rec.get('requested', False), dependencies=rec['dependencies'],
                 uses_from_macos=rec.get('uses_from_macos', []), cellar=rec.get('cellar'), bottle_tag=rec.get('bottle_tag'),
                 machos=0, minos=set(), sdk=set(), system_libs={}, missing_libs={}, missing_syms={}, weak_missing={},
                 flat=0, fixups=collections.Counter(), signatures=collections.Counter(), shebangs={}, bytes=rec.get('bytes'))
        formulae[name] = f
        if 'error' in rec or not keg.is_dir():
            f['error'] = rec.get('error', 'not unpacked')
            continue
        f['shebangs'] = shebangs(keg)
        f['programs'] = {}
        for sub in ('bin', 'sbin'):
            d = keg / kegs.version.get(name, '') / sub
            for p in sorted(d.iterdir()) if d.is_dir() else []:
                got = kegs.host(kegs.logical(name, sub + '/' + p.name))
                if got is None:
                    continue
                with open(got[1], 'rb') as fh:
                    if not hb_macho.is_macho(fh.read(8)):
                        continue
                m = kegs.macho(got[1])
                if m is None or m.filetype != 2:
                    continue
                r = launch(kegs, sysroot, got[0], got[1])
                f['programs'][sub + '/' + p.name] = {k: sorted(v) for k, v in r.items() if v}
        for rel, m in scan_keg(keg):
            f['machos'] += 1
            if m.minos:
                f['minos'].add(m.minos)
                f['sdk'].add(m.sdk)
            f['fixups']['chained' if m.chained else ('dyld_info' if m.dyld_info else 'none')] += 1
            sig = m.signature()
            f['signatures']['unsigned' if sig is None else ('adhoc-linker' if sig.get('linker_signed') else ('adhoc' if sig.get('adhoc') else 'identity'))] += 1
            by_lib = collections.defaultdict(list)
            for sym, ordinal, weak in m.imports():
                if 0 < ordinal <= len(m.dylibs):
                    by_lib[ordinal - 1].append((sym, weak))
                elif ordinal == 254:
                    f['flat'] += 1
            flat = flat_missing(m, sysroot)
            if flat:
                f['missing_syms'].setdefault(FLAT, set()).update(flat)
            for i, d in enumerate(m.dylibs):
                lib = d['name']
                if not lib.startswith(SYSTEM_PREFIXES):
                    continue
                f['system_libs'].setdefault(lib, 0)
                f['system_libs'][lib] += 1
                have = sysroot.exports(lib)
                if have is None:
                    if d['kind'] == 'weak':
                        continue
                    f['missing_libs'].setdefault(lib, set()).update(s for s, _w in by_lib.get(i, []))
                    continue
                for sym, weak in by_lib.get(i, []):
                    if sym not in have:
                        (f['weak_missing'] if weak or d['kind'] == 'weak' else f['missing_syms']).setdefault(lib, set()).add(sym)
    return formulae


class Kegs:
    """The unpacked bottles as the prefix would hold them after a pour, for following a
    program's real library graph: logical path (/opt/homebrew/...) -> file on the host."""
    def __init__(self, tag, names):
        self.root = RUNS / 'kegs' / tag
        self.version = {}
        for n in names:
            d = self.root / n
            if d.is_dir():
                vs = sorted(x.name for x in d.iterdir() if x.is_dir())
                if vs:
                    self.version[n] = vs[-1]
        self.alias = {}
        try:
            for f in json.loads((RUNS / 'api/formula.json').read_text()):
                for al in f.get('aliases', []) + f.get('oldnames', []):
                    self.alias[al] = f['name']
        except OSError:
            pass
        self.cache = {}

    def logical(self, name, rel):
        return '/opt/homebrew/Cellar/%s/%s/%s' % (name, self.version[name], rel)

    def host(self, path):
        """Host file for a logical path, or None. Follows the opt/ links and symlinks inside
        the Cellar; /opt/homebrew/lib/<x> is looked up in every keg (the linked prefix)."""
        path = path.replace('@@HOMEBREW_CELLAR@@', '/opt/homebrew/Cellar').replace('@@HOMEBREW_PREFIX@@', '/opt/homebrew')
        path = os.path.normpath(path)
        for _ in range(20):
            parts = path.split('/')
            if path.startswith('/opt/homebrew/opt/') and len(parts) > 4:
                n = self.alias.get(parts[4], parts[4])
                if n not in self.version:
                    return None
                path = '/'.join(['/opt/homebrew/Cellar', n, self.version[n]] + parts[5:])
                continue
            if path.startswith('/opt/homebrew/Cellar/'):
                h = self.root / path[len('/opt/homebrew/Cellar/'):]
                # resolve symlinks component by component, staying inside the Cellar
                cur, comps = self.root, Path(path[len('/opt/homebrew/Cellar/'):]).parts
                moved = False
                for i, c in enumerate(comps):
                    cur = cur / c
                    if cur.is_symlink():
                        t = os.readlink(cur)
                        logical_dir = '/opt/homebrew/Cellar/' + '/'.join(comps[:i])
                        t = t.replace('@@HOMEBREW_CELLAR@@', '/opt/homebrew/Cellar').replace('@@HOMEBREW_PREFIX@@', '/opt/homebrew')
                        path = os.path.normpath(t if t.startswith('/') else logical_dir + '/' + t)
                        if comps[i + 1:]:
                            path += '/' + '/'.join(comps[i + 1:])
                        moved = True
                        break
                if moved:
                    continue
                return (path, h) if h.is_file() else None
            if path.startswith('/opt/homebrew/lib/'):
                rel = path[len('/opt/homebrew/'):]
                for n, v in self.version.items():
                    if (self.root / n / v / rel).exists():
                        path = '/opt/homebrew/Cellar/%s/%s/%s' % (n, v, rel)
                        break
                else:
                    return None
                continue
            return None
        return None

    def macho(self, hostpath):
        if hostpath not in self.cache:
            try:
                self.cache[hostpath] = hb_macho.load(hostpath)
            except (ValueError, OSError, IndexError):
                self.cache[hostpath] = None
        return self.cache[hostpath]


def launch(kegs, sysroot, exe_logical, exe_host):
    """What dyld meets when it starts this executable: the library graph is walked as dyld
    walks it. Returns dict(missing_libs, missing_syms, unresolved) (sets of strings)."""
    out = dict(missing_libs=set(), missing_syms=set(), unresolved=set())
    main = kegs.macho(exe_host)
    if main is None:
        return out
    todo, seen = [(exe_logical, exe_host, main.rpaths)], {str(exe_host)}
    while todo:
        logical, host, rpaths = todo.pop()
        m = kegs.macho(host)
        if m is None:
            continue
        by_lib = collections.defaultdict(list)
        for sym, ordinal, weak in m.imports():
            if 0 < ordinal <= len(m.dylibs):
                by_lib[ordinal - 1].append((sym, weak))
        out['missing_syms'] |= {'%s:%s' % (FLAT, sym) for sym in flat_missing(m, sysroot)}
        search = list(rpaths) + [r for r in m.rpaths if r not in rpaths]
        for i, d in enumerate(m.dylibs):
            lib = d['name']
            if lib.startswith(SYSTEM_PREFIXES):
                have = sysroot.exports(lib)
                if have is None:
                    if d['kind'] != 'weak':
                        out['missing_libs'].add(lib)
                    continue
                out['missing_syms'] |= {'%s:%s' % (short(lib), s) for s, w in by_lib.get(i, []) if s not in have and not w and d['kind'] != 'weak'}
                continue
            cands = []
            here = os.path.dirname(logical)
            if lib.startswith('@rpath/'):
                for r in search:
                    r = r.replace('@loader_path', here).replace('@executable_path', os.path.dirname(exe_logical))
                    cands.append(r + lib[len('@rpath'):])
            else:
                cands.append(lib.replace('@loader_path', here).replace('@executable_path', os.path.dirname(exe_logical)))
            found = next((f for f in (kegs.host(c) for c in cands) if f), None)
            if found is None:
                if d['kind'] != 'weak':
                    out['unresolved'].add(lib)
                continue
            if str(found[1]) not in seen:
                seen.add(str(found[1]))
                todo.append((found[0], found[1], search))
    return out


FLAT = 'any linked library (flat namespace)'


def flat_missing(m, sysroot):
    """Imports of an image linked without two-level namespace (Go programs): dyld takes each
    from whichever linked library has it. Checked when every linked library is a system
    library that exists; returns the symbols none of them exports."""
    if m.twolevel or not m.dylibs or m.filetype != 2:      # a flat plugin takes symbols from its host program
        return set()
    have = set()
    for d in m.dylibs:
        e = sysroot.exports(d['name']) if d['name'].startswith(SYSTEM_PREFIXES) else None
        if e is None:
            return set()
        have |= e
    return {s for s, _o, w in m.imports() if s not in have and not w}


def closure_of(name, formulae, seen=None):
    seen = set() if seen is None else seen
    if name in seen or name not in formulae:
        return seen
    seen.add(name)
    for d in formulae[name]['dependencies']:
        closure_of(d, formulae, seen)
    return seen


def verdict(f):
    if f.get('error'):
        return 'no bottle: ' + f['error']
    if not f['machos']:
        return 'no machine code'
    parts = []
    libs = [l for l in f['missing_libs'] if '.framework/' not in l]
    fw = [l for l in f['missing_libs'] if '.framework/' in l]
    if libs:
        parts.append('missing library ' + ', '.join(sorted(short(l) for l in libs)))
    if fw:
        parts.append('needs framework ' + ', '.join(sorted(short(l).replace('.framework', '') for l in fw)))
    for lib, syms in sorted(f['missing_syms'].items()):
        parts.append('missing %d symbols in %s' % (len(syms), short(lib)))
    return '; '.join(parts) or 'all system libraries and symbols present'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--tag', default='arm64_tahoe')
    p.add_argument('--sysroot', type=Path, required=True)
    p.add_argument('--name', required=True)
    a = p.parse_args()
    formulae = survey(a.tag, Sysroot(a.sysroot))
    out = RUNS / 'survey'
    out.mkdir(parents=True, exist_ok=True)

    def plain(o):
        if isinstance(o, (set, frozenset)):
            return sorted(o)
        if isinstance(o, collections.Counter):
            return dict(o)
        raise TypeError(type(o))
    (out / (a.name + '.json')).write_text(json.dumps(formulae, indent=1, default=plain, sort_keys=True) + '\n')

    requested = [n for n, f in formulae.items() if f['requested']]
    ok = lambda n: verdict(formulae[n]).startswith(('all system', 'no machine code'))   # noqa: E731

    def primary(n):
        """The programs a user means by the formula: bin/<name> when there is one."""
        progs = formulae[n].get('programs', {})
        base = n.split('@')[0]
        named = {k: v for k, v in progs.items() if k.split('/')[1] in (base, 'g' + base, {'ripgrep': 'rg', 'neovim': 'nvim', 'gnupg': 'gpg', 'imagemagick': 'magick', 'python': 'python3', 'openssl': 'openssl',
                                                                              'coreutils': 'gls', 'findutils': 'gfind', 'gnu-sed': 'gsed', 'ncurses': 'tput', 'llvm': 'clang', 'rust': 'rustc', 'sqlite': 'sqlite3',
                                                                              'readline': '', 'make': 'gmake'}.get(base, base))}
        return named or progs

    lines = ['# Static survey `%s`: bottles `%s` against system root `%s`' % (a.name, a.tag, a.sysroot), '',
             '%d formulae (%d requested + dependencies), %d Mach-O files.' % (len(formulae), len(requested), sum(f['machos'] for f in formulae.values())), '',
             '"Programs dyld can start" walks each program\'s real library graph (its own libraries, the Homebrew libraries it loads, the system libraries those load) and counts the programs for which every library exists and every non-weak imported symbol is exported. "Whole bottle" also counts loadable modules and libraries no program of the bottle loads at start.', '',
             '| Formula | Version | min OS | Main program | Programs dyld can start | What stops the main program | Whole bottle |', '|---|---|---|---|---|---|---|']
    main_ok = all_ok = 0
    for n in sorted(requested):
        f = formulae[n]
        progs = f.get('programs', {})
        prim = primary(n)
        good = sum(1 for v in progs.values() if not v)
        pgood = bool(prim) and all(not v for v in prim.values())
        libonly = not progs
        main_ok += pgood or (libonly and ok(n))
        all_ok += (good == len(progs)) and (bool(progs) or ok(n))
        stops = set()
        for v in prim.values():
            stops |= {short(l) for l in v.get('missing_libs', [])} | set(v.get('missing_syms', [])) | {'(unresolved) ' + u for u in v.get('unresolved', [])}
        stops = sorted(stops)
        lines.append('| %s | %s | %s | %s | %s | %s | %s |' % (
            n, f['version'], ' '.join(sorted(f['minos'])) or '-',
            'library only' if libonly else ('**starts**' if pgood else 'blocked'),
            '-' if libonly else '%d of %d' % (good, len(progs)),
            (', '.join(stops[:8]) + (' … (%d)' % len(stops) if len(stops) > 8 else '')) or '-', verdict(f)))
    lines += ['', 'Requested formulae whose main program dyld can start: **%d of %d**; with every program startable: **%d of %d**.' % (main_ok, len(requested), all_ok, len(requested)), '']

    # Ranking: each missing piece by the requested formulae with a program it stops.
    blocks = collections.defaultdict(set)
    progcount = collections.Counter()
    mainblocks = collections.defaultdict(set)
    for n in requested:
        prim = primary(n)
        for prog, v in formulae[n].get('programs', {}).items():
            for lib in v.get('missing_libs', []):
                blocks[('library', lib)].add(n)
                progcount[('library', lib)] += 1
                if prog in prim:
                    mainblocks[('library', lib)].add(n)
            for s in v.get('missing_syms', []):
                blocks[('symbol', s)].add(n)
                progcount[('symbol', s)] += 1
                if prog in prim:
                    mainblocks[('symbol', s)].add(n)
    wanted = collections.defaultdict(set)
    for f in formulae.values():
        for lib, syms in f['missing_libs'].items():
            wanted[lib] |= set(syms)
    lines += ['## Missing system libraries and frameworks, ranked by requested formulae whose main program they stop', '',
              '| Library | Main programs stopped | Formulae with any program stopped | Programs | Distinct symbols wanted (all bottles) | Main programs stopped: which |', '|---|---|---|---|---|---|']
    for key, who in sorted((k for k in blocks.items() if k[0][0] == 'library'), key=lambda kv: (-len(mainblocks[kv[0]]), -len(kv[1]), kv[0][1])):
        lines.append('| `%s` | %d | %d | %d | %d | %s |' % (key[1], len(mainblocks[key]), len(who), progcount[key], len(wanted[key[1]]), ' '.join(sorted(mainblocks[key]))))
    never = sorted(set(wanted) - {k[1] for k in blocks if k[0] == 'library'})
    lines += ['', 'Linked only by modules or libraries that no surveyed program loads at start (plugins, language extension modules): ' + ', '.join('`%s`' % short(l) for l in never), '']
    lines += ['## Missing symbols in libraries that exist, same ranking', '',
              '| Symbol | Main programs stopped | Formulae with any program stopped | Programs | Main programs stopped: which |', '|---|---|---|---|---|']
    for key, who in sorted((k for k in blocks.items() if k[0][0] == 'symbol'), key=lambda kv: (-len(mainblocks[kv[0]]), -len(kv[1]), kv[0][1])):
        lines.append('| `%s` | %d | %d | %d | %s |' % (key[1], len(mainblocks[key]), len(who), progcount[key], ' '.join(sorted(mainblocks[key]))))
    modsyms = collections.defaultdict(set)
    for n, f in formulae.items():
        for lib, syms in f['missing_syms'].items():
            for s in syms:
                if ('symbol', '%s:%s' % (short(lib), s)) not in blocks:
                    modsyms['%s:%s' % (short(lib), s)].add(n)
    lines += ['', 'Missing symbols wanted only by modules, libraries, or dependency-only formulae (%d): ' % len(modsyms) + ', '.join('`%s` (%s)' % (s, ' '.join(sorted(w))) for s, w in sorted(modsyms.items())), '']

    weak = collections.Counter()
    for f in formulae.values():
        for lib, syms in f['weak_missing'].items():
            for s in syms:
                weak[(short(lib), s)] += 1
    lines += ['', '## Weak imports that are absent (not fatal at launch; the program must test before calling)', '',
              ', '.join('`%s` (%s, %d)' % (s, l, c) for (l, s), c in sorted(weak.items(), key=lambda kv: -kv[1])[:80]) or 'none', '']
    she = collections.Counter()
    for f in formulae.values():
        for k, v in f['shebangs'].items():
            she[k] += v
    lines += ['## Interpreters outside the prefix named by scripts in bin/', '', ', '.join('`%s` (%d)' % kv for kv in she.most_common()) or 'none', '',
              '## Other facts', '',
              '- fixup formats: %s' % dict(sum((collections.Counter(f['fixups']) for f in formulae.values()), collections.Counter())),
              '- signatures: %s' % dict(sum((collections.Counter(f['signatures']) for f in formulae.values()), collections.Counter())),
              '- minimum OS values: %s' % dict(collections.Counter(v for f in formulae.values() for v in f['minos'])),
              '- flat-namespace (dynamic lookup) imports: %d' % sum(f['flat'] for f in formulae.values()), '']
    (out / (a.name + '.md')).write_text('\n'.join(lines))
    print('\n'.join(lines[:3]))
    print('main program startable: %d of %d; all programs: %d; report: %s' % (main_ok, len(requested), all_ok, out / (a.name + '.md')))


if __name__ == '__main__':
    main()
