#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Gap scanner: which libraries and symbols do macOS programs ask for that a SiliconForest
root does not provide?

Input is a copy of a root as a directory (usr/lib, System/Library, opt/homebrew ... as on
the machine; symbolic links are followed inside it). For every Mach-O looked at:
  * each LC_LOAD_DYLIB (and weak/re-export/upward) is resolved as dyld resolves it
    (absolute path, @rpath, @loader_path, @executable_path); a library that is not there is
    a gap;
  * each undefined symbol is checked against the library its two-level namespace ordinal
    binds it to: that library's export trie, the libraries it re-exports (recursively) and
    its individually re-exported symbols. Flat-namespace images (Go) are checked against
    all their libraries together.
Weak imports and weak libraries are counted apart: dyld starts without them.

Two views are reported:
  direct    the image itself has the gap;
  programs  an executable in a formula's bin/ or sbin/ reaches the gap through its load
            closure (itself and every library dyld would load for it), i.e. the program
            does not start. Images with chained fixups bind everything at load; images with
            dyld-info opcodes may bind functions lazily, so a missing function there kills
            the program only when called (column "lazy").

Static analysis only: it predicts dyld's verdict, not behaviour; dlopen()ed names are not seen.

  sf_gapscan.py --root runs/sf-gaps/m1root --brew --name m1-before
  sf_gapscan.py --root R --system                 # the root's own frameworks, libraries, programs
  sf_gapscan.py --root R --binary /opt/homebrew/bin/vim [--binary ...]
  sf_gapscan.py --root R --overlay DIR ...        # DIR laid over the root (a candidate fix), searched first
  sf_gapscan.py --root R --kegs runs/homebrew/kegs/arm64_tahoe --brew --name wide
                                                  # unpacked bottles (hb_fetch.py) as the prefix: a formula
                                                  # set wider than what a machine has installed
  sf_gapscan.py --root R --overlay DIR --app private/.../Terminal.app=/Applications/Utilities/Terminal.app --name terminal
                                                  # an application bundle that is not in the root: mounted at the
                                                  # logical path, its executable (Info.plist) scanned with its load
                                                  # closure; a per-library table of every import, the Objective-C
                                                  # classes it imports, the selectors it sends that nothing in the
                                                  # closure implements, and what its chained fixups ask of dyld
                                                  # (arm64e: authenticated binds and rebases). --arch picks the slice.
writes runs/sf-gaps/<name>.json (everything) and <name>.md (the ranked summary).
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
SYSTEM_DIRS = ('usr/lib', 'System/Library', 'bin', 'sbin', 'usr/bin', 'usr/sbin', 'usr/libexec', 'libexec', 'usr/local/bin', 'usr/local/libexec')


