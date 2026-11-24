#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Supplement the shared VM oracle with cache toggles and 64 metadata payloads."""
from pathlib import Path
import argparse,json,re,subprocess,hashlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);a=p.parse_args();w=a.run
assert json.loads((w/'summary.json').read_text())['status']=='PASS'
s=(w/'guest/serial.log').read_text(errors='replace')
rows=re.findall(r'cache_stats: enabled=(\d+) reads=(\d+) journal_hits=(\d+) bread_calls=(\d+)',s)
assert len(rows)==4,rows
rows=[tuple(map(int,r)) for r in rows];assert [r[0] for r in rows]==[0,0,1,1]
assert rows[1][2]==rows[0][2] and rows[1][3]>rows[0][3]
assert rows[3][2]>rows[2][2] and rows[3][1]>rows[2][1]
assert 'CACHE_REHEARSAL_MODE_0=PASS' in s and 'CACHE_REHEARSAL_MODE_1=PASS' in s
proof=[]
for mode in range(2):
 for i in range(32):
  dest=w/('metadata-'+str(mode)+'-'+str(i)+'.bin');cmd='dump /cache'+str(mode)+'/file-'+str(i)+'.saved '+str(dest)
  r=subprocess.run(['debugfs','-R',cmd,str(w/'after.ext4')],capture_output=True);assert r.returncode==0,r.stderr
  data=dest.read_bytes();assert data==('CACHE-PATTERN-'+str(mode)+'-'+str(i)+'\n').encode()
  proof.append({'mode':mode,'index':i,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
result={'status':'PASS','stats':rows,'disabled_hits_delta':rows[1][2]-rows[0][2],'enabled_hits_delta':rows[3][2]-rows[2][2],'metadata_files':proof,'limits':'Actual disposable VM data/counter/toggle test; not native speed, power-loss or root-filesystem repair proof.'}
(w/'cache-summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='metadata_files'},indent=2))
