#!/usr/bin/env python3
"""Compile and execute original portable core under host sanitizers; no network."""
import json
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='desktop-network-core-') as tmp:
 output=Path(tmp)/'check'
 command=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-g',
  '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT),
  str(ROOT/'coordinator.c'),str(ROOT/'test_coordinator.c'),'-o',str(output)]
 subprocess.run(command,check=True)
 result=subprocess.run([str(output)],capture_output=True,text=True,timeout=15,check=True)
 print(json.dumps({'status':'PASS','scope':'real host execution of pure model with synthetic facts; no Darwin/network evidence',
  'compiler':subprocess.check_output(['clang','--version'],text=True).splitlines()[0],
  'sanitizers':['address','undefined'],'stdout':result.stdout,'stderr':result.stderr},indent=2))
