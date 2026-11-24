#!/usr/bin/env python3
"""Source-only host validation; no backend, reader, registry or writer."""
import argparse
from pathlib import Path
import os
import subprocess
import sys
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=HERE/'validation');args=p.parse_args()
args.output_dir.mkdir(parents=True,exist_ok=True)
for name,script in (('python-model','test_facts_wire.py'),('python-mutants','test_wire_mutations.py'),('C-decoder','test_policy_snapshot.py')):
    r=subprocess.run([sys.executable,str(HERE/script)],cwd=HERE,capture_output=True,timeout=90,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
    (args.output_dir/(name+'.log')).write_bytes(r.stdout+r.stderr)
    if r.returncode:raise SystemExit(name+' FAILED: '+str(r.returncode)+'; see retained log')
    print(name+' PASS',flush=True)
