"""Read-only exact isolated source verification, never an actor."""
from pathlib import Path
import hashlib,json
H=Path(__file__).resolve().parent
R=H.parents[4]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify():
 a=json.loads((H/'source-validation-001.json').read_text())
 for n,h in a['files_sha256'].items():assert digest(H/n)==h,n
 b=json.loads((H/'host-binding-001.json').read_text());lab=R/b['isolated_lab']
 actual={str(p.relative_to(lab)) for p in lab.rglob('*') if p.is_file() and '__pycache__' not in p.parts}
 assert actual==set(b['normalized_files_sha256']),actual^set(b['normalized_files_sha256'])
 for n,h in b['normalized_files_sha256'].items():assert digest(lab/n)==h,n
 return a,b
if __name__=='__main__':
 a,b=verify();print(json.dumps({'status':'PASS_FROZEN_DL30_HOST_AND_ISOLATED_SOURCE','isolated_files':len(b['normalized_files_sha256']),'actor':False}))