class Root:
    """A root directory, optionally with overlays searched first; logical path -> host file.
    A mount puts a host directory at a logical path (unpacked bottles as /opt/homebrew/Cellar)."""
    def __init__(self, root, overlays=(), mounts=(), under=()):
        self.roots = [(Path(m[1]), m[0].rstrip('/')) for m in mounts] + [(Path(o), '') for o in overlays] + [(Path(root), '')] + [(Path(u), '') for u in under]
        self.machos = {}
        self.exp = {}
        self.prefer = None          # slice of a universal file: 'arm64e' or 'arm64'

    def resolve(self, name, depth=0):
        """Host file for the logical absolute path `name`, following links inside the root."""
        name = os.path.normpath(name)
        for root, prefix in self.roots:
            if prefix and not name.startswith(prefix + '/'):
                continue
            parts = Path(name[len(prefix):].lstrip('/')).parts
            cur = root
            for i, part in enumerate(parts):
                cur = cur / part
                if cur.is_symlink():
                    if depth > 30:
                        return None
                    target = os.readlink(cur).replace('@@HOMEBREW_CELLAR@@', '/opt/homebrew/Cellar').replace('@@HOMEBREW_PREFIX@@', '/opt/homebrew')
                    base = target if target.startswith('/') else os.path.join(prefix + '/', *parts[:i], target)
                    rest = '/'.join(parts[i + 1:])
                    return self.resolve(base + ('/' + rest if rest else ''), depth + 1)
                if not cur.exists():
                    break
            else:
                if cur.is_file():
                    return cur
        return None

    def macho(self, host):
        host = str(host)
        if host not in self.machos:
            try:
                with open(host, 'rb') as fh:
                    head = fh.read(8)
                self.machos[host] = hb_macho.load(host, self.prefer) if hb_macho.is_macho(head) else None
            except (ValueError, OSError, IndexError, Exception):   # a damaged or foreign file is not a Mach-O for us
                self.machos[host] = None
        return self.machos[host]

    def find(self, lib, loader_logical, exe_logical, rpaths):
        """(logical, host) of the library an image names, or None."""
        here = os.path.dirname(loader_logical)
        exed = os.path.dirname(exe_logical)
        # a bottle that was unpacked but not poured still carries Homebrew's placeholders
        lib = lib.replace('@@HOMEBREW_CELLAR@@', '/opt/homebrew/Cellar').replace('@@HOMEBREW_PREFIX@@', '/opt/homebrew')
        rpaths = [r.replace('@@HOMEBREW_CELLAR@@', '/opt/homebrew/Cellar').replace('@@HOMEBREW_PREFIX@@', '/opt/homebrew') for r in rpaths]
        if lib.startswith('@rpath/'):
            cands = [r.replace('@loader_path', here).replace('@executable_path', exed) + lib[len('@rpath'):] for r in rpaths]
        else:
            cands = [lib.replace('@loader_path', here).replace('@executable_path', exed)]
        # dyld's fallback when the path as given has nothing: the leaf name in the default
        # DYLD_FALLBACK_LIBRARY_PATH, a framework's partial path in DYLD_FALLBACK_FRAMEWORK_PATH
        if '.framework/' in lib:
            partial = lib[lib.rfind('/', 0, lib.index('.framework/')) + 1:]
            cands += [d + '/' + partial for d in ('/Library/Frameworks', '/System/Library/Frameworks')]
        else:
            cands += [d + '/' + lib.rsplit('/', 1)[-1] for d in ('/usr/local/lib', '/lib', '/usr/lib')]
        for c in cands:
            if not c.startswith('/'):
                continue
            h = self.resolve(c)
            if h is not None and self.macho(h) is not None:
                return os.path.normpath(c), h
        return None

    def exports(self, logical, host, depth=0):
        """Every symbol a client of this library can bind (own, re-exported libraries,
        individually re-exported symbols)."""
        key = str(host)
        if key in self.exp:
            return self.exp[key]
        self.exp[key] = set()                    # cycle guard
        m = self.macho(host)
        own, re = m.exported()
        out = set(own)
        if depth < 10:
            for i, d in enumerate(m.dylibs):
                if d['kind'] != 'reexport' and not any(o == i + 1 for o, _n in re.values()):
                    continue
                got = self.find(d['name'], logical, logical, m.rpaths)
                if got is None:
                    continue
                sub = self.exports(got[0], got[1], depth + 1)
                if d['kind'] == 'reexport':
                    out |= sub
                for sym, (ordinal, target) in re.items():
                    if ordinal == i + 1 and target in sub:
                        out.add(sym)
        self.exp[key] = out
        return out


