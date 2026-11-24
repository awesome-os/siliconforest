#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Qualify actual VT hardware receipts; fallback or mismatching frames fail."""
import argparse
import json
from pathlib import Path
import re


def records(text):
    return [tuple(item.strip() for item in line.split(','))
            for line in text.splitlines() if line and not line.startswith('#')]


def vt_records(text):
    return sorted(tuple(match.groups()) for match in re.finditer(
        r'^SFAVD_FRAME \d+ pts (-?\d+) (\d+)x(\d+) ([a-f0-9]{32})', text, re.M))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('candidate', type=Path)
    ap.add_argument('reference', type=Path)
    ap.add_argument('fixtures', type=Path)
    ap.add_argument('--frames', type=int, default=300)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    rows = {r['name']: r for r in json.loads(args.candidate.read_text())}
    native = {r['name']: r for r in json.loads(args.reference.read_text())}
    results = {}
    for codec in ['h264', 'hevc']:
        hw, sw = (rows['ffmpeg-'+codec+'-'+mode] for mode in ['hardware', 'software'])
        control = rows['reference-'+codec]
        ref = native[control['name']]
        vt = rows['vt-'+codec+'-hardware']
        vt_ref = native[vt['name']]
        decoded = records(hw['stdout'])
        linux = records((args.fixtures/(codec+'.nv12.framemd5')).read_text())
        timing = [line for line in hw['stderr'].splitlines() if line.startswith('VT_AVD_TIMING ')]
        checks = dict(
            completed=all(r['status']=='OK' and r['returncode']==0 for r in [hw, sw, control, ref, vt, vt_ref]),
            frames=len(decoded)==args.frames,
            hardware_reported=any(re.search(r'pictures '+str(args.frames)+r' hardware 1\b', line) for line in timing),
            hardware_software_records_equal=decoded==records(sw['stdout']),
            linux_records_equal=decoded==linux,
            linux_built_control_equal=decoded==records(control['stdout'])==records(ref['stdout']),
            same_control_bytes=control['sha256']==ref['sha256'],
            same_vt_bytes=vt['sha256']==vt_ref['sha256'],
            native_vt_timestamps_pixels_equal=len(vt_records(vt['stdout']))==args.frames and vt_records(vt['stdout'])==vt_records(vt_ref['stdout']),
            vt_required_hardware_success=bool(re.search(r'VT_SUMMARY samples '+str(args.frames)+r' pictures '+str(args.frames)+r' errors 0 hardware 1\b', vt['stdout'])))
        results[codec] = dict(status='PASS' if all(checks.values()) else 'FAIL', checks=checks,
                             hardware_seconds=hw['seconds'], software_seconds=sw['seconds'],
                             hardware_fps=len(decoded)/hw['seconds'],
                             software_fps=len(records(sw['stdout']))/sw['seconds'],
                             timing=timing, note='Single end-to-end process trial, including startup/hash output; normal desktop load, not an isolated benchmark.')
    args.output.write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))
    return int(any(r['status']!='PASS' for r in results.values()))


if __name__ == '__main__':
    raise SystemExit(main())
