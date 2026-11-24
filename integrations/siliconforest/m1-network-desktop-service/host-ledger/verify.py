"""Read-only source/overlay validation; no target/network/controller calls."""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def verify():
 freeze=json.loads((HERE/'source-validation-001.json').read_text())
 for n,h in freeze['files_sha256'].items():assert sha(HERE/n)==h,n
 for n,h in freeze['dependencies_sha256'].items():assert sha(ROOT/n)==h,n
 binding=json.loads((HERE/'host-binding-001.json').read_text());lab=ROOT/binding['isolated_lab']
 actual={str(p.relative_to(lab))for p in lab.rglob('*')if p.is_file()and'__pycache__'not in p.parts}
 assert actual==set(binding['normalized_files_sha256'])
 for n,h in binding['normalized_files_sha256'].items():assert sha(lab/n)==h,n
 old=json.loads((ROOT/'integrations/puredarwin/m1-network-desktop-service/session/host/host-binding-001.json').read_text())
 delta=[n for n,h in binding['normalized_files_sha256'].items()if old['normalized_files_sha256'][n]!=h]
 assert delta==['integrations/puredarwin/m1-bringup/host_progress.py']
 return {'status':'PASS_ISOLATED_LEDGER_SOURCE_ONLY','normalized_files':len(actual),'changed_sources':delta,'actor':False}
if __name__=='__main__':print(json.dumps(verify()))
