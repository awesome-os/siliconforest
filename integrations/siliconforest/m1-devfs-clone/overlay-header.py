#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The kext recipe uses cached XNU headers: overlay only the new devfs API."""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve();source=Path(sys.argv[2]);data=source.read_bytes();proof=[]
for p in root.rglob('devfs.h'):
    if p.parts[-3:]!=('miscfs','devfs','devfs.h'):continue
    resolved=p.resolve()
    assert resolved.is_relative_to(root) and resolved.stat().st_nlink==1,p
    before=hashlib.sha256(resolved.read_bytes()).hexdigest()
    resolved.chmod(resolved.stat().st_mode|0o200);resolved.write_bytes(data)
    proof.append({'header':str(p.relative_to(root)),'before_sha256':before,'after_sha256':hashlib.sha256(data).hexdigest()})
assert 1<=len(proof)<=4,proof
Path('devfs-header-overlay.json').write_text(json.dumps(proof,indent=2)+'\n')
print('DEVFS_HEADER_OVERLAY actual cached SDK headers replaced from verified source:',len(proof))
