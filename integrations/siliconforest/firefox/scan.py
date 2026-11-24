#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Scan every arm64 Mach-O in Firefox, including dlopened XUL and all helpers."""
import argparse
import json
from pathlib import Path
import sys

LAB = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(LAB / 'integrations/siliconforest/homebrew'))
import sf_gapscan as scanner


class FirefoxRoot(scanner.Root):
    """Include the main executable's runpath for separately scanned dlopened images."""
    def find(self, lib, loader_logical, exe_logical, rpaths):
        if lib.startswith('@rpath/'):
            # firefox has LC_RPATH @executable_path. XUL and sibling dylibs
            # inherit it at runtime; scanning XUL alone otherwise loses it.
            rpaths = list(rpaths) + ['/Applications/Firefox.app/Contents/MacOS']
        if '/' not in lib:
            logical = '/Applications/Firefox.app/Contents/MacOS/' + lib
            path = self.resolve(logical)
            if path is not None and self.macho(path) is not None:
                return logical, path
        return super().find(lib, loader_logical, exe_logical, rpaths)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, required=True)
    ap.add_argument('--bundle', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--under', type=Path, action='append', default=[])
    ap.add_argument('--overlay', type=Path, action='append', default=[])
    args = ap.parse_args()
    logical = '/Applications/Firefox.app'
    root = FirefoxRoot(args.root, overlays=args.overlay, mounts=[(logical, args.bundle)], under=args.under)
    root.prefer = 'arm64'
    reports, markdown = {}, []
    for path in sorted(args.bundle.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open('rb') as f:
            if not scanner.hb_macho.is_macho(f.read(8)):
                continue
        name = logical + '/' + str(path.relative_to(args.bundle))
        text, report = scanner.app_report(root, name, path)
        gaps = scanner.closure_gaps(root, name, path)
        report['closure_gaps'] = [dict(kind=k, library=l, symbol=s, weak=w,
                                      images=sorted({i for i, _ in where}))
                                  for (k, l, s, w), where in sorted(gaps.items())]
        reports[name] = report
        markdown.append(text)
        print(name, 'imports', report['imports'], 'missing', report['missing'],
              'closure gaps', sum(not g['weak'] for g in report['closure_gaps']))
    if not reports:
        raise RuntimeError('No Mach-O images found')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'scan.json').write_text(json.dumps(reports, indent=2) + '\n')
    (args.out / 'scan.md').write_text('\n'.join(markdown))


if __name__ == '__main__':
    main()
