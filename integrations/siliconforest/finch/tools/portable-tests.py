#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Use public framework install names in our Linux-built comparison programs.

PD's plain /usr/lib/lib{Core,}Foundation.dylib names do not describe the
controller Mac's public frameworks. SiliconForest has canonical wrappers for
both. Rewrite only our test images, preserving imports/ordinals and ad-hoc hashes.
"""
import hashlib
import json
from pathlib import Path
import runpy
import sys

api = runpy.run_path(sys.argv[1])
mapping = {
    '/System/Library/Frameworks/AppKit.framework/Versions/A/AppKit': '/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit',
    '/usr/lib/libFoundation.dylib': '/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation',
    '/usr/lib/libCoreFoundation.dylib': '/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation',
}
rows = []
for path in sorted(Path(sys.argv[2]).iterdir()):
    before = path.read_bytes()
    after, changed = api['rewrite_names'](before, lambda name: mapping.get(name, name))
    after = api['resign'](after)[0]
    if api['verify'](after):
        raise ValueError('Invalid ad-hoc page hashes: '+str(path))
    path.chmod(0o755)
    path.write_bytes(after)
    rows.append(dict(name=path.name, framework_names_changed=changed,
                     input_sha256=hashlib.sha256(before).hexdigest(),
                     sha256=hashlib.sha256(after).hexdigest()))
Path(sys.argv[3]).write_text(json.dumps(rows, indent=2)+'\n')
