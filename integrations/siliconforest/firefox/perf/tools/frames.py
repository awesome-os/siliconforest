#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Presented-frame and compose statistics from a Firefox log (SFFX_FRAME_TRACE / SFFX_GPU_PHASE_TRACE lines)
over the windows named in run.json steps.  frames.py <run dir>"""
import json, re, statistics, sys
from pathlib import Path

d = Path(sys.argv[1])
run = json.loads((d / 'run.json').read_text())
logs = sorted(d.glob('firefox*.log'))
steps = run.get('steps', [])
pres, phase, other = [], [], {}
for lf in logs:
    for line in lf.read_text(errors='replace').splitlines():
        if line.startswith('FXWSPRESENTED'):
            m = dict(re.findall(r'(\w+)=([\d.]+)', line))
            pres.append((float(m['request_ms']) / 1000, float(m['callback_ms']) / 1000, int(m['window']), int(m['sequence'])))
        elif line.startswith('FXGPUPHASE'):
            m = dict(re.findall(r'(\w+)=([\d.x]+)', line))
            phase.append((float(m['epoch_ms']) / 1000, float(m['total_ms']), float(m['base_ms']), float(m['draw_ms']), float(m['finish_ms'])))
        else:
            k = line.split(' ')[0][:40]
            other[k] = other.get(k, 0) + 1
print('presented lines', len(pres), 'compose lines', len(phase))
for i, (name, t0) in enumerate(steps):
    t1 = steps[i + 1][1] if i + 1 < len(steps) else t0 + 60
    p = [x for x in pres if t0 <= x[0] < t1]
    c = [x for x in phase if t0 <= x[0] < t1]
    dt = t1 - t0
    if not p and not c:
        continue
    tot = [x[1] for x in c]
    print('%-28s %6.1fs presented=%5d (%.1f/s) composes=%5d (%.1f/s) compose_ms p50=%s p95=%s mean=%s base_ms_mean=%s finish_ms_mean=%s' % (
        name, dt, len(p), len(p) / dt, len(c), len(c) / dt,
        round(statistics.median(tot), 2) if tot else '-', round(sorted(tot)[int(.95 * (len(tot) - 1))], 2) if tot else '-',
        round(statistics.mean(tot), 2) if tot else '-', round(statistics.mean(x[2] for x in c), 2) if c else '-',
        round(statistics.mean(x[4] for x in c), 2) if c else '-'))
    if p:
        iv = [b[0] - a[0] for a, b in zip(p, p[1:])]
        if iv:
            print('    request intervals ms: p10=%.1f p50=%.1f p90=%.1f; windows=%s' % (
                1000 * sorted(iv)[len(iv) // 10], 1000 * statistics.median(iv), 1000 * sorted(iv)[9 * len(iv) // 10],
                sorted(set(x[2] for x in p))))
print('other line kinds (top):', sorted(other.items(), key=lambda kv: -kv[1])[:30])
