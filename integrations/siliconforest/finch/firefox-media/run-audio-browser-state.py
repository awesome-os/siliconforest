#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own no-input Firefox PCM/state controls, caller-held gui.lock then bench."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import tarfile


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--installed-clock',action='store_true',help='One installed-owner gate after FX selects its normalized family; never select privately')
    a=p.parse_args();repo=Path(__file__).resolve().parents[4];stage=a.stage.resolve()
    if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold gui.lock then tools/bench.sh')
    if stage.parent!=repo/'runs/sffinch' or not stage.name.startswith('firefox-audio-state-browser-packet-'):
        raise ValueError('Fresh own prepared audio-state packet required')
    if not (stage/'lib/SFHALPCMTrace.dylib').is_file():raise ValueError('PCM trace required')
    if (stage/'transport.stdout').exists():raise ValueError('Never reuse a started packet')
    subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),
                    '--max-seconds','170'],cwd=repo,check=True)
    def note(body):
        with (repo/'runs/m1-coord/log.txt').open('a') as f:
            f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+body+'\n')
    note('gui.lock: own Firefox audio states <=170s; no input, '+('one installed-clock profile' if a.installed_clock else 'two fresh profiles')+', per-element volume/mute/pause/tab-close only; reporter disabled.')
    ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
         '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
         '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
    dest='/private/var/db/sffx/'+stage.name;results=[]
    try:
        subprocess.run(['python3',str(repo/'integrations/siliconforest/firefox/put.py'),
            '--source',str(stage),'--destination',dest,'--receipt',str(stage/'cold-receipt.json')],cwd=repo,check=True)
        for mode in (['installed'] if a.installed_clock else ['installed','normalized-private']):
            command=['python3',dest+'/audio-death-browser.py','--run',dest+'/own-'+mode,'--state-controls']
            if mode=='normalized-private':command.append('--audio-clock-family')
            if a.installed_clock:command.append('--expect-installed-clock')
            with (stage/('transport-'+mode+'.stdout')).open('wb') as out:
                r=subprocess.run(ssh+[shlex.join(command)],stdout=out,stderr=subprocess.STDOUT,timeout=72)
            results.append(dict(mode=mode,returncode=r.returncode))
        (stage/'transport.stdout').write_text(json.dumps(results,indent=2)+'\n')
    finally:
        archive=stage/'evidence.tar.gz'
        with archive.open('wb') as out:
            r=subprocess.run(ssh+[shlex.join(['tar','--exclude=*/profile','--exclude=*/home',
                '-C',dest,'-czf','-','own-installed']+([] if a.installed_clock else ['own-normalized-private']))],
                stdout=out,stderr=subprocess.PIPE,timeout=20)
        (stage/'collection.stderr').write_bytes(r.stderr)
        if r.returncode==0:
            with tarfile.open(archive) as t:
                t.extractall(stage/'evidence',members=[m for m in t.getmembers() if not m.issym() and not m.islnk()],filter='data')
        note('gui.lock released (own Firefox audio states); own-family cleanup/evidence retained, no installed changes/input/owner-process signals.')
    print(json.dumps(results))
    failed=len(results)!=(1 if a.installed_clock else 2) or any(row['returncode'] for row in results) or r.returncode
    if a.installed_clock and not failed:
        gate=subprocess.run(['python3',str(Path(__file__).with_name('analyze-audio-state.py')),
            str(stage/'evidence/own-installed'),'--expect-installed-clock',
            '--output',str(stage/'installed-clock-admission.json')],cwd=repo)
        failed=gate.returncode!=0
    return int(bool(failed))


if __name__=='__main__':raise SystemExit(main())
