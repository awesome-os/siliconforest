#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Cold-install one reviewed media packet, check regressions, rollback on failure.

Run from the repository root, under gui.lock and tools/bench.sh. The owner has
authorized these media paths; this never installs kernels or core libraries.
Export coverage is not ABI proof: choose a previously exercised variant first.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import io
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tarfile


HERE = Path(__file__).resolve().parent
COORD = Path('runs/m1-coord')
SSH = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8',
       '-o', 'UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
       '-o', 'HostKeyAlias=m1-siliconforest', 'root@10.1.1.211']


def log(message):
    with (COORD/'log.txt').open('a') as f:
        f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+message+'\n')


def gate():
    subprocess.run([sys.executable, str(HERE/'coord-gate.py'), '--install-reservation'], check=True)


def decoded(text):
    return sorted(re.findall(r'^SFAVD_FRAME \d+ pts (-?\d+) (\d+)x(\d+) ([a-f0-9]{32})', text, re.M))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('packet', type=Path)
    ap.add_argument('--update', action='store_true', help='Explicit reviewed update; preserve original and prior-version backups')
    ap.add_argument('--installed', action='append', default=[], help='All previously installed media names')
    ap.add_argument('--baseline', type=Path, required=True, help='Qualified hardware candidate.json, 300 frames/codec')
    ap.add_argument('--hardware-stage', default='/private/var/db/sffinch/hardware-decode-v1')
    ap.add_argument('--audio-stage', default='/private/var/db/sffinch/audio-codecs-v7')
    ap.add_argument('--extra-commands', type=Path, help='Reviewed target argv arrays, no shell')
    a = ap.parse_args()
    if not os.environ.get('BENCH_OWNER'):
        raise ValueError('Run through tools/bench.sh')
    manifest = json.loads((a.packet/'manifest.json').read_text())
    name = manifest['framework']
    allowed = {'CoreVideo', 'CoreMedia', 'VideoToolbox', 'AudioToolbox', 'AVFAudio',
               'AVFoundation', 'CoreImage', 'ColorSync', 'Accelerate', 'MediaAccessibility', 'Metal'}
    if name not in allowed or set(a.installed)-allowed or name in a.installed:
        raise ValueError('Only reviewed media, one new transaction at a time')
    if not a.packet.name.startswith('install-') or not re.fullmatch(r'[A-Za-z0-9_-]+', a.packet.name):
        raise ValueError('Packet must have a simple fresh install-* name')
    if (a.packet/'receipt.json').exists():
        raise ValueError('Packet already has a receipt; inspect it before any retry')
    baseline = {row['name']: row for row in json.loads(a.baseline.read_text())}
    extras = {row['name']: row for row in json.loads(a.extra_commands.read_text())} if a.extra_commands else {}
    for codec in ['h264', 'hevc']:
        row = baseline['vt-'+codec+'-hardware']
        if row['status'] != 'OK' or row['returncode'] or len(decoded(row['stdout'])) != 300:
            raise ValueError('Incomplete qualified hardware baseline')
    # Parent flock owns gui.lock. Confirm it is held without reacquiring it.
    with (COORD/'gui.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            pass
        else:
            fcntl.flock(lock, fcntl.LOCK_UN)
            raise ValueError('Caller must hold gui.lock across this transaction')
    gate()
    work = '/private/var/db/sffinch/'+a.packet.name
    log('INSTALL START '+name+': cold hashes before activation,.pre-finch originals;one framework;bounded CLI/app checks,no input/capture/restart.')
    payload = io.BytesIO()
    with tarfile.open(fileobj=payload, mode='w', dereference=False) as tar:
        for p in a.packet.iterdir():
            tar.add(p, arcname=p.name)
        tar.add(HERE/'media-regressions.py', arcname='media-regressions.py')
        if a.extra_commands:
            tar.add(a.extra_commands, arcname='extra-commands.json')
    active = False
    try:
        r = subprocess.run(SSH+['mkdir '+shlex.quote(work)+' && tar -C '+shlex.quote(work)+' -xf -'],
                           input=payload.getvalue(), capture_output=True, timeout=45)
        (a.packet/'upload.stderr').write_bytes(r.stderr)
        r.check_returncode()
        gate()
        prefix = 'cd '+shlex.quote(work)+' && python3 install-framework.py '
        r = subprocess.run(SSH+[prefix+'--manifest manifest.json --source '+shlex.quote(name+'.framework')+' --receipt receipt.json'+(' --update' if a.update else '')],
                           capture_output=True, text=True, timeout=60)
        (a.packet/'install.stdout').write_text(r.stdout)
        (a.packet/'install.stderr').write_text(r.stderr)
        r.check_returncode()
        receipt = json.loads(r.stdout)
        (a.packet/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        active = True
        if receipt['state'] != 'installed-awaiting-regressions':
            raise ValueError('Installer did not finish activation')
        installed = [*a.installed, name]
        argv = ['python3', work+'/media-regressions.py', '--work', work,
                '--hardware-stage', a.hardware_stage, '--audio-stage', a.audio_stage]
        for item in installed:
            argv += ['--installed', item]
        if a.extra_commands:
            argv += ['--extra-commands', work+'/extra-commands.json']
        r = subprocess.run(SSH+[shlex.join(argv)], capture_output=True, text=True, timeout=200)
        (a.packet/'regressions.stdout').write_text(r.stdout)
        (a.packet/'regressions.stderr').write_text(r.stderr)
        r.check_returncode()
        report = json.loads(r.stdout)
        (a.packet/'regressions.json').write_text(json.dumps(report, indent=2)+'\n')
        for row in report['programs']:
            if row['name'] in ('vt-h264', 'vt-hevc'):
                codec = row['name'][3:]
                if (decoded(row['stdout']) != decoded(baseline['vt-'+codec+'-hardware']['stdout']) or
                        not re.search(r'VT_SUMMARY samples 300 pictures 300 errors 0 hardware 1\b', row['stdout'])):
                    raise ValueError('Hardware/PTS/pixel regression: '+codec)
            expected = extras.get(row['name'], {})
            if 'expected_sha256' in expected and row['sha256'] != expected['expected_sha256']:
                raise ValueError('Framework-specific program bytes changed: '+row['name'])
            if 'stage_expected_stdout' in expected:
                skip = expected.get('skip_lines', 0)
                if row['stdout'].splitlines()[skip:] != expected['stage_expected_stdout'].splitlines()[skip:]:
                    raise ValueError('Output changed from qualified stage: '+row['name'])
            if 'native_movie_frames' in expected:
                actual = [list(values) for values in re.findall(
                    r'^MOVIE_HEADLESS_DELIVER frame=(\d+) pts=([\d.]+).*?md5=([a-f0-9]{32})$', row['stdout'], re.M)]
                if actual != expected['native_movie_frames'] or len(actual) != 900:
                    raise ValueError('Movie frame order/PTS/pixels changed from native reference')
                summaries = re.findall(r'^MOVIE_SYNC (.*)$', row['stdout'], re.M)
                sync = dict(item.split('=', 1) for item in summaries[0].split()) if len(summaries) == 1 else {}
                if expected.get('movie_clock_relative'):
                    clock_bad = not (abs(float(sync.get('clock_relative_drift_ms', 'inf'))) < 100 and
                                     int(sync.get('clock_early_samples', '0')) > 0 and
                                     int(sync.get('clock_late_samples', '0')) > 0)
                else:
                    clock_bad = not abs(float(sync.get('clock_wall_drift_ms', 'inf'))) < 100
                if (sync.get('hardware') != '1' or sync.get('frames') != '900/900' or
                        sync.get('audio_queued') != '1440000' or sync.get('audio_completed') != '1440000' or
                        sync.get('rate') != '48000' or clock_bad):
                    raise ValueError('Movie hardware/HAL/clock regression')
        gate()
        with (a.packet/'compat.log').open('w') as output:
            r = subprocess.run(['integrations/siliconforest/macos-compat/tools/sf_compat_push.sh', 'check'],
                               stdout=output, stderr=subprocess.STDOUT, timeout=150)
        text = (a.packet/'compat.log').read_text()
        if r.returncode or text.count('COMPAT_VERDICT ok') != 18 or 'COMPAT_CHECK failed=0' not in text:
            raise ValueError('Compatibility regression')
        qualification = dict(framework=name, state='cli-qualified',
                             cold_hashes=receipt['verified'], providers=report['providers'],
                             hardware_pts_pixels='600/600 match qualified baseline',
                             compatibility='18/18', processes=report['processes'],
                             app_opens=report['app_opens'], screen_check='TERM5 pending')
        (a.packet/'qualified.json').write_text(json.dumps(qualification, indent=2)+'\n')
        log('INSTALL CLI PASS '+name+': Root selection,600 hardware PTS/hashes,WAV/AAC HAL,desktop processes,open -a Terminal/AM,compat18/18;TERM5 owns screen proof.')
        print(name, 'installed; cold hashes', len(receipt['verified']), 'CLI/app-open/compat checks pass')
    except BaseException:
        if active:
            gate()
            r = subprocess.run(SSH+['cd '+shlex.quote(work)+' && python3 install-framework.py --receipt receipt.json --rollback'],
                               capture_output=True, text=True, timeout=60)
            (a.packet/'rollback.stdout').write_text(r.stdout)
            (a.packet/'rollback.stderr').write_text(r.stderr)
            log('INSTALL FAILED '+name+': rollback command exit='+str(r.returncode)+';receipts retained,no success claim.')
        else:
            log('INSTALL FAILED '+name+': activation not confirmed;inspect target installer receipt before retry.')
        raise


if __name__ == '__main__':
    main()
