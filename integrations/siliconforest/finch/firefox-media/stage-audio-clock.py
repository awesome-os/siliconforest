#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Copy a qualified AudioToolbox family to a NEW inactive runtime prefix.

No manifest, launcher, canonical provider, daemon or process is changed.
Caller holds bench and has announced the two-minute staging notice.
The packet includes cold-files.json and a recorded not-before.json deadline.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('stage',type=Path)
a=p.parse_args();repo=Path(__file__).resolve().parents[4]
if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold bench')
if not a.stage.name.startswith('firefox-audio-clock-install-'):raise ValueError('Own qualified audio packet required')
notice=json.loads((a.stage/'not-before.json').read_text())
if time.time()<notice['epoch']:raise SystemExit(77)
subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),
                '--max-seconds','60'],cwd=repo,check=True)
remote='/private/var/db/sffx/'+a.stage.name
subprocess.run(['python3',str(repo/'integrations/siliconforest/firefox/put.py'),
                '--source',str(a.stage),'--destination',remote,
                '--receipt',str(a.stage.parent/(a.stage.name+'-cold-receipt.json'))],cwd=repo,check=True)
script=r'''
from pathlib import Path
import hashlib,json,os,shutil,sys,time
source=Path(sys.argv[1])
if source.parent!=Path('/private/var/db/sffx') or not source.name.startswith('firefox-audio-clock-install-'):
    raise ValueError('Own cold packet required')
if time.time()<json.loads((source/'not-before.json').read_text())['epoch']:raise SystemExit(77)
target=Path('/usr/local/lib/siliconforest/firefox/160-system1/overrides/audio-clock-v1')
temporary=target.with_name('.audio-clock-v1-cold-stage')
if target.exists() or target.is_symlink() or temporary.exists():raise ValueError('New target required; no overwrite or partial retry')
rows=json.loads((source/'cold-files.json').read_text())
if not rows or any(not name.startswith('Frameworks/AudioToolbox.framework/') for name in rows):
    raise ValueError('Only complete AudioToolbox family allowed')
def verify(root):
    for name,expected in rows.items():
        path=root/name
        if not path.resolve().is_relative_to(root.resolve()):raise ValueError('Packet alias escapes prefix')
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:raise ValueError('Cold hash mismatch: '+name)
verify(source)
temporary.mkdir()
shutil.copytree(source/'Frameworks',temporary/'Frameworks',symlinks=True)
verify(temporary)
os.rename(temporary,target)
verify(target)
receipt=dict(target=str(target),files=rows,cold_before_execution=True,selected=False,
             canonical_changed=False,launcher_changed=False,daemon_changed=False,
             backup='New inactive prefix; no previous file replaced',timestamp=time.time())
(target/'install-ledger.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt))
'''
ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
     '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
     '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
r=subprocess.run(ssh+[shlex.join(['python3','-c',script,remote])],capture_output=True,text=True,timeout=60)
(a.stage.parent/(a.stage.name+'-install.stdout')).write_text(r.stdout)
(a.stage.parent/(a.stage.name+'-install.stderr')).write_text(r.stderr)
if r.returncode:raise RuntimeError('Inactive staging failed; preserve stdout/stderr and partial target, no activation')
receipt=json.loads(r.stdout)
(a.stage.parent/(a.stage.name+'-install.json')).write_text(json.dumps(receipt,indent=2)+'\n')
with (repo/'runs/m1-coord/log.txt').open('a') as f:
    f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+
            ' CODEX inactive AudioToolbox family staged at '+receipt['target']+
            '; every cold file verified ON M1 before execution; selected=false, canonical/launcher/daemon unchanged. FX owns integration.\n')
