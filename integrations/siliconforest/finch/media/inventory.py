#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare library exports and list exact CoreMedia/CoreVideo client imports."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('before', type=Path)
    ap.add_argument('candidate', type=Path)
    ap.add_argument('--client', type=Path, action='append', default=[])
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    macho = runpy.run_path(str(Path(__file__).resolve().parents[2]/'homebrew/hb_macho.py'))['MachO']
    def image(path):
        data = path.read_bytes()
        return macho(data), dict(path=str(path), sha256=hashlib.sha256(data).hexdigest())
    before, b = image(args.before)
    candidate, c = image(args.candidate)
    old, new = set(before.exported()[0]), set(candidate.exported()[0])
    result = dict(before=dict(b, exports=len(old)), candidate=dict(c, exports=len(new)),
                  retained=len(old & new), missing=sorted(old-new), added=sorted(new-old), clients=[])
    for path in args.client:
        obj, receipt = image(path)
        imports = []
        for sym, ordinal, weak in obj.imports():
            library = obj.dylibs[ordinal-1]['name'] if 1 <= ordinal <= len(obj.dylibs) else str(ordinal)
            if sym.startswith(('_CM', '_CV', '_kCM', '_kCV')) or any(n+'.framework/' in library for n in ('CoreMedia', 'CoreVideo')):
                imports.append(dict(symbol=sym, library=library, weak=weak))
        result['clients'].append(dict(receipt, imports=imports))
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f'Exports: before {len(old)}, candidate {len(new)}, retained {len(old & new)}, missing {len(old-new)}')


if __name__ == '__main__':
    main()
