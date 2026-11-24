#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inventory a bottle's actual framework load commands and bound imports."""
import argparse
import json
from pathlib import Path
import runpy


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('images', type=Path, nargs='+')
    ap.add_argument('--framework', action='append', required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    macho = runpy.run_path(str(Path(__file__).resolve().parents[3]/'homebrew/hb_macho.py'))['MachO']
    rows = []
    for path in a.images:
        image = macho(path.read_bytes())
        for ordinal, library in enumerate(image.dylibs, 1):
            if not any('/'+name+'.framework/' in library['name'] for name in a.framework):
                continue
            rows.append(dict(image=str(path), library=library,
                             symbols=[dict(name=name, weak=bool(weak))
                                      for name, bound, weak in image.imports() if bound == ordinal]))
    a.output.write_text(json.dumps(rows, indent=2)+'\n')


if __name__ == '__main__':
    main()
