#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Qualify movie decode identity and the measured headless HAL-clock run."""
import argparse
import json
from pathlib import Path
import re


def frames(text, prefix):
    pattern = r'^'+prefix+r' frame=(\d+) pts=([\d.]+).*?md5=([a-f0-9]{32})$'
    return [(int(n), float(pts), digest) for n, pts, digest in re.findall(pattern, text, re.M)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('candidate', type=Path)
    ap.add_argument('reference', type=Path)
    ap.add_argument('linux_framemd5', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--window', action='store_true', help='Require actual CPU view draw callbacks')
    a = ap.parse_args()
    candidate = {r['name']: r for r in json.loads(a.candidate.read_text())}
    reference = {r['name']: r for r in json.loads(a.reference.read_text())}
    decode, play = candidate['movie-decode-only'], candidate['movie-window' if a.window else 'movie-headless']
    native = reference['movie-decode-only']
    decoded = frames(decode['stdout'], 'MOVIE_DECODE')
    prefix = 'MOVIE_LAYER_SUBMIT' if a.window else 'MOVIE_HEADLESS_DELIVER'
    presented = frames(play['stdout'], prefix)
    expected = frames(native['stdout'], 'MOVIE_DECODE')
    linux = [line.split(',')[-1].strip() for line in a.linux_framemd5.read_text().splitlines()
             if line and not line.startswith('#')]
    matches = re.findall(r'^MOVIE_SYNC (.*)$', play['stdout'], re.M)
    sync = dict(item.split('=', 1) for item in matches[0].split()) if len(matches) == 1 else {}
    clocks = [(float(pts), float(audio)) for pts, audio in re.findall(
        r'^'+prefix+r' frame=\d+ pts=([\d.]+) audio=([\d.]+)', play['stdout'], re.M)]
    av_drift = (clocks[-1][1]-clocks[-1][0] - (clocks[0][1]-clocks[0][0]))*1000 if clocks else float('inf')
    if clocks:
        sync.update(av_delivery_start_ms=round((clocks[0][1]-clocks[0][0])*1000, 3),
                    av_delivery_end_ms=round((clocks[-1][1]-clocks[-1][0])*1000, 3),
                    av_delivery_drift_ms=round(av_drift, 3))
    if 'audio_clock' in sync and 'rate' in sync:
        sync['audio_clock_frames'] = round(float(sync['audio_clock'])*float(sync['rate']))
    checks = dict(
        programs_completed=all(r['status'] == 'OK' and r['returncode'] == 0 for r in [decode, play, native]),
        same_linux_built_program=decode['sha256'] == play['sha256'] == native['sha256'],
        all_frames=len(decoded) == len(presented) == len(expected) == len(linux) == 900,
        ordered_indices=[r[0] for r in presented] == list(range(900)),
        ordered_pts=all(x[1] < y[1] for x, y in zip(presented, presented[1:])),
        same_pts_pixels=decoded == presented == expected,
        linux_pixels=[r[2] for r in decoded] == linux,
        hardware_reported=sync.get('hardware') == '1',
        all_audio_completed=sync.get('audio_queued') == sync.get('audio_completed') == '1440000',
        audio_rate=sync.get('rate') == '48000',
        selected_mode=sync.get('mode') == ('cpu-layer' if a.window else 'headless'),
        cpu_view_draws=(sync.get('view_draws') == '900' and frames(play['stdout'], 'MOVIE_VIEW_DRAW') == decoded) if a.window else True,
        audio_clock_monotonic=len(clocks) == 900 and all(x[1] <= y[1] for x, y in zip(clocks, clocks[1:])),
        av_delivery_drift=abs(av_drift) < 100,
        thirty_seconds=abs(float(sync.get('wall', 'inf')) - 30) < 1,
        clock_drift=(abs(float(sync.get('clock_relative_drift_ms', 'inf'))) < 100 and
                     int(sync.get('clock_early_samples', '0')) > 0 and int(sync.get('clock_late_samples', '0')) > 0)
                    if 'clock_relative_drift_ms' in sync else abs(float(sync.get('clock_wall_drift_ms', 'inf'))) < 100,
        delivery_lateness=float(sync.get('max_delivery_late_ms', 'inf')) < 100)
    result = dict(status='PASS' if all(checks.values()) else 'FAIL', checks=checks, measured=sync,
                  note='Queued/completed counts describe source buffers returned by the retained PD HAL IOProc; '
                  'the sample clock is recorded separately. Relative clock drift is the change in mean wall/queue offset '
                  'between the 5-10% and 90-95% audio-clock windows; fixed end offset remains reported. '
                  'Delivery offsets use the scheduling audio-clock tick. CPU view drawing is not compositor presentation, panel scanout, acoustic output, '
                  'or proof of a general streaming player. One generated 30-second fixture.')
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    return int(not all(checks.values()))


if __name__ == '__main__':
    raise SystemExit(main())
