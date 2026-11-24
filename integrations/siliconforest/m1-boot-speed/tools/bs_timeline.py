#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Where the seconds of one desktop boot go, from the wrapper's start to the first frame.

Usage: bs_timeline.py runs/m1-desktop-010/pcie-enum-attempt-<name> [--tsv]

One clock (this host's and the controller's agree to a few ms over NTP):
  times.txt             tools/pcie_iterate.sh and ./bs_boot.sh, this host
  controller-times.txt  file times of the controller's cycle directory (./bs_boot.sh
                        fetches them; without the file those phases are one segment)
  guest/worker.log      the launcher's BQ_T lines
  guest/console.bin + console-times.txt   a console line has the arrival time of its chunk
                        (chunks are about 0.1 s apart)
Each segment is harness (exists only because the M1 is booted over USB from another Mac) or
Darwin (would exist on an installed system). Works on any pcie_iterate boot of the desktop;
segments whose marks are missing are left out.
"""
import bisect
import re
import sys
from pathlib import Path

CONSOLE = [
    ('banner', rb'Darwin Kernel Version'),
    ('drm', rb'stage: drm_dev_register'),
    ('root', rb'BSD root: md0'),
    ('launchd', rb'launchd\[1\] has started'),
    ('shell', rb'Singleuser boot'),
    ('script', rb'(GPUDESK|DSDESK)_BEGIN'),
    ('desktop', rb'(GPUDESK|DSDESK)_DESKTOP_START'),
    ('renderer', rb'WMSTAT selected'),
    ('frame', rb'first flip-complete event'),
    ('wifi_join', rb'GPUDESK_WIFI_JOIN_RC=0'),
    ('net_up', rb'GPUDESK_NET_UP_RC=0'),
    ('ping', rb'GPUDESK_PING_RC=0'),
    ('touchbar_fw', rb'SFTouchBar: digitizer: firmware'),
    ('en0', rb'SFBwfm: interface is en0'),
    ('panic', rb'panic\(cpu'),
]
# (label, from mark, to mark, kind)
SEGMENTS = [
    ('staging: builds are looked up, boot picture packed', 'wrapper_start', 'wrapper_staged', 'harness'),
    ('packet assembled (RAM root staged)', 'start', 'assembled', 'harness'),
    ('packet delivered to the controller', 'assembled', 'packet_delivered', 'harness'),
    ('waiting for the bench', 'packet_delivered', 'cycle_begin', 'queue'),
    ('launcher tree delivered, inputs verified', 'cycle_begin', 'c_verified', 'harness'),
    ('Linux baseline, reboot requested', 'c_verified', 'c_reboot', 'harness'),
    ('Linux shuts down, m1n1 appears on USB', 'c_reboot', 'c_m1n1', 'harness'),
    ('m1n1 bootstrap stage', 'c_m1n1', 'c_bootstrapped', 'harness'),
    ('new USB enumeration, worker starts', 'c_bootstrapped', 'worker_begin', 'harness'),
    ('controller: reboot to launcher start (no phase times)', 'cycle_begin', 'worker_begin', 'harness'),
    ('proxy state collected', 'worker_begin', 'proxy_collected', 'harness'),
    ('boot picture painted and read back', 'proxy_collected', 'boot_logo_done', 'harness'),
    ('hypervisor init, cpufreq, PCIe link', 'boot_logo_done', 'kernel_load_begin', 'harness'),
    ('kernel collection uploaded', 'kernel_load_begin', 'kernel_load_end', 'harness'),
    ('RAM root: gzip on the controller', 'ramdisk_load_begin', 'ramdisk_compressed', 'harness'),
    ('RAM root: sent over USB', 'ramdisk_compressed', 'ramdisk_sent', 'harness'),
    ('RAM root: decompressed on the M1', 'ramdisk_sent', 'ramdisk_load_end', 'harness'),
    ('RAM root uploaded (no phase times)', 'ramdisk_load_begin', 'ramdisk_load_end', 'harness'),
    ('device tree, boot arguments, hypervisor start', 'ramdisk_load_end', 'hv_start', 'harness'),
    ('XNU: start to root mounted (drivers start, registry and clock waits, BSD)', 'hv_start', 'root', 'Darwin'),
    ('root mounted to launchd running', 'root', 'launchd', 'Darwin'),
    ('launchd to the single-user shell (launchctl waits for the registry)', 'launchd', 'shell', 'Darwin'),
    ('shell up to the challenge typed (the launcher waits for a silent console)', 'shell', 'shell_prompt', 'harness'),
    ('challenge answered, command typed, script begins', 'shell_prompt', 'script', 'harness'),
    ('guest script: audio server, Touch Bar, input nodes', 'script', 'desktop', 'Darwin'),
    ('compositor: process start to renderer selected', 'desktop', 'renderer', 'Darwin'),
    ('compositor: renderer selected to first flip', 'renderer', 'frame', 'Darwin'),
    ('first frame to both terminals ready', 'frame', 'terminals', 'Darwin'),
]


def load(run):
    run = Path(run)
    marks = {}
    for line in run.joinpath('times.txt').read_text().splitlines():
        name, _, value = line.partition(' ')
        if value:
            marks[name] = float(value)
    controller = run / 'controller-times.txt'
    if controller.is_file():
        names = {'verified-inputs.json': 'c_verified', '002-ssh.json': 'c_reboot', 'bootstrap-port.json': 'c_m1n1',
                 'bootstrap-completed.json': 'c_bootstrapped', 'guest-port.json': 'c_guest_port',
                 'guest-completed.json': 'c_guest_done'}
        for line in controller.read_text().splitlines():
            when, _, name = line.partition(' ')
            if name in names:
                marks[names[name]] = float(when)
    guest = run / 'guest'
    log = guest.joinpath('worker.log').read_text(errors='replace')
    for match in re.finditer(r'^BQ_T (\S+) ([0-9.]+)', log, re.M):
        marks.setdefault(match[1], float(match[2]))
    raw = guest.joinpath('console.bin').read_bytes()
    times = [(float(a), int(b)) for a, b in
             (line.split() for line in guest.joinpath('console-times.txt').read_text().split('\n') if line)]
    ends = [b for _, b in times]

    def when(offset):
        return times[min(bisect.bisect_right(ends, offset), len(times) - 1)][0]
    for name, pattern in CONSOLE:
        found = re.search(pattern, raw)
        if found:
            marks[name] = when(found.start())
    # Terminals: the compositor's own record, milliseconds after it started, for the two it
    # starts by itself.
    ready, source = [], {}
    for match in re.finditer(rb'attempt=(\d+) source=(\w+)', raw):
        source[match[1]] = match[2]
    for match in re.finditer(rb'attempt=(\d+) gate=AppReady observed_ms=Some\((\d+)\)', raw):
        if source.get(match[1]) == b'explicit_startup':
            ready.append(int(match[2]))
    if 'desktop' in marks and len(ready) >= 2:
        marks['terminals'] = max(marks['frame'], marks['desktop'] + max(ready) / 1000) if 'frame' in marks else marks['desktop'] + max(ready) / 1000
    # the last console output before the launcher typed its challenge
    if 'shell_prompt' in marks:
        before = [t for t, _ in times if t < marks['shell_prompt']]
        if before:
            marks['quiet'] = before[-1]
    root = raw.find(b'BSD root: md0')
    extra = dict(console_bytes=len(raw), console_bytes_to_root=root if root >= 0 else None,
                 busy=[line.decode('latin1').strip() for line in raw.split(b'\n') if b'IOKitInitializeTime:' in line],
                 wait_patch=re.search(r'^TIME_WAIT_PATCH (.*)$', log, re.M),
                 logo=re.search(r'^QUIET_BOOT_LOGO (.*)$', log, re.M))
    return marks, extra


def segments(marks):
    rows, used = [], set()
    for label, a, b, kind in SEGMENTS:
        if a not in marks or b not in marks:
            continue
        # the coarse alternatives apply only when the fine phases are missing
        if 'no phase times' in label and (('c_verified' in marks and a == 'cycle_begin') or
                                          ('ramdisk_compressed' in marks and a == 'ramdisk_load_begin')):
            continue
        rows.append((label, marks[b] - marks[a], kind))
        used.add((a, b))
    return rows


def main(argv):
    tsv = '--tsv' in argv
    run = [a for a in argv if not a.startswith('--')][0]
    marks, extra = load(run)
    rows = segments(marks)
    name = Path(run).name.replace('pcie-enum-attempt-', '')
    if tsv:
        for label, seconds, kind in rows:
            print('%s\t%s\t%.2f\t%s' % (name, kind, seconds, label))
    else:
        print('boot %s' % name)
        for label, seconds, kind in rows:
            print('%8.2f s  %-8s %s' % (seconds, kind, label))
    totals = {}
    for _, seconds, kind in rows:
        totals[kind] = totals.get(kind, 0) + seconds

    def span(a, b):
        return marks[b] - marks[a] if a in marks and b in marks else None
    summary = [
        ('first frame after the cycle starts (reboot of the M1)', span('cycle_begin', 'frame')),
        ('first frame after the wrapper starts, without the bench queue',
         (span('wrapper_start', 'frame') or 0) - (span('packet_delivered', 'cycle_begin') or 0) if span('wrapper_start', 'frame') else None),
        ('first frame after the hypervisor starts the guest', span('hv_start', 'frame')),
        ('both terminals ready after the hypervisor starts the guest', span('hv_start', 'terminals')),
        ('display driver registered after the hypervisor starts the guest', span('hv_start', 'drm')),
        ('last kernel line before the challenge, after the shell is up', span('shell', 'quiet')),
        ('Wi-Fi joined after the script begins', span('script', 'wifi_join')),
        ('network up (address, ping) after the script begins', span('script', 'ping')),
        ('Touch Bar digitizer firmware loaded after root mount', span('root', 'touchbar_fw')),
        ('en0 attached after root mount', span('root', 'en0')),
        ('M1 back in Linux after the capture stops', span('capture_stop', 'cycle_end')),
    ]
    out = sys.stdout
    for label, value in summary:
        if value is not None:
            out.write(('%s\ttotal\t%.2f\t%s\n' % (name, value, label)) if tsv else ('%8.2f s  total    %s\n' % (value, label)))
    for kind in ('harness', 'Darwin'):
        if kind in totals:
            out.write(('%s\tsum\t%.2f\t%s segments\n' % (name, totals[kind], kind)) if tsv else ('%8.2f s  sum      %s segments\n' % (totals[kind], kind)))
    if not tsv:
        print('console: %d bytes, %s up to the root mount' % (extra['console_bytes'], extra['console_bytes_to_root']))
        for key in ('wait_patch', 'logo'):
            if extra[key]:
                print(extra[key][0][:300])
        for line in extra['busy'][:40]:
            print(line[:200])
        if 'panic' in marks:
            print('PANIC in the console at %.2f s after the hypervisor start' % (marks['panic'] - marks.get('hv_start', 0)))


if __name__ == '__main__':
    main(sys.argv[1:])