def image_gaps(root, logical, host, exe_logical, rpaths):
    """Gaps of one image. Returns (gaps, deps): gaps = set of (kind, library, symbol or '',
    weak); deps = [(logical, host)] of the libraries found."""
    m = root.macho(host)
    gaps, deps = set(), []
    by_lib = collections.defaultdict(list)
    flat = []
    for sym, ordinal, weak in m.imports():
        if 0 < ordinal <= len(m.dylibs):
            by_lib[ordinal - 1].append((sym, weak))
        elif ordinal == 254 or not m.twolevel:
            flat.append((sym, weak))
    search = list(rpaths) + [r for r in m.rpaths if r not in rpaths]
    have_all, all_found = set(), True
    for i, d in enumerate(m.dylibs):
        got = root.find(d['name'], logical, exe_logical, search)
        if got is None:
            all_found = False
            gaps.add(('library', d['name'], '', d['kind'] == 'weak'))
            for sym, weak in by_lib.get(i, []):
                gaps.add(('symbol-of-missing-library', d['name'], sym, weak or d['kind'] == 'weak'))
            continue
        deps.append(got)
        have = root.exports(got[0], got[1])
        have_all |= have
        for sym, weak in by_lib.get(i, []):
            if sym not in have:
                gaps.add(('symbol', d['name'], sym, weak or d['kind'] == 'weak'))
    if flat and all_found and m.filetype == 2 and not m.twolevel:
        for sym, weak in flat:
            if sym not in have_all:
                gaps.add(('symbol', 'any linked library (flat namespace)', sym, weak))
    return gaps, deps, search


def closure_gaps(root, logical, host):
    """Gaps of a program's whole load closure: {gap: [image that has it, ...]}."""
    m = root.macho(host)
    out = collections.defaultdict(list)
    todo, seen = [(logical, host, list(m.rpaths))], {str(host)}
    while todo:
        lg, h, rp = todo.pop()
        gaps, deps, search = image_gaps(root, lg, h, logical, rp)
        lazy = not root.macho(h).chained
        for g in gaps:
            out[g].append((lg, lazy))
        for d in deps:
            if str(d[1]) not in seen:
                seen.add(str(d[1]))
                todo.append((d[0], d[1], search))
    return out


def closure_files(root, logical, host):
    """[(logical, host)] of a program and every library dyld loads for it."""
    m = root.macho(host)
    out, todo, seen = [], [(logical, host, list(m.rpaths))], {str(host)}
    while todo:
        lg, h, rp = todo.pop()
        out.append((lg, h))
        _gaps, deps, search = image_gaps(root, lg, h, logical, rp)
        for d in deps:
            if str(d[1]) not in seen:
                seen.add(str(d[1]))
                todo.append((d[0], d[1], search))
    return out


