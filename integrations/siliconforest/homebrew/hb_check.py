#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Static check of any Mach-O files (not only bottles) against a system root: what each
links, its minimum OS, signature kind, and every non-weak import the root does not export.
Same analysis and same limits as hb_survey.py.

  hb_check.py --sysroot runs/homebrew/sysroot/p4 FILE...
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hb_macho  # noqa: E402
import hb_survey  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--sysroot', type=Path, required=True)
    p.add_argument('files', nargs='+', type=Path)
    a = p.parse_args()
    root = hb_survey.Sysroot(a.sysroot)
    for f in a.files:
        try:
            m = hb_macho.load(f)
        except (ValueError, OSError, IndexError) as e:
            print('%s: not a Mach-O (%s)' % (f.name, e))
            continue
        sig = m.signature() or {}
        kind = 'unsigned' if not sig else ('ad-hoc' if sig.get('adhoc') else 'identity-signed (%s)' % sig.get('identifier'))
        imports = m.imports()
        print('%s: %s, %d bytes, min OS %s, SDK %s, %s, %s, %s, %d imports' % (
            f.name, hb_macho.FILETYPES.get(m.filetype), f.stat().st_size, m.minos, m.sdk, 'chained fixups' if m.chained else 'dyld-info binds',
            'two-level' if m.twolevel else 'flat namespace', kind, len(imports)))
        by = {}
        for s, o, w in imports:
            if 0 < o <= len(m.dylibs):
                by.setdefault(o - 1, []).append((s, w))
        for i, d in enumerate(m.dylibs):
            have = root.exports(d['name']) if d['name'].startswith(hb_survey.SYSTEM_PREFIXES) else None
            syms = by.get(i, [])
            if not d['name'].startswith(hb_survey.SYSTEM_PREFIXES):
                state = 'not a system path'
            elif have is None:
                state = 'ABSENT%s' % (' (weak link: allowed)' if d['kind'] == 'weak' else '')
            else:
                missing = sorted(s for s, w in syms if s not in have and not w)
                weak = sorted(s for s, w in syms if s not in have and w)
                state = 'present, %d of %d imports missing' % (len(missing), len(syms))
                if missing:
                    state += ': ' + ' '.join(missing[:40]) + (' …' if len(missing) > 40 else '')
                if weak:
                    state += ' | weak and absent: ' + ' '.join(weak[:12])
            print('    %-9s %s: %s' % (d['kind'], d['name'], state))
        flat = hb_survey.flat_missing(m, root)
        if flat:
            print('    flat-namespace imports no linked library has: ' + ' '.join(sorted(flat)))


if __name__ == '__main__':
    main()
