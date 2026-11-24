#!/usr/bin/env python3
"""Host-only checks; never reads a device or writes supplied ADT/capture bytes."""
import argparse
import json
from pathlib import Path
import os
import subprocess
import sys
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=HERE/'validation');a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
for name,script in (('tests','test_inventory.py'),('mutants','test_mutations.py'),('saved-adt','audit_saved_adt.py')):
    r=subprocess.run([sys.executable,str(HERE/script)],capture_output=True,timeout=90,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
    (a.output_dir/(name+'.log')).write_bytes(r.stdout+r.stderr)
    if r.returncode:raise SystemExit(name+' FAILED; retained log')
    print(name+' PASS',flush=True)
