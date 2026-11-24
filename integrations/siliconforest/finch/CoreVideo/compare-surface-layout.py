#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare measured planar layout fields, retaining every raw metadata difference.

Only required public dimensions, rows, offsets and allocation sizes qualify.
Optional getter availability and private GPU extensions remain raw differences,
not a claim of complete native metadata/API equivalence.
"""
import argparse,hashlib,json
from pathlib import Path

FIELDS={
 'CV':('width','height','row','size','planes','base-present','iosurface'),
 'CV_PLANE':('index','width','height','row','offset-cv','offset-io'),
 'IO':('width','height','row','alloc','planes','base-present'),
 'IO_PLANE':('index','width','height','row','offset','row-height-bytes'),
}
def cases(text):
 result={};current=None
 for line in text.splitlines():
  fields=line.split()
  if not fields:continue
  values=dict(f.split('=',1) for f in fields[1:] if '=' in f)
  if fields[0]=='CREATE':
   key=(values['format'],values['requested'],values['surface-requested'])
   if key in result:raise ValueError('Duplicate allocation case')
   current=result[key]={'create':values,'rows':[],'raw':[]}
  if current is None:continue
  current['raw'].append(line)
  if fields[0] in FIELDS:
   current['rows'].append((fields[0],{key:values[key] for key in FIELDS[fields[0]]}))
  elif fields[0]=='SF_GPU':current['gpu']=values
  elif fields[0]=='UNLOCK':current['unlock']=values
 if len(result)!=8:raise ValueError('Expected all eight native allocation cases')
 return result

def compare(record,reference,require_gpu):
 if record['sha256']!=reference['sha256']:raise ValueError('Probe bytes differ')
 expected,actual=cases(reference['stdout']),cases(record['stdout'])
 checks={'program_exit0':record['status']=='OK' and record['returncode']==0,
         'cases_match':actual.keys()==expected.keys()}
 details=[]
 for key,want in expected.items():
  got=actual.get(key)
  if got is None:continue
  label='/'.join(key)
  checks[label+'/create']=got['create'].get('status')=='0' and got['create'].get('buffer')=='1'
  checks[label+'/unlock']=got.get('unlock',{}).get('status')=='0'
  checks[label+'/required-public-layout']=got['rows']==want['rows']
  if require_gpu and key[2]=='1':
   checks[label+'/actual-gpu-fd']=got.get('gpu',{}).get('copied-fd-valid')=='1'
   checks[label+'/gpu-data-offset']=got.get('gpu',{}).get('offset')=='16384'
  details.append(dict(case=list(key),required_public_layout_matches=got['rows']==want['rows'],
                      raw_stdout_matches=got['raw']==want['raw'],native=want,observed=got))
 return dict(status='PASS' if all(checks.values()) else 'FAIL',checks=checks,cases=details,
             limits='Required public layout fields and optional actual GPU storage only. Complete raw metadata differences retained; no pixel, fence, import or frame-rate claim.')

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('candidate',type=Path);ap.add_argument('reference',type=Path)
 ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
 native=json.loads(a.reference.read_text())
 if len(native)!=1 or native[0]['status']!='OK' or native[0]['returncode']!=0:raise ValueError('Missing successful native probe')
 rows=json.loads(a.candidate.read_text());results=[]
 if {r['name'] for r in rows}!={'cv-layout-gpu-0','cv-layout-gpu-1'} or len(rows)!=2:raise ValueError('Expected both CPU and GPU allocation modes')
 # The comparison harness hashes argv[0], our broker launcher. Bind its nested
 # probe through the complete cold-verified manifest, rather than mistaking
 # the launcher hash for the public probe's native hash.
 stage=a.candidate.parent
 cold={r['path']:r['sha256'] for r in json.loads((stage/'cold-files.json').read_text())}
 manifest=json.loads((stage/'manifest.json').read_text())
 for row in rows:
  tests=[r for r in manifest if r['name']==row['name']]
  if len(tests)!=1 or tests[0]['argv']!=['bin/surface-session','bin/iosurfaced','bin/cv-iosurface-layout-probe']:raise ValueError('Unexpected wrapper/probe manifest')
  gpu=row['name'].endswith('-1')
  if tests[0]['env']['SFFX_IOSURFACE_GPU']!=str(int(gpu)):raise ValueError('GPU mode differs')
  for path in tests[0]['argv']:
   if hashlib.sha256((stage/path).read_bytes()).hexdigest()!=cold[path]:raise ValueError('Changed cold closure')
  if row['sha256']!=cold[tests[0]['argv'][0]]:raise ValueError('Wrong executed wrapper')
  probe_sha=cold[tests[0]['argv'][2]]
  result=compare(dict(row,sha256=probe_sha),native[0],gpu)
  results.append(dict(name=row['name'],probe_sha256=probe_sha,wrapper_sha256=row['sha256'],**result))
 a.output.write_text(json.dumps(results,indent=2)+'\n')
 for result in results:print(result['name'],result['status'])
 return any(r['status']!='PASS' for r in results)
if __name__=='__main__':raise SystemExit(main())
