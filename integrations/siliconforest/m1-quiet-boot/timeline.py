#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Boot timeline of one tools/pcie_iterate.sh boot, in seconds after the guest started.
Usage: timeline.py runs/m1-desktop-010/pcie-enum-attempt-<name>
Host clock throughout: the launcher's BQ_T lines (guest/worker.log) and the arrival time of
each console chunk (guest/console-times.txt: time, bytes received so far). A console line's
time is that of the chunk it arrived in (chunks are about 0.1 s apart)."""
import bisect
import json
import re
import sys
from pathlib import Path

MARKS = [
    ('kernel banner', b'Darwin Kernel Version'),
    ('SFDcp probing', b'SFDcp: LinuxKPI runtime up'),
    ('DCP coprocessor booted', b'DCP booted'),
    ('DRM device registered', b'stage: drm_dev_register'),
    ('root mounted', b'BSD root: md0'),
    ('guest script begins', b'GPUDESK_BEGIN'),
    ('compositor started', b'GPUDESK_DESKTOP_START'),
    ('first commit requested (mode set)', b'atomic commit with ALLOW_MODESET'),
    ('dcp_poweron', b'dcp_poweron() starting'),
    ('set_digital_out_mode finished', b'set_digital_out_mode finished'),
    ('first flip complete (first desktop frame)', b'first flip-complete event'),
    ('panic', b'panic(cpu'),
]


def main(run):
    guest = Path(run) / 'guest'
    log = guest.joinpath('worker.log').read_text(errors='replace')
    bq = {m.group(1): float(m.group(2)) for m in re.finditer(r'^BQ_T (\S+) ([0-9.]+)', log, re.M)}
    start = bq['hv_start']
    logo = re.search(r'^QUIET_BOOT_LOGO (.*)$', log, re.M)
    rows = []
    if logo:
        info = json.loads(logo.group(1))
        print('boot picture:', json.dumps(info))
        if info.get('painted'):
            rows.append((info['painted_at'], 'boot picture in the framebuffer'))
    rows += [(bq[k], 'launcher: ' + k) for k in ('worker_begin', 'hv_start', 'first_callback', 'shell_prompt', 'capture_stop') if k in bq]
    raw = guest.joinpath('console.bin').read_bytes()
    times = [(float(a), int(b)) for a, b in (line.split() for line in guest.joinpath('console-times.txt').read_text().split('\n') if line)]
    ends = [b for _, b in times]
    for label, marker in MARKS:
        at = raw.find(marker)
        if at >= 0:
            rows.append((times[min(bisect.bisect_right(ends, at), len(times) - 1)][0], label))
    for when, label in sorted(rows):
        print('%9.2f s  %s' % (when - start, label))


if __name__ == '__main__':
    main(sys.argv[1])
