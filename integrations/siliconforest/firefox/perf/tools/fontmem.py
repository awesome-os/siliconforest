#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Font memory in a region map: file mappings per font file and private anonymous regions whose size equals a
mapped font file's page-rounded size (whole-file private copies).  fontmem.py <regions-*.json>..."""
import collections, json, sys

for p in sys.argv[1:]:
    d = json.load(open(p)); page = d['page']
    fonts = collections.defaultdict(list)
    for r in d['regions']:
        if '/Fonts/' in r['path'] or '/fonts/' in r['path']:
            fonts[r['path']].append(r)
    sizes = {}
    for path, rs in fonts.items():
        for r in rs:
            if r['s'] >= (1 << 20):
                sizes[r['s']] = path
    copies = collections.defaultdict(list)
    for r in d['regions']:
        if not r['path'] and r['s'] in sizes:
            copies[sizes[r['s']]].append(r)
    maps_res = sum(r['res'] for rs in fonts.values() for r in rs)
    print('%s: font file maps=%d (resident %.1f MB, shared clean); whole-file private copies:' % (
        p, sum(len(v) for v in fonts.values()), maps_res / 2**20))
    for path, rs in copies.items():
        print('   %2d x %-60s private resident %.1f MB' % (len(rs), path.split('/')[-1], sum(r['prv'] for r in rs) / 2**20))
    big = sorted(((len(v), k) for k, v in fonts.items()), reverse=True)[:4]
    print('   most-mapped files:', ', '.join('%s x%d' % (k.split('/')[-1], n) for n, k in big))
