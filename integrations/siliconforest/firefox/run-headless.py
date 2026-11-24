#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Finite target headless timer control/candidate with private homes and real sandbox."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
results = []
for name, framework in [('control', None), ('candidate', '/private/var/db/sffx/appkit-headless-v1/Frameworks')]:
    home = root / ('home-' + name)
    home.mkdir(exist_ok=False)
    env = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home),
               XDG_RUNTIME_DIR='/private/var/run/gpu-desktop', WAYLAND_DISPLAY='wayland-0',
               DYLD_INSERT_LIBRARIES='/private/var/db/sffx/sandbox-v2/libsffx-sandbox.dylib')
    if framework:
        env['DYLD_FRAMEWORK_PATH'] = framework
    else:
        env.pop('DYLD_FRAMEWORK_PATH', None)
    r = subprocess.run([str(root / 'headless-probe')], env=env,
                       capture_output=True, text=True, timeout=8)
    results.append(dict(name=name, status=r.returncode, stdout=r.stdout, stderr=r.stderr))
(root / 'headless-results.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
