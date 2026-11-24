#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify only the named frozen source files, before/after native patching."""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]);manifest=Path(sys.argv[2]);kind=sys.argv[3];phase=sys.argv[4]
assert kind in ('kernel','kernel_build','dri') and phase in ('before','after')
for name,hashes in json.loads(manifest.read_text())[kind].items():
    actual=hashlib.sha256((root/name).read_bytes()).hexdigest()
    if actual!=hashes[phase]:raise SystemExit('Frozen '+kind+' '+phase+' source mismatch: '+name)
print('DEVFS_SOURCE '+kind+' '+phase+' verified')
