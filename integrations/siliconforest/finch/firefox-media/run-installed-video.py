#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Cold-copy/run the installed-outer video gate. Caller holds gui -> bench.

No launcher/manifest/provider changes. FX rolls selection back if this gate or
the required inspection of both actual GPU captures fails.
"""
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
    a=p.parse_args();stage=a.stage.resolve();repo=Path(__file__).resolve().parents[4]
    if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold gui.lock then bench')
    if stage.parent!=repo/'runs/sffinch' or not stage.name.startswith('firefox-video-installed-packet-'):
        raise ValueError('Own fresh installed acceptance packet required')
    if (stage/'evidence').exists() or (stage/'target-cold-verified.json').exists():
        raise ValueError('Retain prior execution; prepare a fresh packet for any repeat')
    subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),
                    '--max-seconds','170'],cwd=repo,check=True)
    dest='/private/var/db/sffx/'+stage.name
    ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
         '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
         '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
    subprocess.run(['python3',str(repo/'integrations/siliconforest/firefox/put.py'),
                    '--source',str(stage),'--destination',dest,
                    '--receipt',str(stage/'target-cold-verified.json')],cwd=repo,check=True)
    def note(body):
        with (repo/'runs/m1-coord/log.txt').open('a') as f:
            f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+body+'\n')
    note('installed CVVT acceptance START: actual outer/strict600+300/naturalQuit; own profile only, captures/no input, no installs.')
    status=None;collection=None
    try:
        command=['python3',dest+'/cadence-browser.py','--run',dest+'/installed-run',
                 '--installed-media','--frame-mode','require','--without-trace']
        with (stage/'transport.stdout').open('wb') as out:
            result=subprocess.run(ssh+[shlex.join(command)],stdout=out,stderr=subprocess.STDOUT,timeout=165)
        status=result.returncode
        (stage/'transport-status.json').write_text(json.dumps({'returncode':status})+'\n')
    finally:
        with (stage/'evidence.tar.gz').open('wb') as out:
            collection=subprocess.run(ssh+[shlex.join(['tar','-C',dest,'--exclude=*/profile',
                '--exclude=*/home','-czf','-','installed-run'])],stdout=out,stderr=subprocess.PIPE,timeout=30)
        (stage/'collection.stderr').write_bytes(collection.stderr)
        (stage/'collection-status.json').write_text(json.dumps({'returncode':collection.returncode})+'\n')
        if collection.returncode==0:
            with tarfile.open(stage/'evidence.tar.gz') as archive:
                archive.extractall(stage/'evidence',members=[m for m in archive.getmembers()
                    if not m.issym() and not m.islnk()],filter='data')
        note('installed CVVT acceptance END: transport='+str(status)+' collection='+str(collection.returncode)+
             '; inspect installed-video-admission.json and both actual GPU images; raw failures retained.')
    if status!=0 or collection.returncode!=0:return 1
    return subprocess.run(['python3',str(Path(__file__).with_name('analyze-installed-video.py')),
        str(stage/'evidence/installed-run'),'--output',str(stage/'installed-video-admission.json')],cwd=repo).returncode


if __name__=='__main__':raise SystemExit(main())
