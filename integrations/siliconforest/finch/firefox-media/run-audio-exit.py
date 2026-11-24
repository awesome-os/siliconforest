#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One installed Firefox client-loss/daemon-slot check; caller holds GUI/bench."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import tarfile


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True)
    a=p.parse_args();repo=Path(__file__).resolve().parents[4];stage=a.stage.resolve()
    if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold gui.lock then bench')
    if stage.parent!=repo/'runs/sffinch' or not stage.name.startswith('firefox-audio-exit-packet-'):
        raise ValueError('Own fresh audio exit packet required')
    for name in ['bin/audio-session-watch','audio-server.json','packet-files.json']:
        if not (stage/name).is_file():raise ValueError('Complete Linux-built slot-watch packet required')
    if (stage/'cold-receipt.json').exists():raise ValueError('Preserve previous run; prepare a fresh packet')
    subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),
                    '--max-seconds','110'],cwd=repo,check=True)
    def note(body):
        with (repo/'runs/m1-coord/log.txt').open('a') as f:
            f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+body+'\n')
    note('gui.lock: own installed Firefox active-audio connection loss and exact read-only coreaudiod-slot watch <=110s; no input/daemon restart/install/owner signals.')
    ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
         '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
         '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
    dest='/private/var/db/sffx/'+stage.name;status=None;collected=False
    try:
        subprocess.run(['python3',str(repo/'integrations/siliconforest/firefox/put.py'),'--source',str(stage),
            '--destination',dest,'--receipt',str(stage/'cold-receipt.json')],cwd=repo,check=True)
        command=['python3',dest+'/audio-death-browser.py','--run',dest+'/own-installed',
                 '--expect-installed-clock','--watch-server-client']
        with (stage/'transport.stdout').open('wb') as out:
            status=subprocess.run(ssh+[shlex.join(command)],stdout=out,stderr=subprocess.STDOUT,timeout=80).returncode
    finally:
        with (stage/'evidence.tar.gz').open('wb') as out:
            r=subprocess.run(ssh+[shlex.join(['tar','--exclude=*/profile','--exclude=*/home','-C',dest,
                '-czf','-','own-installed'])],stdout=out,stderr=subprocess.PIPE,timeout=20)
        (stage/'collection.stderr').write_bytes(r.stderr)
        if r.returncode==0:
            with tarfile.open(stage/'evidence.tar.gz') as archive:
                archive.extractall(stage/'evidence',members=[m for m in archive.getmembers()
                    if not m.issym() and not m.islnk()],filter='data')
            collected=True
        note('gui.lock released (own Firefox audio exit); raw outcome retained, no installed changes/input/foreign process signals.')
    result_path=stage/'evidence/own-installed/result.json'
    raw=json.loads(result_path.read_text()) if result_path.exists() else {}
    checks=dict(native_zero=status==0,collected=collected,complete=raw.get('diagnostic_complete') is True,
        installed_outer=raw.get('installed_outer_launcher') is True,
        selected_normalized_audio=raw.get('expected_installed_clock') is True,
        actual_audio_providers=raw.get('media_providers',{}).get('complete') is True,
        active_unmuted=raw.get('audio_started',{}).get('paused') is False and raw.get('audio_started',{}).get('muted') is False,
        exact_server_slot_empty=raw.get('server_slot_removal_qualified') is True,
        family_gone=raw.get('before_cleanup_owned')==[],no_cleanup_signals=raw.get('cleanup_signals')==[],
        no_input=raw.get('synthetic_input') is False,no_errors=not raw.get('error') and not raw.get('native_log_collection_error'))
    admission=dict(passed=all(checks.values()),checks=checks,exit_status=raw.get('exit_before_cleanup'),
        server_watch=raw.get('audio_session_watch'),limits='Exact own daemon-slot destruction after active-audio client loss; not acoustic silence, device-buffer drain latency, all crash modes or underrun proof.')
    (stage/'exit-admission.json').write_text(json.dumps(admission,indent=2)+'\n')
    print(json.dumps(admission,indent=2));return int(not admission['passed'])


if __name__=='__main__':raise SystemExit(main())
