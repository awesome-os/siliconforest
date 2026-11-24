#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Static import survey of a macOS application bundle against a SiliconForest system root.

For every Mach-O in the bundle (its arm64 slice) this records the libraries it links, its
minimum OS, fixup format and code signature, and every symbol it imports from each system
library (/usr/lib, /System/Library). Each system library is looked up in the system root(s)
and each imported symbol in that library's export trie, following re-exports. Libraries the
bundle carries itself (@rpath, @executable_path, @loader_path) are resolved inside the bundle.

Static analysis only: it predicts what dyld refuses at launch (missing library, missing
non-weak symbol) and what fails at first use (lazy or weak imports, dlopen/dlsym and
Objective-C class lookups by name are NOT seen here). The Mach-O reader and the system-root
resolver are the Homebrew stream's (integrations/siliconforest/homebrew/hb_macho.py,
hb_survey.py), imported, not copied.

  cocoa_survey.py --app runs/cocoa/firefox/x/Firefox/Firefox.app --sysroot DIR [--sysroot DIR]... --out runs/cocoa/firefox/survey
Several --sysroot directories are searched in order (an overlay: the Cocoa bundle before the
base image's root). Writes <out>.json and <out>.md.
"""
import argparse
import collections
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'homebrew'))
import hb_macho   # noqa: E402
import hb_survey  # noqa: E402

SYSTEM_PREFIXES = ('/usr/lib/', '/System/Library/', '/Library/')


class Roots(hb_survey.Sysroot):
    """Several system roots searched in order, as one root: a library in the first root may
    re-export one that only a later root has (the Cocoa root over the base image)."""
    def __init__(self, roots):
        super().__init__(roots[0])
        self.all = [hb_survey.Sysroot(r) for r in roots]

    def resolve(self, name, depth=0):
        for r in self.all:
            p = r.resolve(name, depth)
            if p is not None:
                return p
        return None


def bind_classes(m):
    """{(ordinal, symbol): 'load' | 'lazy'} from LC_DYLD_INFO's bind opcodes: a symbol in the
    non-lazy (or weak) bind stream must resolve when the image is loaded, or dyld refuses the
    image; one that is only in the lazy stream is bound at its first call. Empty for images
    with chained fixups (every import binds at load there)."""
    import struct
    info = None
    for cmd, off, size in m.commands:
        if cmd in (hb_macho.LC_DYLD_INFO, hb_macho.LC_DYLD_INFO_ONLY):
            info = struct.unpack_from('<10I', m.data, off + 8)
    out = {}
    if info is None:
        return out
    for kind, (start, size) in (('load', (info[2], info[3])), ('lazy', (info[6], info[7]))):
        data = m.data[start:start + size]
        i, ordinal, symbol = 0, 0, ''
        while i < len(data):
            op, imm = data[i] & 0xf0, data[i] & 0x0f
            i += 1
            if op == 0x00:                                   # DONE (separates lazy entries)
                continue
            elif op == 0x10:
                ordinal = imm
            elif op == 0x20:
                ordinal, i = hb_macho.uleb(data, i)
            elif op == 0x30:
                ordinal = 0 if imm == 0 else (imm | 0xf0) - 256
            elif op == 0x40:
                end = data.index(b'\0', i)
                symbol = data[i:end].decode('utf-8', 'replace')
                i = end + 1
            elif op == 0x50:
                pass
            elif op == 0x60:
                _, i = hb_macho.uleb(data, i)                # addend (sleb, same length)
            elif op == 0x70:
                _, i = hb_macho.uleb(data, i)
            elif op == 0x80:
                _, i = hb_macho.uleb(data, i)
            elif op in (0x90, 0xb0):
                if kind == 'load' or (ordinal, symbol) not in out:
                    out[(ordinal, symbol)] = kind
            elif op == 0xa0:
                if kind == 'load' or (ordinal, symbol) not in out:
                    out[(ordinal, symbol)] = kind
                _, i = hb_macho.uleb(data, i)
            elif op == 0xc0:
                if kind == 'load' or (ordinal, symbol) not in out:
                    out[(ordinal, symbol)] = kind
                _, i = hb_macho.uleb(data, i)
                _, i = hb_macho.uleb(data, i)
            else:
                break
    return out


def machos(app):
    for p in sorted(Path(app).rglob('*')):
        if p.is_file() and not p.is_symlink():
            with open(p, 'rb') as f:
                if hb_macho.is_macho(f.read(8)):
                    yield p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--app', type=Path, required=True)
    ap.add_argument('--sysroot', type=Path, action='append', required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    roots = Roots(a.sysroot)
    images, by_lib = [], collections.defaultdict(lambda: dict(present=None, users=set(), symbols=set(), missing=set(), weak_missing=set()))
    for path in machos(a.app):
        try:
            m = hb_macho.load(path)
        except (ValueError, OSError) as e:
            images.append(dict(path=str(path.relative_to(a.app)), error=str(e)))
            continue
        rel = str(path.relative_to(a.app))
        sig = m.signature()
        rec = dict(path=rel, fat=m.fat, minos=m.minos, sdk=m.sdk, platform=m.platform, chained_fixups=m.chained,
                   signature=None if sig is None else dict(identifier=sig.get('identifier'), flags=sig.get('flags'),
                                                           adhoc=sig.get('adhoc'), cms=sig.get('cms'),
                                                           hash_types=[d['hash_type'] for d in sig.get('directories', [])],
                                                           page_shift=[d['page_shift'] for d in sig.get('directories', [])]),
                   libraries=[dict(name=d['name'], kind=d['kind']) for d in m.dylibs], missing_libraries=[],
                   missing_symbols={}, weak_missing_symbols={}, load_blocking={}, imports=0)
        binds = bind_classes(m)
        for d in m.dylibs:
            if d['name'].startswith(SYSTEM_PREFIXES):
                lib = by_lib[d['name']]
                lib['users'].add(rel)
                if lib['present'] is None:
                    lib['present'] = roots.exports(d['name']) is not None
                if not lib['present'] and d['kind'] != 'weak':
                    rec['missing_libraries'].append(d['name'])
        for sym, ordinal, weak in m.imports():
            if not 0 < ordinal <= len(m.dylibs):
                continue
            name = m.dylibs[ordinal - 1]['name']
            if not name.startswith(SYSTEM_PREFIXES):
                continue
            rec['imports'] += 1
            lib = by_lib[name]
            lib['symbols'].add(sym)
            exports = roots.exports(name)
            if exports is None or sym not in exports:
                (lib['weak_missing'] if weak else lib['missing']).add(sym)
                (rec['weak_missing_symbols'] if weak else rec['missing_symbols']).setdefault(name, []).append(sym)
                # Refused at load: any missing non-weak import of an image with chained fixups,
                # or one bound by the non-lazy stream of LC_DYLD_INFO.
                if not weak and (m.chained or binds.get((ordinal, sym), 'load') == 'load'):
                    rec['load_blocking'].setdefault(name, []).append(sym)
        images.append(rec)
    libs = {n: dict(present=bool(v['present']), users=sorted(v['users']), imported=len(v['symbols']),
                    missing=sorted(v['missing']), weak_missing=sorted(v['weak_missing'])) for n, v in sorted(by_lib.items())}
    total = sum(v['imported'] for v in libs.values())
    miss = sum(len(v['missing']) for v in libs.values())
    weak = sum(len(v['weak_missing']) for v in libs.values())
    summary = dict(app=str(a.app), sysroots=[str(s) for s in a.sysroot], images=len(images), system_libraries=len(libs),
                   libraries_absent=sorted(n for n, v in libs.items() if not v['present']),
                   imported_symbols=total, missing_symbols=miss, weak_missing_symbols=weak,
                   load_blocking={r['path']: r['load_blocking'] for r in images if r.get('load_blocking')})
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.with_suffix('.json').write_text(json.dumps(dict(summary=summary, libraries=libs, images=images), indent=1) + '\n')
    lines = ['# Import survey: %s' % a.app.name, '',
             'System roots (searched in order): ' + ', '.join('`%s`' % s for s in a.sysroot), '',
             '%d Mach-O images, %d system libraries linked, %d distinct (library, symbol) imports: %d missing, %d weak imports missing.'
             % (len(images), len(libs), total, miss, weak), '',
             '| Library | In root | Imported | Missing | Weak missing |', '|---|---|---|---|---|']
    for n, v in libs.items():
        lines.append('| `%s` | %s | %d | %d | %d |' % (n, 'yes' if v['present'] else '**no**', v['imported'], len(v['missing']), len(v['weak_missing'])))
    lines += ['', '## Images', '', '| Image | min OS | fixups | signature | system imports | missing libs | missing symbols |', '|---|---|---|---|---|---|---|']
    for r in images:
        if 'error' in r:
            lines.append('| `%s` | error: %s | | | | | |' % (r['path'], r['error']))
            continue
        s = r['signature']
        sig = 'none' if s is None else ('ad-hoc' if s.get('adhoc') else ('Developer ID (CMS)' if s.get('cms') else 'signed')) + ' flags 0x%x' % (s.get('flags') or 0)
        lines.append('| `%s` | %s | %s | %s | %d | %d | %d |' % (r['path'], r['minos'], 'chained' if r['chained_fixups'] else 'dyld info', sig, r['imports'],
                                                               len(r['missing_libraries']), sum(len(v) for v in r['missing_symbols'].values())))
    lines += ['', '## Missing symbols by library', '']
    for n, v in libs.items():
        if v['missing'] or v['weak_missing']:
            lines += ['### `%s`%s' % (n, '' if v['present'] else ' (library absent)'), '']
            if v['missing']:
                lines += ['```', *v['missing'], '```', '']
            if v['weak_missing']:
                lines += ['weak:', '```', *v['weak_missing'], '```', '']
    a.out.with_suffix('.md').write_text('\n'.join(lines) + '\n')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