def app_report(root, logical, host):
    """The table for one application executable: every import by library, what is missing,
    Objective-C classes and selectors, fixup facts. Returns (markdown, json)."""
    m = root.macho(host)
    files = closure_files(root, logical, host)
    cg = closure_gaps(root, logical, host)
    own = {g for g, where in cg.items() if any(i == logical for i, _l in where)}
    st = m.fixup_stats() if m.fixups else None
    per = collections.OrderedDict()
    for i, d in enumerate(m.dylibs):
        per[i + 1] = dict(name=d['name'], kind=d['kind'], present=root.find(d['name'], logical, logical, m.rpaths) is not None,
                          n=0, weak=0, classes=0, swift=0, missing=[], missing_weak=[])
    imps = m.imports()
    for idx, (sym, o, weak) in enumerate(imps):
        e = per.get(o)
        if e is None:
            continue
        e['n'] += 1
        e['weak'] += weak
        e['classes'] += sym.startswith(('_OBJC_CLASS_$_', '_OBJC_METACLASS_$_'))
        e['swift'] += sym.startswith(('_$s', '_swift_'))
    for kind, lib, sym, weak in own:
        if not sym:
            continue
        for e in per.values():
            if e['name'] == lib:
                (e['missing_weak'] if weak or e['kind'] == 'weak' else e['missing']).append(sym)
    # Objective-C: selectors sent by the application that neither it nor any image of the closure implements
    sent = m.objc_selrefs()
    impl, classes_have = set(), set()
    for lg, h in files:
        cl, cats = root.macho(h).objc_defined()
        for c, ms in cl.items():
            classes_have.add(c)
            impl.update(x[1:] for x in ms)
        impl.update(x[1:] for x in cats)
    nobody = sorted(sent - impl)
    cls_imp = sorted({s[len('_OBJC_CLASS_$_'):] for s, _o, _w in imps if s.startswith('_OBJC_CLASS_$_')})
    cls_missing = [c for c in cls_imp if c not in classes_have]
    tot = sum(e['n'] for e in per.values())
    miss = sum(len(e['missing']) for e in per.values())
    out = ['## %s' % logical, '',
           'Slices: %s; scanned: %s (ptrauth ABI byte 0x%02x), %s, minos %s, sdk %s. %d load commands for libraries (%d weak), %d of them not in the root.'
           % (', '.join(m.slices) or 'thin', 'arm64e' if m.arm64e else 'arm64', m.pac_abi, 'chained fixups' if m.chained else 'dyld-info opcodes',
              m.minos, m.sdk, len(m.dylibs), sum(d['kind'] == 'weak' for d in m.dylibs), sum(not e['present'] for e in per.values())), '']
    if st:
        keys = collections.Counter()
        for o, ks in st['auth_imports'].items():
            for k in ks:
                keys[('IA', 'IB', 'DA', 'DB')[k[0]] + (' address-diversified' if k[1] else '')] += 1
        out += ['Fixups dyld must apply: pointer format %s; %d rebases (%d authenticated), %d binds (%d authenticated) of %d imports; '
                '%d imports are bound authenticated at least once (%s), %d plain at least once.'
                % (', '.join(map(str, sorted(st['formats']))), st['rebase'] + st['auth_rebase'], st['auth_rebase'], st['bind'] + st['auth_bind'],
                   st['auth_bind'], len(imps), len(st['auth_imports']), ', '.join('%s: %d' % kv for kv in sorted(keys.items())), len(st['plain_imports'])), '']
    out += ['**%d imports, %d missing (not counting weak ones: %d).** Objective-C: %d classes imported, %d not defined by any image of the closure; '
            '%d selectors sent, %d of them implemented by no image of the closure (a message with one of those cannot be answered; '
            'a selector that some class implements may still be missing on the class it is sent to: that is not visible statically).'
            % (tot, miss, sum(len(e['missing_weak']) for e in per.values()), len(cls_imp), len(cls_missing), len(sent), len(nobody)), '',
            '| Library | In the root | Imports | of them classes | Swift | Missing | Missing (weak) |', '|---|---|---|---|---|---|---|']
    for e in sorted(per.values(), key=lambda e: -e['n']):
        out.append('| %s%s | %s | %d | %d | %d | %d | %d |' % (short(e['name']), ' (weak)' if e['kind'] == 'weak' else '', 'yes' if e['present'] else '**no**',
                                                         e['n'], e['classes'], e['swift'], len(e['missing']), len(e['missing_weak'])))
    out += ['', '### Missing symbols by library', '']
    for e in sorted(per.values(), key=lambda e: -len(e['missing'])):
        if e['missing'] or e['missing_weak']:
            out.append('- **%s**%s: %s%s' % (short(e['name']), '' if e['present'] else ' (library absent)', ', '.join('`%s`' % x for x in sorted(e['missing'])),
                                           ('; weak: ' + ', '.join('`%s`' % x for x in sorted(e['missing_weak']))) if e['missing_weak'] else ''))
    deeper = sorted({(short(lib), sym, os.path.basename(where[0][0])) for (kind, lib, sym, weak), where in cg.items()
                     if (kind, lib, sym, weak) not in own and not weak and kind != 'symbol-of-missing-library'})
    if deeper:
        out += ['', '### Gaps in the libraries it loads (not its own imports)', ''] + ['- %s `%s` (in %s)' % d for d in deeper]
    out += ['', '### Objective-C classes imported and not defined in the closure (%d)' % len(cls_missing), '', ', '.join('`%s`' % c for c in cls_missing) or 'none',
            '', '### Selectors sent that no image of the closure implements (%d of %d)' % (len(nobody), len(sent)), '', ', '.join('`%s`' % x for x in nobody) or 'none', '']
    js = dict(libraries=[dict(e, name=short(e['name'])) for e in per.values()], classes_missing=cls_missing, selectors_unimplemented=nobody,
              selectors_sent=len(sent), imports=tot, missing=miss, closure=[lg for lg, _h in files])
    if st:
        js['fixups'] = dict(formats=sorted(st['formats']), rebase=st['rebase'], auth_rebase=st['auth_rebase'], bind=st['bind'], auth_bind=st['auth_bind'])
    return '\n'.join(out) + '\n', js


