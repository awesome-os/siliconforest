#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare relative video/HAL clock drift in an owned unmuted 60-second run.

The transparent observer must report the same process's actual consumed-frame
clock. This measures relative drift, never acoustic output or constant latency.
"""
import argparse
import bisect
import json
from pathlib import Path
import re
import statistics


def analyze(directory):
    data = json.loads((directory / 'result.json').read_text())
    video = data['video_measurement']
    rows = [dict(pid=int(m[1]), epoch=float(m[2]), frames=float(m[3])) for m in re.finditer(
        r'FXHAL pid=(\d+) device=\d+ monotonic=[\d.]+ epoch=([\d.]+) status=0 consumed=([\d.]+)',
        (directory / 'firefox.log').read_text())]
    if len(rows) < 2 or len({row['pid'] for row in rows}) != 1:
        raise ValueError('Expected one observed HAL client with multiple samples')
    if video['muted'] or video['currentTime'] < 60:
        raise ValueError('Expected unmuted playback for at least60seconds')
    times = [row['epoch'] for row in rows]
    if any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError('HAL wall times must advance')
    if any(b['frames'] < a['frames'] for a, b in zip(rows, rows[1:])):
        raise ValueError('HAL consumed-frame clock reset during observation')
    offsets = []
    for callback in video['presented']:
        wall = (video['timeOrigin'] + callback['wall']) / 1000
        index = bisect.bisect_right(times, wall)
        if 0 < index < len(rows):
            a, b = rows[index-1], rows[index]
            frames = a['frames'] + (b['frames']-a['frames'])*(wall-a['epoch'])/(b['epoch']-a['epoch'])
            offsets.append((callback['mediaTime'], callback['mediaTime']-frames/48000))
    early = [offset for media, offset in offsets if 1 <= media <= 10]
    late = [offset for media, offset in offsets if 50 <= media <= 59]
    if not early or not late:
        raise ValueError('Insufficient overlapping early/late video and HAL samples')
    return dict(method='Same-process HAL consumed frames interpolated at JS callback wall times at48kHz; '
                'median relative offset at media1..10s versus50..59s; constant latency unknown; not acoustic proof.',
        points=len(offsets), hal_rows=len(rows),
        hal_rate_hz=(rows[-1]['frames']-rows[0]['frames'])/(times[-1]-times[0]),
        early_offset_median_s=statistics.median(early), late_offset_median_s=statistics.median(late),
        median_offset_change_ms=1000*(statistics.median(late)-statistics.median(early)),
        muted=video['muted'], volume=video['volume'], duration=video['currentTime'],
        browser_frames=video['quality'][-1]['total'], browser_dropped=video['quality'][-1]['dropped'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = analyze(args.directory)
    (args.directory / 'relative-clock-drift.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
