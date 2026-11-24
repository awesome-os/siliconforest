#!/usr/bin/env python3
"""Only offline saved-file tests; exclusive validation output."""
import argparse,json,os,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True,type=Path);a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=False)
r=subprocess.run([sys.executable,'-B','-m','unittest','discover','-s',str(Path(__file__).resolve().parent),'-p','test_*.py','-v'],capture_output=True,timeout=120,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
(a.output_dir/'tests.log').write_bytes(r.stdout+r.stderr)
(a.output_dir/'result.json').write_text(json.dumps({'status':'PASS_OFFLINE_TESTS_ONLY' if not r.returncode else 'FAILED','returncode':r.returncode,'scope':'synthetic captures/external docs and known static image hashes only; no actual target capture graded'},indent=2)+'\n')
raise SystemExit(r.returncode)
