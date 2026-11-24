#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run our staged player under caller-held gui.lock/bench, retaining AVD counters."""
import argparse,datetime,io,json,os,shlex,subprocess,tarfile
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);a=p.parse_args()
 if not os.environ.get('BENCH_OWNER'):raise ValueError('Run under gui.lock then tools/bench.sh')
 repo=Path(__file__).resolve().parents[4]
 subprocess.run(['python3',str(repo/'integrations/siliconforest/finch/tools/coord-gate.py'),'--max-seconds','120'],check=True,cwd=repo)
 if not a.stage.name.startswith('avkit-real-player-stage-'):raise ValueError('Fresh owned player stage required')
 for name in ['manifest.json','compare-tests.py','cold-hashes.json']:
  if not (a.stage/name).is_file():raise ValueError('Incomplete stage: '+name)
 ssh=['ssh','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','UserKnownHostsFile=/home/chrisk/.ssh/known_hosts_m1_siliconforest','-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
 remote='/private/var/db/sffinch/'+a.stage.name
 def log(message):
  with (repo/'runs/m1-coord/log.txt').open('a') as f:f.write(datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX-AVKIT '+message+'\n')
 guest='''import json,os,subprocess,sys
from pathlib import Path
if os.uname().sysname!='Darwin':raise SystemExit('Not the Darwin target')
def stats():
 p=subprocess.run(['sysctl','kern.sf_avd.stats'],capture_output=True,text=True,timeout=10)
 return dict(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr)
before=stats()
p=subprocess.run(['python3','compare-tests.py','capture','manifest.json','--role','candidate','--output','candidate.json'],capture_output=True,text=True,timeout=80)
after=stats()
print(json.dumps(dict(before=before,after=after,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,candidate=json.loads(Path('candidate.json').read_text()) if Path('candidate.json').exists() else None)))
sys.exit(p.returncode)
'''
 buf=io.BytesIO()
 with tarfile.open(fileobj=buf,mode='w',dereference=False) as tar:
  for f in a.stage.iterdir():tar.add(f,arcname=f.name)
 command='mkdir '+shlex.quote(remote)+' && tar -C '+shlex.quote(remote)+' -xf - && cd '+shlex.quote(remote)+' && python3 -c '+shlex.quote(guest)
 log('CLI START '+a.stage.name+' <=90s, own player/CPU bitmap and actual AVD stats; no window/input/capture/restart/install.')
 try:
  result=subprocess.run(ssh+[command],input=buf.getvalue(),capture_output=True,timeout=105)
  (a.stage/'transport.stdout').write_bytes(result.stdout);(a.stage/'transport.stderr').write_bytes(result.stderr)
  if result.stdout:
   report=json.loads(result.stdout);(a.stage/'execution.json').write_text(json.dumps(report,indent=2)+'\n')
   if report['candidate'] is not None:(a.stage/'candidate.json').write_text(json.dumps(report['candidate'],indent=2)+'\n')
   print(report['stdout'],report['stderr']);print('AVD',report['before']['stdout'].strip(),'->',report['after']['stdout'].strip())
  raise SystemExit(result.returncode)
 finally:log('CLI END '+a.stage.name+'; no installed changes, no window/input/capture/restart.')
if __name__=='__main__':main()
