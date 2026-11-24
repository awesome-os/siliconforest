#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compositor-side frames of a Firefox window from WMPACING lines (one per 10 s per GPU window) in a desktop.log
excerpt: commits/s and shown/s per line, median over lines after the first <skip>.  pacing.py <desktop.log> [skip]"""
import re, statistics, sys

skip = int(sys.argv[2]) if len(sys.argv) > 2 else 2
rows = []
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'WMPACING over_ms=(\d+) window=(\d+) app="org.mozilla.firefox" commits=(\d+) shown=(\d+) replaced_before_shown=(\d+)', line)
    if m:
        ms, w, c, s, r = map(int, m.groups())
        rows.append((w, c * 1000 / ms, s * 1000 / ms, r * 1000 / ms))
print('lines', len(rows), 'per line commits/s, shown/s:', [(w, round(c, 1), round(s, 1)) for w, c, s, _r in rows])
use = rows[skip:] or rows
if use:
    print('median after %d: commits/s %.1f shown/s %.1f (min %.1f max %.1f)' % (
        skip, statistics.median(r[1] for r in use), statistics.median(r[2] for r in use),
        min(r[2] for r in use), max(r[2] for r in use)))
