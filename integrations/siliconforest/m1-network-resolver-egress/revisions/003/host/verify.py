"""Verify this frozen host leaf before deployment; does not start networking."""
import hashlib
import json
from pathlib import Path
HERE=Path(__file__).resolve().parent

def verify():
 receipt=json.loads((HERE/'source-validation.json').read_text())
 assert receipt['profile']=='native-resolver-example-http-v1'
 assert receipt['dns_policy_revision']==2
 assert receipt['backend_policy_revision']==3
 assert receipt['status']=='FROZEN_LINUX_BACKEND_ONLY_NO_DARWIN_RUNTIME_CLAIM'
 actual={p.name for p in HERE.iterdir() if p.is_file() and p.name!='source-validation.json'}
 assert actual==set(receipt['files_sha256']),('file set',actual)
 for name,digest in receipt['files_sha256'].items():
  assert hashlib.sha256((HERE/name).read_bytes()).hexdigest()==digest, name
 return receipt

if __name__=='__main__':
 x=verify();print(json.dumps({'status':'PASS_FROZEN_HOST_SOURCE_ONLY','files':len(x['files_sha256']),'profile':x['profile']}))
