#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Create a cold, one-framework install packet and actual export-superset audit.

Export coverage does not prove ABI compatibility. Select the reviewed variant
and its consumer regressions before using the packet. No SSH or install here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('framework', type=Path)
    ap.add_argument('--before-root', type=Path, required=True)
    ap.add_argument('--under', type=Path, action='append', default=[])
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    here = Path(__file__).resolve().parent
    sys.path.insert(0, str(here.parents[1]/'homebrew'))
    from sf_gapscan import Root
    name = a.framework.name.removesuffix('.framework')
    if not name.isalnum() or a.framework.name != name+'.framework' or a.output.exists():
        raise ValueError('Use a framework directory and fresh output')
    logical = '/System/Library/Frameworks/'+name+'.framework/Versions/A/'+name
    before = Root(a.before_root, under=a.under)
    source_root = a.framework.parents[3]
    after = Root(source_root, under=[a.before_root, *a.under])
    old, new = before.resolve(logical), after.resolve(logical)
    if new is None:
        raise ValueError('Source has no canonical main image')
    old_names = before.exports(logical, old) if old else set()
    new_names = after.exports(logical, new)
    missing = sorted(old_names-new_names)
    if missing:
        raise ValueError('Candidate loses installed exports: '+', '.join(missing))
    a.output.mkdir()
    dest = a.output/a.framework.name
    shutil.copytree(a.framework, dest, symlinks=True)
    entries = []
    for path in sorted(dest.rglob('*')):
        rel = str(path.relative_to(dest))
        if path.is_symlink():
            entries.append(dict(path=rel, kind='symlink', target=str(path.readlink())))
        elif path.is_file():
            entries.append(dict(path=rel, kind='file', sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    audit = dict(before_present=old is not None, before_exports=len(old_names),
                 after_exports=len(new_names), missing_exports=missing,
                 source_main_sha256=hashlib.sha256(new.read_bytes()).hexdigest(),
                 note='Export superset only; not opaque-context/ABI proof. Old readback may have PAC lowered. All new image hashes are from Linux artifacts.')
    (a.output/'manifest.json').write_text(json.dumps(dict(framework=name, entries=entries, audit=audit), indent=2)+'\n')
    shutil.copy2(here/'install-framework.py', a.output)
    print(json.dumps(audit, indent=2))


if __name__ == '__main__':
    main()
