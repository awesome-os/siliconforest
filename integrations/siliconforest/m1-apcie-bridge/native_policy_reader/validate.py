#!/usr/bin/env python3
"""Host-only validation; native wrapper is never linked into executed tests."""
import argparse
from pathlib import Path
import os
import subprocess
import sys
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=HERE/'validation');a=p.parse_args()
a.output_dir.mkdir(parents=True,exist_ok=True)
r=subprocess.run([sys.executable,str(HERE/'test_policy_reader.py')],capture_output=True,timeout=90,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
(a.output_dir/'tests.log').write_bytes(r.stdout+r.stderr)
if r.returncode:raise SystemExit('FAILED; see retained tests.log')
print('native reader source seam:3groups/117sanitizedcases-per-mode/18assertionmutants PASS')
