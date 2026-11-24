"""Read-only source/binding verification; no network or hardware execution."""
import hashlib
import json
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def verify():
 receipt=json.loads((HERE/'source-validation.json').read_text())
 assert receipt['status']=='FROZEN_ISOLATED_DL29_HOST_ONLY_NO_TARGET_EXECUTION'
 expected=receipt['files_sha256']
 actual={str(p.relative_to(HERE)) for p in HERE.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p!=HERE/'source-validation.json'}
 assert actual==set(expected),('host source set',actual^set(expected))
 for name,sha in expected.items():assert digest(HERE/name)==sha,name
 binding=json.loads((HERE/'host-binding-001.json').read_text())
 lab=ROOT/binding['isolated_lab']
 actual={str(p.relative_to(lab)) for p in lab.rglob('*') if p.is_file() and '__pycache__' not in p.parts}
 assert actual==set(binding['normalized_files_sha256']),('isolated source set',actual)
 for name,sha in binding['normalized_files_sha256'].items():assert digest(lab/name)==sha,name
 return receipt,binding
if __name__=='__main__':
 r,b=verify();print(json.dumps({'status':'PASS_FROZEN_DL29_HOST_SOURCE_AND_BINDING','host_files':len(r['files_sha256']),'isolated_files':len(b['normalized_files_sha256']),'profile':b['profile']}))
