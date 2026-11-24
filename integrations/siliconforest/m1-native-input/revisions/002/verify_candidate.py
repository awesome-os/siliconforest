#!/usr/bin/env python3
"""Read-only hashes plus patch replay into temporary copies; no build/device."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
BASE = HERE.parents[1]

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    m = json.loads((HERE/'manifest.json').read_text())
    for name, expected in m['owned_files_sha256'].items():
        assert sha(HERE/name) == expected, name
    assert sha(BASE/'candidate.patch') == m['revision001_patch_sha256']
    old = json.loads((BASE/'manifest.json').read_text())
    for name, expected in old['owned_files_sha256'].items():
        assert sha(BASE/name) == expected, 'modified frozen001: '+name
    routes = [('before','publication-readiness.patch'),('baseline','candidate.patch')]
    for start, patch in routes:
        with tempfile.TemporaryDirectory(prefix='native-input-002-replay-') as td:
            tree = Path(td)/'tree'
            shutil.copytree(HERE/start, tree)
            subprocess.run(['git','apply','--check','--whitespace=error',str(HERE/patch)], cwd=tree, check=True)
            subprocess.run(['git','apply','--whitespace=error',str(HERE/patch)], cwd=tree, check=True)
            for name, row in m['files'].items():
                assert sha(tree/name) == row['after_sha256'], name
    print('PASS: frozen001 unchanged;002 hashes; both patch routes replay identically; no SDK/device')

if __name__ == '__main__':
    main()