def walk(root, sub):
    """(logical, host) of every Mach-O executable, dylib or bundle under root/<sub>."""
    seen = set()
    for r, prefix in root.roots:
        if prefix and not ('/' + sub + '/').startswith(prefix + '/'):
            continue
        top = r / ('/' + sub)[len(prefix):].lstrip('/')
        for dirpath, _dirs, files in os.walk(top):
            for f in files:
                p = Path(dirpath) / f
                if p.is_symlink() or f.endswith(('.a', '.o')):
                    continue
                logical = prefix + '/' + str(p.relative_to(r))
                if logical in seen:
                    continue
                m = root.macho(p)
                if m is not None and m.filetype in (2, 6, 8):
                    seen.add(logical)
                    yield logical, p


def shebangs(root, sub):
    """{interpreter outside the prefix: {formula: count}} over every executable script under root/<sub>."""
    out = collections.defaultdict(lambda: collections.defaultdict(int))
    for r, prefix in root.roots:
        if prefix and not ('/' + sub + '/').startswith(prefix + '/'):
            continue
        for dirpath, _dirs, files in os.walk(r / ('/' + sub)[len(prefix):].lstrip('/')):
            for f in files:
                p = Path(dirpath) / f
                if p.is_symlink() or not os.access(p, os.X_OK):
                    continue
                try:
                    with open(p, 'rb') as fh:
                        line = fh.readline(200)
                except OSError:
                    continue
                if not line.startswith(b'#!'):
                    continue
                words = line[2:].decode('utf-8', 'replace').split()
                if not words or words[0].startswith(('/opt/homebrew', '@@HOMEBREW')):
                    continue
                interp = words[0]
                if interp.endswith('/env') and len(words) > 1:
                    interp += ' ' + next((w for w in words[1:] if not w.startswith('-') and '=' not in w), '')
                out[interp][owner(prefix + '/' + str(p.relative_to(r)))] += 1
    return out


def shebang_table(root, sb):
    rows = []
    for interp, owners in sb.items():
        name = interp.split(' ', 1)[1] if ' ' in interp else interp
        if ' ' in interp:      # env <name>: on the PATH a login has
            have = any(root.resolve(d + '/' + name) for d in ('/opt/homebrew/bin', '/usr/local/bin', '/usr/bin', '/bin', '/usr/sbin', '/sbin'))
        else:
            have = root.resolve(name) is not None
        rows.append((have, -len(owners), interp, owners))
    rows.sort()
    out = ['### Interpreters that scripts in the prefix name (outside the prefix)', '',
           '| Interpreter | In the root | Formulae | Scripts |', '|---|---|---|---|']
    for have, _n, interp, owners in rows:
        o = sorted(owners)
        out.append('| `%s` | %s | %d: %s | %d |' % (interp, 'yes' if have else '**no**', len(o), ', '.join(o[:10]) + (' …' if len(o) > 10 else ''), sum(owners.values())))
    return '\n'.join(out) + '\n'


def owner(logical):
    """The formula (or system component) an image belongs to."""
    parts = logical.split('/')
    if logical.startswith('/opt/homebrew/Cellar/') and len(parts) > 4:
        return parts[4]
    if '.framework/' in logical:
        return logical.split('.framework/')[0].rsplit('/', 1)[1] + '.framework'
    return logical


