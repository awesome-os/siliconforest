#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Upload a fresh staged copy and capture one bounded direct QuickTime load.

Caller must hold gui.lock and tools/bench.sh. No installed mutation or input.
"""
import argparse,io,json,os,shlex,subprocess,tarfile
from datetime import datetime,timezone
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('stage',type=Path);a=ap.parse_args()
if not os.environ.get('BENCH_OWNER'):raise ValueError('tools/bench.sh required')
for name in ['load.py','cold-files.json','providers.json']:
 if not(a.stage/name).is_file():raise ValueError('Incomplete local stage: '+name)
if not a.stage.name.startswith('quicktime-') or not a.stage.name.replace('-','').isalnum():raise ValueError('Named QuickTime stage required')
repo=Path(__file__).resolve().parents[4]
subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py')],check=True)
ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','UserKnownHostsFile=/home/chrisk/.ssh/known_hosts_m1_siliconforest','-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
remote='/private/var/db/sffinch/'+a.stage.name
buf=io.BytesIO()
with tarfile.open(fileobj=buf,mode='w',dereference=False) as tar:
 for p in a.stage.iterdir():tar.add(p,arcname=p.name)
def log(message):
 with (repo/'runs/m1-coord/log.txt').open('a') as f:f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX-QT '+message+'\n')
log('gui.lock: bounded direct staged QuickTime load; cold hashes, own stdout/stderr, no input/capture/restart/install.')
try:
 command='mkdir '+shlex.quote(remote)+' && tar -C '+shlex.quote(remote)+' -xf - && cd '+shlex.quote(remote)+' && python3 load.py'
 r=subprocess.run(ssh+[command],input=buf.getvalue(),capture_output=True,timeout=75)
 (a.stage/'transport.stdout').write_bytes(r.stdout);(a.stage/'transport.stderr').write_bytes(r.stderr)
 r.check_returncode();report=json.loads(r.stdout)
 (a.stage/'load.json').write_text(json.dumps(report,indent=2)+'\n')
 cold=subprocess.run(ssh+['cat '+shlex.quote(remote+'/cold-verified.json')],capture_output=True,timeout=15)
 if cold.returncode==0:(a.stage/'cold-verified.json').write_bytes(cold.stdout)
 print(json.dumps(report,indent=2))
finally:log('QuickTime bounded direct load END; no installed changes/input/capture/restart, exact owned process only.')
