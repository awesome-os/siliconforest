"""Explicit real Linux new40 causal fixture entry; no target or fake RESULT."""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_close_causal import configure,run
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--library',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
 p.add_argument('--allow-public-example-http',required=True,action='store_true')
 a=p.parse_args();configure('new40');result=run(a.library.resolve(),a.output.resolve())
 print(json.dumps(result,indent=2));raise SystemExit(0 if result['status'].startswith('PASS') else 1)
