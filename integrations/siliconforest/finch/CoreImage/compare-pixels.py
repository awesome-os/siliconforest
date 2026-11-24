#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Separate rendered pixels from extent descriptions; raw harness verdict is retained."""
import argparse
import json
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('candidate', type=Path)
    ap.add_argument('reference', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    rows = []
    for path in [a.candidate, a.reference]:
        result = next(r for r in json.loads(path.read_text()) if r['name'] == 'coreimage-test')
        if result['returncode'] != 0:
            raise ValueError('A program failed')
        pixels = []
        for line in result['stdout'].splitlines():
            if ': extent ' in line and ' |' in line:
                label, value = line.split(': extent ', 1)
                extent, values = value.split(' |', 1)
                pixels.append(dict(label=label, extent=extent, values=values))
        rows.append((result, pixels))
    (candidate, c), (reference, r) = rows
    if candidate['sha256'] != reference['sha256']:
        raise ValueError('Programs differ')
    if not c or [p['label'] for p in c] != [p['label'] for p in r]:
        raise ValueError('Pixel result sets differ')
    differences = [dict(label=x['label'], candidate=x['values'], reference=y['values'])
        for x, y in zip(c, r) if x['values'] != y['values']]
    extent_differences = [dict(label=x['label'], candidate=x['extent'], reference=y['extent'])
        for x, y in zip(c, r) if x['extent'] != y['extent']]
    result = dict(sha256=candidate['sha256'], pixel_rows=len(c),
        matching_pixels=len(c)-len(differences), pixel_differences=differences,
        extent_differences=extent_differences,
        note='Supplementary analysis; does not determine or change the raw harness verdict. '
             'Upstream coarse pixel quantization is retained, not a new tolerance.')
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(f"Pixels: {result['matching_pixels']}/{len(c)} rows match; see raw harness verdict")


if __name__ == '__main__':
    main()
