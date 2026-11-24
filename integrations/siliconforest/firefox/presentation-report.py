#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize saved presentation-series receipts without averaging quantiles.

No target access. Stats collection can perturb the client: reported longest
intervals include that observer, and are not attributed to Firefox alone.
"""
import argparse
import json
from pathlib import Path
import re
import statistics


def process_times(snapshot):
    rows = {}
    for line in snapshot.splitlines()[1:]:
        fields = line.split(None, 5)
        if len(fields) != 6:
            raise ValueError('Incomplete CPU snapshot')
        parts = fields[2].split(':')
        seconds = 0.0
        for part in parts:
            seconds = seconds * 60 + float(part)
        rows[int(fields[0])] = seconds
    return rows


def summarize(root):
    rounds = []
    observation_settings = set()
    for path in sorted(root.glob('round-*/result.json')):
        receipt = json.loads(path.read_text())
        if (not receipt.get('complete') or receipt.get('natural_exit') != 0 or
                receipt.get('surviving_family') or receipt.get('cleanup_signals') or
                receipt.get('qualification_invalidated')):
            raise ValueError('Incomplete or invalid round: ' + str(path))
        observation_settings.add((bool(receipt.get('automatic_intervals', False)),
                                  receipt.get('normal_services_settle_seconds', 0)))
        if len(observation_settings) > 1:
            raise ValueError('Cannot combine different observation or startup-settle settings')
        phases = []
        for phase in receipt['phases']:
            if not phase['measurement_complete']:
                raise ValueError('Incomplete phase: ' + str(path))
            pacing = phase['presentation']
            before, after = map(process_times, [phase['cpu_before'], phase['cpu_after']])
            elapsed = phase['end_monotonic'] - phase['begin_monotonic']
            if elapsed <= 0:
                raise ValueError('Invalid elapsed time')
            per_flush = []
            for line in pacing['lines']:
                match = re.search(r'shown->shown n=(\d+) p50_ms=([\d.]+) p95_ms=([\d.]+) p99_ms=([\d.]+) max_ms=([\d.]+)', line)
                if match:
                    per_flush.append(dict(zip(['n', 'p50_ms', 'p95_ms', 'p99_ms', 'max_ms'],
                                              [int(match[1])] + [float(v) for v in match.groups()[1:]])))
            phases.append(dict(case=phase['case'], fps=pacing['presented_fps'],
                shown=pacing['shown'], replaced=pacing['replaced_before_shown'],
                over_ms=pacing['over_ms'], video_frames=phase.get('video_frames'),
                video_drops=phase.get('video_drops'), per_flush_intervals=per_flush,
                cpu_interval_seconds=elapsed,
                family_cpu_percent=100 * sum(after[p] - t for p, t in before.items() if p in after) / elapsed,
                exited_before_final_snapshot=sorted(set(before) - set(after)),
                born_after_initial_snapshot=sorted(set(after) - set(before))))
        if [p['case'] for p in phases] != ['scroll', 'css', 'video']:
            raise ValueError('Expected all three ordered phases')
        rounds.append(dict(round=receipt['round'], phases=phases))
    cases = {}
    for case in ['scroll', 'css', 'video']:
        phases = [p for r in rounds for p in r['phases'] if p['case'] == case]
        if not phases:
            continue
        def extent(key):
            values = [p[key] for p in phases]
            return dict(min=min(values), median=statistics.median(values), max=max(values))
        cases[case] = dict(samples=len(phases), fps=extent('fps'), replaced=extent('replaced'),
            family_cpu_percent=extent('family_cpu_percent'),
            threshold_59_5_passes=sum(p['fps'] >= 59.5 for p in phases),
            decoded_frame_drops=sum(p['video_drops'] or 0 for p in phases),
            maximum_observed_interval_ms=max(h['max_ms'] for p in phases for h in p['per_flush_intervals']))
    automatic, settle = next(iter(observation_settings), (False, 0))
    observer = ('Automatic compositor log intervals; no stats/dump/input requests during observation. '
                'Automatic log output can still perturb the compositor.' if automatic else
                'Explicit stats flush and log output occur in the measured clock; '
                'interval maxima include their potential cost.')
    return dict(complete=len(rounds) == 12 and [r['round'] for r in rounds] == list(range(1, 13)),
        all_59_5=len(rounds) == 12 and all(p['fps'] >= 59.5 for r in rounds for p in r['phases']),
        quantiles='Per-flush histograms retained separately; no combined percentile is inferred.',
        automatic_intervals=automatic, normal_services_settle_seconds=settle,
        observer=observer,
        cpu_scope='Matched process TIME deltas, percent of one core. Exited/new processes are listed and excluded; this can undercount.',
        cases=cases, rounds=rounds)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    print(json.dumps(summarize(args.root), indent=2))


if __name__ == '__main__':
    main()
