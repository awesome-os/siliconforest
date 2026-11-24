#!/usr/bin/env python3
"""Offline isolated-copy validation only; no actor or deployment entry point."""
import argparse
from pathlib import Path
import os
import subprocess
import sys
HERE=Path(__file__).resolve().parent;AFTER=HERE/'after'
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=HERE/'validation');a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
checks={
 'private-policy':[sys.executable,str(HERE/'test_private_policy.py')],
 'native-wire-regression':[sys.executable,str(HERE/'test_native_host.py')],
 'legacy-host':[sys.executable,'-m','unittest','test_host_progress','test_native_desktop_progress','test_native_trackpad_child_progress','test_trackpad_enumeration_progress','test_dart_survey_progress','test_terminal_input_progress','test_virtual_input_progress','test_progress_source_fidelity','test_bootargs_progress'],
 'controller':[sys.executable,'-m','unittest','discover','-s','tests','-p','test_m1_bringup_cycle.py'],
}
for name,argv in checks.items():
    r=subprocess.run(argv,cwd=AFTER,capture_output=True,timeout=90,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONPATH='integrations/puredarwin/m1-bringup:.'))
    (a.output_dir/(name+'.log')).write_bytes(r.stdout+r.stderr)
    if r.returncode:raise SystemExit(name+' FAILED; retained log')
    print(name+' PASS',flush=True)
