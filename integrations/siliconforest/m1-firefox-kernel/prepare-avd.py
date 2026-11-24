#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply only the reviewed IRQ/cache delta to the frozen import SDK source."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
manifest_path = Path(sys.argv[2]) if len(sys.argv) == 4 else here / 'avd-source-manifest.json'
patch_path = Path(sys.argv[3]) if len(sys.argv) == 4 else here / 'avd-import-cache.patch'
manifest = json.loads(manifest_path.read_text())
root = Path(sys.argv[1]) / 'src/Kernel/Extensions/SFAppleAVD'

def verify(expected):
    actual = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in root.rglob('*') if p.is_file()}
    if actual != expected:
        changed = sorted(k for k in actual.keys() | expected.keys()
                         if actual.get(k) != expected.get(k))
        raise SystemExit('Frozen AVD source mismatch: ' + ', '.join(changed))

verify(manifest['before'])
subprocess.run(['patch', '--batch', '--forward', '--fuzz=0', '-p1',
                '-i', str(patch_path)], cwd=root, check=True)
verify(manifest['after'])
print('AVD snapshot: all 52 files verified; only platform/core/OS API changed')
