#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Target-only stats around our own finite decoder/broker child family."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

root=Path(__file__).resolve().parent
name=sys.argv[1]
if name not in ['pattern-1080p60-h264','pattern-4k30-hevc']:raise ValueError('Unknown own fixture')
before=root/(name+'-before.txt');after=root/(name+'-after.txt')
if before.exists() or after.exists():raise ValueError('Fresh case required')
def stats(path):
    r=subprocess.run(['sysctl','kern.sf_avd.stats'],capture_output=True,text=True,timeout=8)
    path.write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('Kernel stats unavailable')
stats(before)
child=subprocess.Popen([str(root/'bin/surface-session'),str(root/'bin/iosurfaced'),
    str(root/'bin/capture-probe'),str(root/'fixtures'/(name+'.mp4'))],start_new_session=True)
try:
    try:status=child.wait(timeout=65)
    except subprocess.TimeoutExpired:
        # Only our new process group, never another app/broker.
        os.killpg(child.pid,signal.SIGKILL);status=child.wait()
        print('CAPTURE_FAIL own decoder deadline',flush=True)
finally:stats(after)
raise SystemExit(status if status>=0 else 128-status)
