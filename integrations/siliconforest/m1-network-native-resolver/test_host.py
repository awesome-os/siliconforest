#!/usr/bin/env python3
"""Build/run only pure result classifiers under ASan/UBSan. Never a resolver."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='native-resolver-classifiers-') as scratch:
 binary=Path(scratch)/'test_result'
 argv=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-g',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer',str(HERE/'resolver_result.c'),str(HERE/'test_result.c'),'-o',str(binary)]
 subprocess.run(argv,check=True)
 result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
 assert '1001 sanitizer assertions PASS; no DNS execution' in result.stdout
 print(json.dumps({'scope':'Pure classifiers only; no getaddrinfo/DNS/Darwin execution',
  'status':'PASS','assertions':1001,'compiler':subprocess.check_output(['clang','--version'],text=True).splitlines()[0],
  'sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [HERE/'resolver_result.c',HERE/'resolver_result.h',HERE/'test_result.c']},
  'stdout':result.stdout},indent=2))
