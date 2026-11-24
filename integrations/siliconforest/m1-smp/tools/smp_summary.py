#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize a darwin-smp-bench run from a saved guest console (console.bin).

Per group: workers that reported, megabytes hashed in total, per second, and per worker.
Usage: smp_summary.py runs/m1-desktop-010/pcie-enum-attempt-<name>/guest/console.bin
"""
import re
import sys
from collections import OrderedDict

text = open(sys.argv[1], 'rb').read().decode('latin-1')
for line in re.findall(r'^hw\.[a-z0-9.]+: .*$', text, re.M):
    print(line.strip())
groups = OrderedDict()
for tag, ident, megabytes, seconds, elapsed in re.findall(
        r'SMP_WORKER tag=(\w+) id=(\d+) megabytes=(\d+) seconds=(\d+)(?: elapsed=(\d+))?', text):
    # elapsed: the real time a worker took (its last pipeline may run past the deadline)
    groups.setdefault(tag, []).append((int(megabytes), int(elapsed or seconds)))
spins = OrderedDict()
for tag, ident, kilo, seconds, elapsed in re.findall(
        r'SMP_SPIN tag=(\w+) id=(\d+) kiloiterations=(\d+) seconds=(\d+) elapsed=(\d+)', text):
    spins.setdefault(tag, []).append((int(kilo), int(elapsed)))
for tag, rows in spins.items():
    rate = sum(k / t for k, t in rows if t)
    print(f'{tag}: workers={len(rows)} {rate:.0f} kiloiterations/s '
          f'per-worker={sorted(k for k, _ in rows)}')
soak_total = soak_seconds = soak_groups = 0
for tag, rows in groups.items():
    total, seconds = sum(m for m, _ in rows), max(t for _, t in rows)
    rate = sum(m / t for m, t in rows if t)
    if tag.startswith('soak'):
        soak_total += total
        soak_seconds += seconds
        soak_groups += 1
        continue
    print(f'{tag}: workers={len(rows)} total={total} MB in {seconds} s '
          f'({rate:.1f} MB/s) per-worker={sorted(m for m, _ in rows)}')
if soak_groups:
    print(f'soak: {soak_groups} one-minute groups, {soak_total} MB in {soak_seconds} s '
          f'({soak_total / soak_seconds:.1f} MB/s)')
print('begin markers:', len(re.findall(r'SMP_BEGIN_', text)), 'end markers:', len(re.findall(r'SMP_END_', text)))
print('panic lines:', len(re.findall(r'(?i)^.*panic\(cpu.*$', text, re.M)))
