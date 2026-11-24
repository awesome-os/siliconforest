#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify every own staged file before loading any of our Mach-O images."""
import hashlib,json,runpy,sys
from pathlib import Path
root=Path(__file__).resolve().parent
if len(sys.argv)>2 and sys.argv[1]=='capture':
    rows=json.loads((root/'cold-files.json').read_text())
    for row in rows:
        path=root/row['path']
        if not path.resolve().is_relative_to(root):raise ValueError('Stage path escape')
        if hashlib.file_digest(path.open('rb'),'sha256').hexdigest()!=row['sha256']:
            raise ValueError('Cold stage mismatch: '+row['path'])
    (root/'cold-verified.json').write_text(json.dumps(rows,indent=2)+'\n')
    manifest=json.loads((root/sys.argv[2]).read_text())
    for test in manifest:test['argv']=[sys.executable if arg=='@PYTHON@' else arg for arg in test['argv']]
    (root/'manifest-resolved.json').write_text(json.dumps(manifest,indent=2)+'\n')
    sys.argv[2]=str(root/'manifest-resolved.json')
runpy.run_path(str(root/'compare-tests-engine.py'),run_name='__main__')
