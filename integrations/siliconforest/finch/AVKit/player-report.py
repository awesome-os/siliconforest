#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare actual staged-player decoded NV12 frames against independent FFmpeg."""
import argparse,json,re
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('candidate',type=Path);p.add_argument('framemd5',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--reference',type=Path,help='Native M5 public-value result array');a=p.parse_args()
 rows={r['name']:r for r in json.loads(a.candidate.read_text())};run=rows['avkit-real-player']
 text=a.framemd5.read_text();match=re.search(r'^#tb 0: (\d+)/(\d+)$',text,re.M)
 if not match:raise ValueError('Missing FFmpeg video time base')
 num,den=map(int,match.groups());expected=[]
 for line in text.splitlines():
  if not line or line.startswith('#'):continue
  values=[v.strip() for v in line.split(',')];expected.append((int(values[2])*num/den,values[-1]))
 actual=[(int(i),float(t),h) for i,t,h in re.findall(r'^AVKIT_FRAME index=(\d+) pts=([\d.]+) md5=([a-f0-9]{32})$',run['stdout'],re.M)]
 checks=dict(programs_ok=all(r['status']=='OK' and r['returncode']==0 for r in rows.values()),
             cold_providers_checked='AVKIT_COLD_HASHES_PASS' in rows['avkit-cold-provider-hashes']['stdout'],
             completed='AVKIT_PLAYER_PASS' in run['stdout'],
             decoded_count=len(actual)==len(expected)>=2,
             ordered_indices=[r[0] for r in actual]==list(range(len(expected))),
             timestamps_and_pixels=len(actual)==len(expected) and all(abs(v[1]-e[0])<1e-6 and v[2]==e[1] for v,e in zip(actual,expected)))
 execution=a.candidate.with_name('execution.json')
 if execution.exists():
  evidence=json.loads(execution.read_text())
  counts=lambda text:{k:int(v) for k,v in re.findall(r'\b(jobs|done|errors|timeouts|contexts|dart_faults|stale) (\d+)',text)}
  before,after=counts(evidence['before']['stdout']),counts(evidence['after']['stdout'])
  required={'jobs','done','errors','timeouts','contexts','dart_faults','stale'}
  valid=evidence['before']['returncode']==evidence['after']['returncode']==0 and required<=before.keys() and required<=after.keys()
  checks['hardware_kernel_evidence']=valid and after['jobs']-before['jobs']==after['done']-before['done']==len(expected) and all(after[k]==before[k] for k in ['errors','timeouts','dart_faults','stale']) and after['contexts']==0
 if a.reference:
  native=next(r for r in json.loads(a.reference.read_text()) if r['name']=='avkit-value-test');value=rows['avkit-value-test']
  checks['native_values_same_binary']=native['sha256']==value['sha256']
  checks['native_values_exact']=native['status']=='OK' and native['returncode']==0 and native['stdout']==value['stdout']
 result=dict(status='PASS' if all(checks.values()) else 'FAIL',checks=checks,frames=len(actual),note='Real decoded data/lifecycle/AudioQueue and CPU bitmap drawing only; no window, compositor, panel, acoustic or streaming-player claim.')
 a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return int(not all(checks.values()))
if __name__=='__main__':raise SystemExit(main())
