#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Seconds from the kernel's start to each console marker of one desktop boot, for a native
boot (from the launcher's `native_vector` line) or a hypervisor boot (`hv_start`).
    nb_times.py <boot name> [<boot name> ...]      e.g. nb_times.py nb08 df02
The times are arrival times of the console bytes on the controller (0.1 s resolution): on a
native boot the console is a 115200 baud UART, so a marker can arrive late and, because the
kernel waits for the UART, the boot itself is held back by its own log."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
MARKS = (('banner', rb'Darwin Kernel Version'), ('root', rb'BSD root: md0'),
         ('launchd', rb'launchd\[1\] has started'), ('shell', rb'Singleuser boot'),
         ('drm', rb'stage: drm_dev_register'), ('en0', rb'SFBwfm: interface is en0'),
         ('script', rb'GPUDESK_BEGIN'), ('desktop', rb'GPUDESK_DESKTOP_START'),
         ('renderer', rb'WMSTAT selected'), ('frame', rb'first flip-complete event'),
         ('wifi_join', rb'GPUDESK_WIFI_JOIN_RC=0'), ('ping', rb'GPUDESK_PING_RC=0'))


def marks(name):
    guest = ROOT/'runs/m1-desktop-010'/('pcie-enum-attempt-' + name)/'guest'
    raw = (guest/'console.bin').read_bytes()
    times = [(float(a), int(b)) for a, b in (line.split() for line in (guest/'console-times.txt').read_text().splitlines())]
    log = (guest/'worker.log').read_text(errors='replace')
    native = re.search(r'BQ_T native_vector (\S+)', log)
    start = float((native or re.search(r'BQ_T hv_start (\S+)', log))[1])

    def when(pattern):
        match = re.search(pattern, raw)
        return next((t - start for t, count in times if match and count > match.start()), None)
    out = {'kind': 'native' if native else 'hypervisor'}
    out.update((key, when(pattern)) for key, pattern in MARKS)
    for key in ('shell_prompt', 'guest_command'):
        match = re.search(r'BQ_T %s (\S+)' % key, log)
        out[key] = float(match[1]) - start if match else None
    ready = [int(x) for x in re.findall(rb'gate=AppReady observed_ms=Some\((\d+)\)', raw)]
    if ready and out['desktop'] is not None and out['frame'] is not None:
        out['terminals'] = max(out['frame'], out['desktop'] + max(ready) / 1000)
    out['console bytes before root'] = raw.find(b'BSD root: md0')
    out['console bytes'] = len(raw)
    return out


def main(names):
    rows = [marks(name) for name in names]
    print('%-26s' % '' + ''.join('%14s' % name for name in names))
    for key in rows[0]:
        cells = [row.get(key) for row in rows]
        print('%-26s' % key + ''.join('%14s' % ('-' if c is None else '%.2f' % c if isinstance(c, float) else c) for c in cells))


if __name__ == '__main__':
    main(sys.argv[1:] or sys.exit(__doc__))
