#!/usr/bin/env python3
"""Offline-only isolated host validation. No actor/deployment entry point."""
from pathlib import Path
import os
import subprocess
import sys

HERE=Path(__file__).resolve().parent
AFTER=HERE/'after'
OUT=HERE/'validation'
OUT.mkdir(exist_ok=True)
checks={
 'native-host':[sys.executable,str(HERE/'test_native_host.py')],
 'legacy-host':[sys.executable,'-m','unittest','test_host_progress','test_native_desktop_progress',
                'test_native_trackpad_child_progress','test_trackpad_enumeration_progress',
                'test_dart_survey_progress','test_terminal_input_progress','test_virtual_input_progress',
                'test_progress_source_fidelity','test_bootargs_progress'],
 'controller':[sys.executable,'-m','unittest','discover','-s','tests','-p','test_m1_bringup_cycle.py'],
}
for name,argv in checks.items():
 env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONPATH='integrations/puredarwin/m1-bringup:.')
 p=subprocess.run(argv,cwd=AFTER,env=env,capture_output=True,timeout=60)
 (OUT/(name+'.log')).write_bytes(p.stdout+p.stderr)
 assert p.returncode==0,(name,p.returncode,(p.stdout+p.stderr).decode())
 print(name,'PASS')
