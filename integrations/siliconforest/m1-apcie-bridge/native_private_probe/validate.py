#!/usr/bin/env python3
"""Host-only tests with retained output; no native API execution."""
import argparse,json,os,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True,type=Path);a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False)
r=subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('test_probe.py'))],capture_output=True,timeout=120,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
(a.output_dir/'tests.log').write_bytes(r.stdout+r.stderr)
(a.output_dir/'result.json').write_text(json.dumps({'status':'PASS_HOST_ONLY' if not r.returncode else 'FAILED','returncode':r.returncode,'scope':'synthetic callbacks/TEST-SDK only, no kernel API execution'},indent=2)+'\n')
raise SystemExit(r.returncode)
