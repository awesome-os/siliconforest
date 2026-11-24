#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Cold-copy and run only our bounded M1 HAL client; caller holds bench."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--probe',type=Path,required=True)
    p.add_argument('--audio-family',type=Path,required=True)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--trace',action='store_true',help='Enable bounded background publication diagnostic')
    p.add_argument('--load',action='store_true',help='Four bounded own CPU workers; caller must additionally hold gui.lock')
    a=p.parse_args();repo=Path(__file__).resolve().parents[4]
    if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold tools/bench.sh')
    a.stage=a.stage.resolve()
    if a.stage.parent!=repo/'runs/sffinch' or not a.stage.name.startswith('firefox-audio-state-packet-'):
        raise ValueError('Fresh own packet under runs/sffinch required')
    for path in [a.probe,a.audio_family]:
        if not str(path.resolve()).startswith('/nix/store/'):raise ValueError('Linux Nix output required')
    a.stage.mkdir()
    shutil.copytree(a.probe/'bin',a.stage/'bin')
    shutil.copytree(a.probe/'lib',a.stage/'lib')
    shutil.copytree(a.audio_family/'Frameworks',a.stage/'Frameworks',symlinks=True)
    cold={str(f.relative_to(a.stage)):hashlib.sha256(f.read_bytes()).hexdigest()
          for f in a.stage.rglob('*') if f.is_file() and not f.is_symlink()}
    (a.stage/'cold-files.json').write_text(json.dumps(cold,indent=2)+'\n')
    subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),
                    '--max-seconds','70'],cwd=repo,check=True)
    log=repo/'runs/m1-coord/log.txt'
    def note(body):
        with log.open('a') as f:f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+body+'\n')
    note('audio-state CLI START <=70s; own quiet per-stream clients only, cpu-load='+str(a.load)+' (four own bounded workers if true), no GUI/input/install/systemvolume change.')
    try:
        dest='/private/var/db/sffx/'+a.stage.name
        subprocess.run(['python3',str(repo/'integrations/siliconforest/firefox/put.py'),
            '--source',str(a.stage),'--destination',dest,'--receipt',str(a.stage/'cold-receipt.json')],cwd=repo,check=True)
        ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
             '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
             '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
        script='''import os,json,subprocess,hashlib,time
from pathlib import Path
root=Path(DEST)
for name,want in json.loads((root/'cold-files.json').read_text()).items():
 assert hashlib.sha256((root/name).read_bytes()).hexdigest()==want,name
rows=[]
for mode in ['installed','normalized-private']:
 env={k:v for k,v in os.environ.items() if not k.startswith('DYLD_')}
 if TRACE_ENABLED:env['SF_HAL_PCM_TRACE']='1'
 if mode=='normalized-private':
  env.update(DYLD_FRAMEWORK_PATH=str(root/'Frameworks'),SF_AU_NORMALIZE_HAL_CLOCK='1')
 start=time.monotonic()
 try:
  r=subprocess.run([str(root/'bin/audio-state-probe')]+(['--load'] if LOAD_ENABLED else []),env=env,capture_output=True,text=True,timeout=12)
  row=dict(mode=mode,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr,seconds=time.monotonic()-start)
 except subprocess.TimeoutExpired as e:
  row=dict(mode=mode,returncode=None,timeout=True,seconds=time.monotonic()-start,
           stdout=(e.stdout or b'').decode(errors='replace'),stderr=(e.stderr or b'').decode(errors='replace'))
 rows.append(row)
(root/'audio-state-results.json').write_text(json.dumps(rows,indent=2)+'\\n')
print(json.dumps(rows))
'''.replace('DEST',repr(dest)).replace('TRACE_ENABLED',repr(a.trace)).replace('LOAD_ENABLED',repr(a.load))
        r=subprocess.run(ssh+[shlex.join(['python3','-c',script])],capture_output=True,text=True,timeout=40)
        (a.stage/'transport.stderr').write_text(r.stderr)
        (a.stage/'transport.stdout').write_text(r.stdout)
        if r.returncode:raise RuntimeError('M1 transport failed; raw evidence retained')
        rows=json.loads(r.stdout);(a.stage/'audio-state-results.json').write_text(json.dumps(rows,indent=2)+'\n')
        print(json.dumps(rows,indent=2))
        return int(len(rows)!=2 or any(row['returncode']!=0 for row in rows))
    finally:note('audio-state CLI END; evidence retained, no installed changes or other-process signals.')


if __name__=='__main__':raise SystemExit(main())
