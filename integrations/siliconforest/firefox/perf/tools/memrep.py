#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarise an about:memory dump (memory-report json.gz).  memrep.py <file> [prefix-depth]"""
import collections, gzip, json, sys

d = json.loads(gzip.open(sys.argv[1]).read())
depth = int(sys.argv[2]) if len(sys.argv) > 2 else 2
per = collections.defaultdict(dict)
tree = collections.defaultdict(collections.Counter)
for r in d['reports']:
    p = r['process']; path = r['path']; amt = r['amount']
    if r['units'] != 0:  # bytes only
        continue
    if not path.startswith('explicit/'):
        per[p][path] = per[p].get(path, 0) + amt
    else:
        per[p]['explicit'] = per[p].get('explicit', 0) + amt
        parts = path.split('/')
        tree[p]['/'.join(parts[:depth + 1])] += amt
MB = 2 ** 20
keys = ['resident', 'resident-unique', 'resident-phys-footprint', 'vsize', 'explicit', 'heap-allocated', 'heap-mapped',
        'heap-committed/allocated', 'heap-committed/bookkeeping', 'heap-committed/overhead', 'gfx-textures', 'gfx-surface-iosurface',
        'js-main-runtime-gc-heap', 'imagelib-surface-cache-estimated-total']
print('%-60s' % 'process' + ''.join('%14s' % k[:13] for k in keys[:8]))
for p in sorted(per, key=lambda p: -per[p].get('resident', 0)):
    print('%-60s' % p[:60] + ''.join('%14.1f' % (per[p].get(k, 0) / MB) for k in keys[:8]))
for p in sorted(per, key=lambda p: -per[p].get('resident', 0))[:6]:
    print('\n%s' % p)
    other = {k: v for k, v in per[p].items() if v > 4 * MB and k not in keys[:8]}
    print('  non-explicit >4MB:', ', '.join('%s=%.1f' % (k, v / MB) for k, v in sorted(other.items(), key=lambda kv: -kv[1])[:14]))
    print('  explicit top:', ', '.join('%s=%.1f' % (k.replace('explicit/', ''), v / MB) for k, v in tree[p].most_common(14)))
