"""Explicit actual Linux new40 fixture; no hardware or fabricated guest RESULT."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import types
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[6]
SCRIPT=ROOT/'integrations/puredarwin/m1-network-resolver-egress/revisions/004/host/test_close_causal.py'
SHA='347d35124cf593ef9d58e165ab8f0eaa5ed2fdde01c7c130d6cb1b68abc8c3a7'
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
 p.add_argument('--allow-public-example-http',required=True,action='store_true');a=p.parse_args()
 raw=SCRIPT.read_bytes();assert hashlib.sha256(raw).hexdigest()==SHA
 sys.path.insert(0,str(SCRIPT.parent));m=types.ModuleType('frozen_close_causal');m.__file__=str(SCRIPT);exec(compile(raw,str(SCRIPT),'exec'),m.__dict__)
 m.configure('new40');result=m.run(a.library.resolve(),a.output.resolve());print(json.dumps(result,indent=2));raise SystemExit(0 if result['status'].startswith('PASS') else 1)
