#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compares the pictures sfavd-replay decoded with a software decode of the same stream.

Usage: avd_compare.py <console log or file with SFAVD_FRAME lines> <reference.framemd5> [<label> [<order file>]]

The reference is `ffmpeg -i <clip> -pix_fmt nv12 -f framemd5` (pure software decode on the
workstation): one MD5 per frame in display order. sfavd-replay prints pictures in decode
order with their presentation time; sorted by it they are in display order. For streams
without presentation times (raw Annex B) the order file gives, per output frame, the number
of the picture in decode order (tools/avd_record.sh makes it from the recording run).
Exit 0 when every picture of the reference was decoded and every hash matches.
"""
import re
import sys


def main():
    log, ref = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else ref
    frames = []
    fails = 0
    for line in open(log, errors='replace'):
        m = re.search(r'SFAVD_FRAME (\d+) pts (-?\d+) (\d+)x(\d+) ([0-9a-f]{32}) us (\d+)', line)
        if m:
            frames.append((int(m.group(2)), int(m.group(1)), m.group(5), int(m.group(6))))
        elif 'SFAVD_FAIL' in line:
            fails += 1
    want = [l.split(',')[-1].strip() for l in open(ref) if l.strip() and not l.startswith('#')]
    if len(sys.argv) > 4:
        by_number = {f[1]: f for f in frames}
        order = [int(l) for l in open(sys.argv[4]) if l.strip()]
        frames = [by_number.get(n, (0, n, 'missing', 0)) for n in order]
    else:
        frames.sort()
    matched = sum(1 for (f, w) in zip(frames, want) if f[2] == w)
    first_bad = next((i for i, (f, w) in enumerate(zip(frames, want)) if f[2] != w), None)
    times = [f[3] for f in frames]
    mean = sum(times) / len(times) if times else 0
    print('%s: %d of %d reference frames matched; %d decoded, %d failed; decode time mean %.2f ms, max %.2f ms%s' % (
        label, matched, len(want), len(frames), fails, mean / 1000, (max(times) if times else 0) / 1000,
        '' if first_bad is None else '; first mismatch at display index %d (decode index %d)' % (first_bad, frames[first_bad][1])))
    return 0 if matched == len(want) and len(frames) >= len(want) else 1


if __name__ == '__main__':
    sys.exit(main())
