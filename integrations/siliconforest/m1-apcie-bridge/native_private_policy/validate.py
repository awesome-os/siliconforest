#!/usr/bin/env python3
"""Bounded host validation; preserve logs only in a new output directory."""
import argparse,json,os,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True,type=Path);a=p.parse_args()
a.output_dir.mkdir(parents=True,exist_ok=False)
r=subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('test_private_policy.py'))],
    capture_output=True,timeout=120,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
(a.output_dir/'tests.log').write_bytes(r.stdout+r.stderr)
(a.output_dir/'result.json').write_text(json.dumps({'status':'PASS_HOST_ONLY' if r.returncode==0 else 'FAILED',
    'returncode':r.returncode,'scope':'synthetic callees and actual frozen join with TEST-SDK only; no target API execution'},indent=2)+'\n')
raise SystemExit(r.returncode)