def short(lib):
    if '.framework/' in lib:
        return lib.split('.framework/')[0].rsplit('/', 1)[1] + '.framework'
    return lib.rsplit('/', 1)[-1]


def is_program(logical):
    parts = logical.split('/')
    return logical.startswith('/opt/homebrew/Cellar/') and len(parts) == 8 and parts[6] in ('bin', 'sbin')


def scan(root, images, programs):
    """images: [(logical, host)] checked directly; programs: the subset checked with their
    load closure. Returns the result dictionary."""
    direct = collections.defaultdict(lambda: collections.defaultdict(set))    # gap -> owner -> {image}
    stopped = collections.defaultdict(lambda: collections.defaultdict(set))   # gap -> owner -> {program}
    lazy_only = collections.defaultdict(lambda: True)
    per_program = {}
    n = 0
    for logical, host in images:
        n += 1
        gaps, _deps, _s = image_gaps(root, logical, host, logical, list(root.macho(host).rpaths))
        for g in gaps:
            direct[g][owner(logical)].add(logical)
    for logical, host in programs:
        cg = closure_gaps(root, logical, host)
        hard = sorted({(k, short(lib), sym) for (k, lib, sym, weak) in cg if not weak and k != 'symbol-of-missing-library'})
        per_program[logical] = ['%s %s%s' % (k, lib, ':' + sym if sym else '') for k, lib, sym in hard]
        for g, where in cg.items():
            stopped[g][owner(logical)].add(logical)
            if any(not lz for _i, lz in where) or g[0] == 'library':
                lazy_only[g] = False
            else:
                lazy_only[g]
    return dict(images=n, programs=len(programs), direct=direct, stopped=stopped, lazy_only=lazy_only, per_program=per_program)


