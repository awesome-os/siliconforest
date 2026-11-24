#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own bounded CLI under caller-held gui.lock -> bench -> fresh coord gate."""
import argparse,datetime,io,json,os,shlex,subprocess,tarfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parents[4]
if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold gui.lock then bench')
if not a.stage.name.startswith(('firefox-audio-lifecycle-stage-','firefox-codec-admission-stage-')):raise ValueError('Fresh own audio stage required')
for name in ['manifest.json','compare-tests.py','cold-files.json']:
    if not (a.stage/name).is_file():raise ValueError('Incomplete stage: '+name)
subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),'--max-seconds','110'],cwd=repo,check=True)
ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','UserKnownHostsFile=/home/chrisk/.ssh/known_hosts_m1_siliconforest','-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
work='/private/var/db/sffinch/'+a.stage.name
def log(message):
    with (repo/'runs/m1-coord/log.txt').open('a') as f:f.write(datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+message+'\n')
buffer=io.BytesIO()
with tarfile.open(fileobj=buffer,mode='w',dereference=False) as tar:
    for path in a.stage.iterdir():tar.add(path,arcname=path.name)
command='mkdir '+shlex.quote(work)+' && tar -C '+shlex.quote(work)+' -xf - && cd '+shlex.quote(work)+' && python3 compare-tests.py capture manifest.json --role candidate --output candidate.json'
log('media CLI START '+a.stage.name+' under gui.lock/bench; <=110s, owner guard, own query/silent timing units, no window/input/install/other-process signals.')
try:
    result=subprocess.run(ssh+[command],input=buffer.getvalue(),capture_output=True,timeout=70)
    (a.stage/'capture.stdout').write_bytes(result.stdout);(a.stage/'capture.stderr').write_bytes(result.stderr)
    script="import json;from pathlib import Path;p=Path("+repr(work)+");print(json.dumps({n:json.loads((p/n).read_text()) if (p/n).is_file() else None for n in ['candidate.json','cold-verified.json','query/receipt.json','silence/receipt.json','latency/receipt.json','latency-normalized/receipt.json','codec-nv12-hw/receipt.json','codec-p010-hw/receipt.json','codec-p010-sw/receipt.json']}))"
    fetched=subprocess.run(ssh+['python3 -c '+shlex.quote(script)],capture_output=True,timeout=15)
    (a.stage/'fetch.stderr').write_bytes(fetched.stderr)
    if fetched.returncode==0:
        payload=json.loads(fetched.stdout);(a.stage/'receipts.json').write_text(json.dumps(payload,indent=2)+'\n')
        if payload['candidate.json'] is not None:(a.stage/'candidate.json').write_text(json.dumps(payload['candidate.json'],indent=2)+'\n')
        print(json.dumps(dict(capture_returncode=result.returncode,receipts=[key for key,value in payload.items() if value is not None])))
    else:print('Receipt fetch failed',fetched.returncode)
    raise SystemExit(result.returncode or fetched.returncode)
finally:log('media CLI END '+a.stage.name+'; no installed changes/input/other-process action.')
