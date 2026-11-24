#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare renderer pixels for native CGL export-cache off/on and resize controls."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops


def crop(root, case, phase):
    directory = root / case
    meta = json.loads((directory / (phase + '-capture.json')).read_text())
    x, y, width, height = meta['content']
    image = Image.open(directory / (phase + '-renderer.ppm')).convert('RGB')
    assert x >= 0 and y >= 0 and x+width <= image.width and y+height <= image.height
    assert width > 0 and height > 0
    return image.crop((x, y, x+width, y+height)), meta


def equal(a, b):
    assert a.size == b.size
    pixels = ImageChops.difference(a, b).tobytes()
    return sum(pixels[i:i+3] == b'\0\0\0' for i in range(0, len(pixels), 3))


def analyze(root, feature='export-cache'):
    runs = json.loads((root / 'results.json').read_text())
    prefix = {'export-cache': 'gpu-cache', 'refresh-coalescing': 'gpu-coalesce',
              'resource-cleanup': 'gpu-cleanup'}[feature]
    off_name, on_name = prefix+'-off', prefix+'-on'
    rows = []
    phases = ['green', 'yellow', 'resized']
    if (root / off_name / 'adopted-capture.json').exists():
        phases.append('adopted')
    for phase in phases:
        images = {name: crop(root, name, phase) for name in
                  ['cpu-control', off_name, on_name, 'cpu-fallback-control']}
        cpu = images['cpu-control'][0]
        off, on, fallback = [images[name][0] for name in
                            [off_name, on_name, 'cpu-fallback-control']]
        box = (0, 0, cpu.width//2, cpu.height)
        row = dict(phase=phase, size=cpu.size, opaque_pixels=cpu.width//2*cpu.height,
                   opaque_off_exact=equal(cpu.crop(box), off.crop(box)),
                   opaque_on_exact=equal(cpu.crop(box), on.crop(box)),
                   all_pixels=cpu.width*cpu.height, variant_on_off_exact=equal(off, on),
                   fallback_exact=equal(cpu, fallback),
                   gpu_external=all(images[name][1]['external'] for name in [off_name, on_name]),
                   cpu_external=any(images[name][1]['external'] for name in ['cpu-control', 'cpu-fallback-control']))
        row['passed'] = (row['opaque_off_exact'] == row['opaque_on_exact'] == row['opaque_pixels']
                         and row['variant_on_off_exact'] == row['all_pixels']
                         and row['gpu_external'] and not row['cpu_external'])
        rows.append(row)
    result = dict(feature=feature, complete=all(row['passed'] for row in rows) and len(runs) == 8 and all(row['passed'] for row in runs),
                  phases=rows, process_controls=[dict(name=r['name'], passed=r['passed'], status=r['status']) for r in runs],
                  method='Exact RGB renderer pixels: opaque region versus CPU; complete variant-on/off image including alpha; resize and optional retained-tree adoption. Existing CPU alpha accumulation is reported separately.')
    (root / 'pixel-analysis.json').write_text(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    ap.add_argument('--feature', choices=['export-cache', 'refresh-coalescing', 'resource-cleanup'], default='export-cache')
    args = ap.parse_args()
    result = analyze(args.directory, args.feature)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['complete'] else 1)
