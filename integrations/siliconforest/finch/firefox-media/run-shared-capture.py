#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native correctness control, not a browser/presentation benchmark.

Caller holds bench. This CLI uses its own broker, no desktop/seat/session.
Each codec receives a separate cold prefix:
executing H264 can lower PAC instructions in loaded files before HEVC starts.
Never relax cold hashing or reuse those hot images as pristine bytes.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
from capture_lifetime import qualify as qualify_lifetime

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('stage',type=Path)
p.add_argument('--capture-buffers',type=int,choices=range(1,33))
p.add_argument('--codec',choices=['h264','hevc'],help='One bounded correctness case; default runs both')
a=p.parse_args();repo=Path(__file__).resolve().parents[4]
if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold bench')
if not a.stage.name.startswith('firefox-shared-capture-'):raise ValueError('Own capture packet required')
manifest=json.loads((a.stage/'manifest.json').read_text())
if a.capture_buffers and any(t['env'].get('SF_VT_CAPTURE_BUFFERS')!=str(a.capture_buffers) for t in manifest):
    raise ValueError('Capture pool request must be frozen in the manifest')
ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
     '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
     '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
def log(message):
    with (repo/'runs/m1-coord/log.txt').open('a') as f:
        f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+message+'\n')
with tempfile.TemporaryDirectory() as temporary:
    frozen=Path(temporary)/'packet'
    shutil.copytree(a.stage,frozen,symlinks=True,ignore=shutil.ignore_patterns('target-*'))
    for codec in ([a.codec] if a.codec else ['h264','hevc']):
        subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),
                        '--max-seconds','100'],cwd=repo,check=True)
        name='pattern-1080p60-h264' if codec=='h264' else 'pattern-4k30-hevc'
        dest='/private/var/db/sffx/'+a.stage.name+'-'+codec
        out=a.stage/('target-'+codec);out.mkdir()
        subprocess.run(['python3',str(repo/'integrations/siliconforest/firefox/put.py'),
                        '--source',str(frozen),'--destination',dest,
                        '--receipt',str(out/'cold-receipt.json')],cwd=repo,check=True)
        log('strict capture '+codec+' START; independent pixels/PTS/held64, no input/install; not browser FPS.')
        result=None
        try:
            command=['python3',dest+'/compare-tests.py','capture','manifest.json',name,
                     '--role','candidate','--output','candidate.json']
            # The comparison engine resolves relative paths inside the prefix.
            remote='cd '+shlex.quote(dest)+' && '+shlex.join(command)
            with (out/'launcher.log').open('w') as stream:
                result=subprocess.run(ssh+[remote],stdout=stream,stderr=subprocess.STDOUT,timeout=90)
        finally:
            files=['candidate.json',name+'-before.txt',name+'-after.txt','cold-verified.json']
            script='import json;from pathlib import Path;p=Path('+repr(dest)+');names='+repr(files)+';print(json.dumps({n:(p/n).read_text() for n in names if (p/n).is_file()}))'
            collected=subprocess.run(ssh+[shlex.join(['python3','-c',script])],capture_output=True,text=True,timeout=20)
            (out/'collection.stderr').write_text(collected.stderr)
            if not collected.returncode:
                for name_,body in json.loads(collected.stdout).items():(out/name_).write_text(body)
            log('strict capture '+codec+' END; raw evidence retained, classification follows.')
        if result is None or result.returncode or collected.returncode:raise RuntimeError('Native capture failed; no automatic retry/next codec')
        test=next(t for t in manifest if t['name']==name)
        if test['env'].get('SF_CAPTURE_HOLD_FINAL')=='1':
            rows=json.loads((out/'candidate.json').read_text())
            lifetime=qualify_lifetime(next(r for r in rows if r['name']==name),test['env'])
            (out/'lifetime-controls.json').write_text(json.dumps(lifetime,indent=2)+'\n')
            if lifetime['status']!='PASS':raise RuntimeError('Requested lifetime control failed or was not executed')
        subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/VideoToolbox/zerocopy/qualify-capture.py'),
                        '--candidate',str(out/'candidate.json'),'--name',name,
                        '--framemd5',str(frozen/'fixtures'/(name+'.framemd5')),
                        '--fixture-manifest',str(frozen/'fixture-manifest.json'),
                        '--movie',str(frozen/'fixtures'/(name+'.mp4')),
                        '--before',str(out/(name+'-before.txt')),'--after',str(out/(name+'-after.txt')),
                        '--output',str(out/'qualification.json')],cwd=repo,check=True)
        if a.capture_buffers:
            rows=json.loads((out/'candidate.json').read_text())
            selected=re.findall(r'SF_VT_CAPTURE_POOL codec-minimum=(\d+) maximum=(\d+) selected=(\d+)',rows[0]['stderr'])
            if not selected or any(int(row[2])!=a.capture_buffers for row in selected):
                raise RuntimeError('Requested smaller pool not selected; do not claim memory reduction')
            (out/'pool-selection.json').write_text(json.dumps(selected)+'\n')