def table(res, title):
    direct, stopped = res['direct'], res['stopped']
    keys = set(direct) | set(stopped)
    libs = collections.defaultdict(lambda: dict(direct=set(), stopped=set(), images=set(), syms=set(), weak=True, lazy=True, kind='symbol'))
    for g in keys:
        kind, lib, sym, weak = g
        if kind == 'symbol-of-missing-library':
            e = libs[(short(lib), 'library')]
            if not weak:
                e['syms'].add(sym)
            continue
        e = libs[(short(lib), 'library' if kind == 'library' else 'symbol')]
        e['kind'] = kind
        if weak:
            if e['weak'] and not e['direct']:
                for o, im in direct.get(g, {}).items():
                    e.setdefault('weak_owners', set()).add(o)
            continue
        e['weak'] = False
        if sym:
            e['syms'].add(sym)
        for o, im in direct.get(g, {}).items():
            e['direct'].add(o)
            e['images'] |= im
        for o, pr in stopped.get(g, {}).items():
            e['stopped'].add(o)
        if not res['lazy_only'].get(g, True):
            e['lazy'] = False
    rows = [(k, e) for k, e in libs.items() if not e['weak']]
    rows.sort(key=lambda r: (-len(r[1]['stopped']), -len(r[1]['direct']), r[0]))
    blocked = collections.defaultdict(set)
    for prog, gaps in res['per_program'].items():
        if gaps:
            blocked[owner(prog)].add(prog)
    out = ['## %s' % title, '',
           '%d Mach-O images checked directly, %d programs with their load closure. '
           '**%d owners (formulae) have at least one program that does not start; %d programs in all.**'
           % (res['images'], res['programs'], len(blocked), sum(len(v) for v in blocked.values())), '',
           '| # | Missing | Kind | Symbols | Formulae with a stopped program | Formulae with the gap in any image | Binding |',
           '|---|---|---|---|---|---|---|']
    for i, ((lib, _k), e) in enumerate(rows, 1):
        syms = sorted(e['syms'])
        shown = ', '.join('`%s`' % s for s in syms[:8]) + (' … (%d)' % len(syms) if len(syms) > 8 else '')
        st = sorted(e['stopped'])
        dr = sorted(e['direct'])
        out.append('| %d | %s | %s | %s | %d: %s | %d: %s | %s |' % (
            i, lib, 'library absent' if e['kind'] == 'library' else 'symbols absent', shown or '(none imported)',
            len(st), ', '.join(st[:12]) + (' …' if len(st) > 12 else ''),
            len(dr), ', '.join(dr[:12]) + (' …' if len(dr) > 12 else ''),
            'lazy only' if e['kind'] != 'library' and e['lazy'] and st else ('at load' if st else '-')))
    out += ['', '### Programs that do not start, by formula', '', '| Formula | Programs | First gaps |', '|---|---|---|']
    for o in sorted(blocked):
        progs = sorted(blocked[o])
        g = res['per_program'][progs[0]]
        out.append('| %s | %d: %s | %s |' % (o, len(progs), ', '.join(os.path.basename(p) for p in progs[:6]) + (' …' if len(progs) > 6 else ''),
                                           '; '.join(g[:4]) + (' … (%d)' % len(g) if len(g) > 4 else '')))
    return '\n'.join(out) + '\n', blocked


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', required=True, help='a copy of the root as a directory')
    ap.add_argument('--overlay', action='append', default=[], help='a tree laid over the root (searched first); repeatable')
    ap.add_argument('--under', action='append', default=[], help='a tree laid under the root (searched last: what the root would get where it has nothing); repeatable')
    ap.add_argument('--brew', action='store_true', help='scan opt/homebrew/Cellar')
    ap.add_argument('--system', action='store_true', help="scan the root's own frameworks, libraries and programs")
    ap.add_argument('--binary', action='append', default=[], help='a logical path in the root, with its load closure; repeatable')
    ap.add_argument('--kegs', help='a directory of unpacked bottles (<formula>/<version>/...) to scan as the prefix instead of the root\'s')
    ap.add_argument('--closure', action='store_true', help='with --binary: list the files of the load closure (CLOSURE size path)')
    ap.add_argument('--app', action='append', default=[], help='<bundle on the host>[=<logical path>]: an application outside the root; repeatable')
    ap.add_argument('--arch', choices=('arm64e', 'arm64'), help='slice of a universal file (default: arm64, else arm64e)')
    ap.add_argument('--name', default='scan')
    ap.add_argument('--out', default=str(LAB / 'runs/sf-gaps'))
    a = ap.parse_args()
    overlays, mounts = list(a.overlay), []
    if a.kegs:
        # unpacked bottles as the prefix: the keg directory is /opt/homebrew/Cellar; a directory of
        # links gives opt/<formula> (its newest version) and lib/<leaf> (what `brew link` puts there)
        kegs = Path(a.kegs).resolve()
        mounts.append(('/opt/homebrew/Cellar', kegs))
        view = Path(a.out) / (a.name + '-prefix')
        if view.exists():
            import shutil
            shutil.rmtree(view)
        (view / 'opt/homebrew/opt').mkdir(parents=True)
        (view / 'opt/homebrew/lib').mkdir()
        for keg in sorted(kegs.iterdir()):
            versions = sorted(v.name for v in keg.iterdir() if v.is_dir())
            if not versions:
                continue
            os.symlink('/opt/homebrew/Cellar/%s/%s' % (keg.name, versions[-1]), view / 'opt/homebrew/opt' / keg.name)
            libdir = keg / versions[-1] / 'lib'
            for lib in (sorted(libdir.iterdir()) if libdir.is_dir() else []):
                if lib.name.endswith('.dylib') and not os.path.lexists(view / 'opt/homebrew/lib' / lib.name):
                    os.symlink('/opt/homebrew/Cellar/%s/%s/lib/%s' % (keg.name, versions[-1], lib.name), view / 'opt/homebrew/lib' / lib.name)
        overlays.insert(0, str(view))
    apps = []
    for spec in a.app:
        import plistlib
        hostdir, _, lg = spec.partition('=')
        hostdir = Path(hostdir).resolve()
        lg = (lg or '/Applications/' + hostdir.name).rstrip('/')
        with open(hostdir / 'Contents/Info.plist', 'rb') as fh:
            exe = plistlib.load(fh)['CFBundleExecutable']
        mounts.append((lg, hostdir))
        apps.append(lg + '/Contents/MacOS/' + exe)
    root = Root(a.root, overlays, mounts, a.under)
    root.prefer = a.arch
    md, js = [], {}
    for b in apps:
        h = root.resolve(b)
        if h is None or root.macho(h) is None:
            print('%s: not a Mach-O' % b)
            continue
        t, j = app_report(root, b, h)
        md.append(t)
        js.setdefault('apps', {})[b] = j
        print('%s: %d imports, %d missing; %d classes missing; %d of %d selectors implemented by nobody'
              % (b, j['imports'], j['missing'], len(j['classes_missing']), len(j['selectors_unimplemented']), j['selectors_sent']))
    if a.brew:
        images = list(walk(root, 'opt/homebrew/Cellar'))
        programs = [(lg, h) for lg, h in images if is_program(lg) and root.macho(h).filetype == 2]
        res = scan(root, images, programs)
        t, blocked = table(res, 'Homebrew prefix (/opt/homebrew/Cellar)')
        md.append(t)
        md.append(shebang_table(root, shebangs(root, 'opt/homebrew/Cellar')))
        js['brew'] = dict(per_program=res['per_program'], blocked={k: sorted(v) for k, v in blocked.items()},
                          formulae=sorted({owner(lg) for lg, _h in images}))
        print('brew: %d images, %d programs, %d formulae of %d with a program that does not start'
              % (res['images'], res['programs'], len(blocked), len(js['brew']['formulae'])))
    if a.system:
        images = [x for sub in SYSTEM_DIRS for x in walk(root, sub)]
        programs = [(lg, h) for lg, h in images if root.macho(h).filetype == 2]
        res = scan(root, images, programs)
        t, blocked = table(res, "The root's own frameworks, libraries and programs")
        md.append(t)
        js['system'] = dict(per_program={k: v for k, v in res['per_program'].items() if v})
        print('system: %d images, %d programs, %d that do not start' % (res['images'], res['programs'], len(blocked)))
    for b in a.binary:
        h = root.resolve(b)
        if h is None or root.macho(h) is None:
            print('%s: not a Mach-O in the root' % b)
            continue
        cg = closure_gaps(root, b, h)
        lines = ['## %s' % b, '']
        if a.closure:
            for lg, hp in sorted(closure_files(root, b, h)):
                print('CLOSURE %9d %s' % (os.path.getsize(hp), lg))
        for (kind, lib, sym, weak), where in sorted(cg.items()):
            if kind == 'symbol-of-missing-library':
                continue
            lines.append('- %s%s `%s`%s (in %s)' % ('weak ' if weak else '', kind, short(lib), ' `%s`' % sym if sym else '',
                                                     ', '.join(sorted({os.path.basename(i) for i, _l in where}))))
        if len(lines) == 2:
            lines.append('no gap: every library is there and every import resolves')
        md.append('\n'.join(lines) + '\n')
        js.setdefault('binaries', {})[b] = sorted('%s %s %s%s' % (k, short(lib), sym, ' (weak)' if w else '') for (k, lib, sym, w) in cg)
        print('\n'.join(lines))
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / (a.name + '.md')).write_text('\n'.join(md))
    (out / (a.name + '.json')).write_text(json.dumps(js, indent=1, sort_keys=True))
    print('wrote %s' % (out / (a.name + '.md')))


if __name__ == '__main__':
    main()
