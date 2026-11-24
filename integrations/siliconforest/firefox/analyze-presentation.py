#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize retained compositor stamps and process-family CPU measurements."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics


def cpu(snapshot):
    result = {}
    for line in snapshot['ps'].splitlines()[1:]:
        fields = line.split(None, 5)
        if len(fields) >= 6:
            result[int(fields[0])] = sum(float(x) * 60 ** i for i, x in enumerate(fields[2].split(':')[::-1]))
    return result


def analyze(directory, data=None):
    if data is None:
        data = json.loads((directory / 'result.json').read_text())
    fixture = data['fixture_measurement']
    start = fixture['timeOrigin'] + fixture['wallStart']
    end = fixture['timeOrigin'] + fixture['wallEnd']
    windows = {}
    log = directory / 'launcher-runtime.log'
    if not log.exists():
        log = directory / 'firefox.log'
    for match in re.finditer(r'FXWSPRESENTED window=(\d+) sequence=(\d+) compositor_ms=(\d+) request_ms=([\d.]+) callback_ms=([\d.]+)',
                             log.read_text()):
        window, _, stamp, _, callback = map(float, match.groups())
        if start <= callback <= end:
            windows.setdefault(int(window), set()).add(stamp)
    presentation = []
    for window, stamps in windows.items():
        stamps = sorted(stamps)
        intervals = sorted(b-a for a, b in zip(stamps, stamps[1:]))
        if intervals:
            presentation.append(dict(window=window, unique_flips=len(stamps),
                fps=(len(stamps)-1)*1000/(stamps[-1]-stamps[0]),
                median_ms=statistics.median(intervals), p95_ms=intervals[math.ceil(.95*len(intervals))-1]))
    before, after = data['fixture_cpu_before'], data['fixture_cpu_after']
    old, new = cpu(before), cpu(after)
    seconds = sum(max(0, value-old.get(pid, 0)) for pid, value in new.items())
    wall = after['elapsed']-before['elapsed']
    return dict(name=directory.name, passed=data['measurement_passed'],
        timing_qualification=data['measurement_passed'] and 'fixture_profile' not in data and 'pipeline_profile' not in data and not data.get('window_trace') and bool(presentation),
        raf_fps=fixture['wallFps'], hash=fixture['hash'], gl_error=fixture['glError'],
        raf_wall_median_ms=fixture.get('wallMedian'), raf_wall_p95_ms=fixture.get('wallP95'),
        raf_wall_max_ms=fixture.get('wallMax'),
        gpu_vector_compare=data.get('gpu_vector_compare'), gpu_cache_lookups=data.get('gpu_cache_lookups'),
        gpu_dirty_frames=data.get('gpu_dirty_frames', False),
        gpu_export_cache=data.get('gpu_export_cache', False), presentation=presentation,
        fixture_qos=data.get('fixture_qos'), compositor_pacing=data.get('pacing'),
        gpu_coalesce_ticks=data.get('gpu_coalesce_ticks', False),
        cpu=dict(cpu_seconds=seconds, wall_seconds=wall, percent_one_core=seconds/wall*100),
        method='Unique compositor_ms callback stamps whose callback wall time lies inside the fixture interval; callbacks sharing a stamp count once. The compositor sends these after observed page-flip completion using event-loop elapsed time, not the DRM hardware timestamp; they count completion batches, not exact hardware phase. CPU is family TIME delta as percent of one core; exited-before-final-snapshot children are omitted. rAF is reported separately.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    arguments = parser.parse_args()
    for directory in arguments.directories:
        result = analyze(directory)
        (directory / 'presentation-analysis.json').write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result))
        data = json.loads((directory / 'result.json').read_text())
        if 'fixture_repetitions' in data:
            repeated = [dict(analyze(directory, dict(data, **entry)), repetition=index+1)
                        for index, entry in enumerate(data['fixture_repetitions'])]
            (directory / 'presentation-repetitions.json').write_text(json.dumps(repeated, indent=2)+'\n')
            for row in repeated:
                print(json.dumps(row))
