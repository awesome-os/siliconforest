#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite owned GPU/IO diagnostic probes; workstation caller must hold gui.lock."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
assert os.uname().sysname == 'Darwin'
results = []

def run(name, command, env, expected=0, marker=None, timeout=12):
    try:
        r = subprocess.run(command, env=dict(os.environ, **env), capture_output=True,
                           text=True, timeout=timeout)
        record = dict(name=name, status=r.returncode, stdout=r.stdout, stderr=r.stderr,
                      passed=r.returncode == expected and (not marker or marker in r.stdout))
    except subprocess.TimeoutExpired as error:
        record = dict(name=name, timeout=True, passed=False,
                      stdout=(error.stdout or b'').decode(), stderr=(error.stderr or b'').decode())
    results.append(record)
    (root / 'probe-results.json').write_text(json.dumps(results, indent=2) + '\n')

base = '/private/var/db/sffx/'
gl = base + 'gl-v2/Frameworks/OpenGL.framework/Versions/A/OpenGL'
env = dict(DYLD_INSERT_LIBRARIES=base + 'sandbox-v4/libsffx-sandbox.dylib',
           DYLD_LIBRARY_PATH=base + 'gl-v2/lib:/usr/local/lib/mesa-asahi')
for mode, status, marker in [('deny', 1, 'FXCGL failures=2'), ('allow', 0, 'FXCGL failures=0')]:
    run('sandbox-gpu-' + mode, [str(root / 'cgl-probe'), gl, mode], env,
        expected=status, marker=marker)
for name, prefix, library in [
    ('installed-qc', base + 'gl-v2/Frameworks', base + 'gl-v2/lib:/usr/local/lib/mesa-asahi'),
    ('private-qc', base + 'quartz-trace-v2/Frameworks:' + base + 'gl-v2/Frameworks',
     base + 'quartz-trace-v2/Frameworks/QuartzCore.framework/Versions/A:' + base + 'gl-v2/lib:/usr/local/lib/mesa-asahi')]:
    run(name, [str(root / 'layer-probe')], dict(DYLD_FRAMEWORK_PATH=prefix,
        DYLD_LIBRARY_PATH=library), marker='other=0')
for mode in ['normal', 'throttle']:
    run('io-' + mode, [str(root / 'io-probe'), mode, str(root / (mode + '.bin'))], {},
        marker='failed=0', timeout=12)
print(json.dumps(results, indent=2))
