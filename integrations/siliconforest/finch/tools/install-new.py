#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install only absent framework trees; journal paths/hashes for uninstall-new.py.
Run on the M1 after imports and CLI checks, with an isolated stage as input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

DB = Path('/private/var/db/sffinch')
DEST = Path('/System/Library/Frameworks')


def identity(path):
    if path.is_symlink():
        return dict(kind='symlink', target=os.readlink(path))
    return dict(kind='file', sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('stage', type=Path)
    ap.add_argument('frameworks', nargs='+')
    args = ap.parse_args()
    if os.uname().sysname != 'Darwin':
        raise SystemExit('Run only on the running Darwin target')
    DB.mkdir(parents=True, exist_ok=True)
    for name in args.frameworks:
        if not name.isalnum():
            raise SystemExit('Framework name must be alphanumeric')
        src = args.stage / 'System/Library/Frameworks' / (name+'.framework')
        dst = DEST / (name+'.framework')
        if not src.is_dir() or os.path.lexists(dst):
            raise SystemExit('Missing source or destination already exists: '+str(dst))
    for name in args.frameworks:
        src = args.stage / 'System/Library/Frameworks' / (name+'.framework')
        dst = DEST / (name+'.framework')
        entries = []
        # Exclusive creation reserves this previously absent framework root.
        dst.mkdir()
        try:
            for path in sorted(src.rglob('*')):
                rel = path.relative_to(src)
                out = dst / rel
                if path.is_dir() and not path.is_symlink():
                    out.mkdir(exist_ok=True)
                    continue
                out.parent.mkdir(parents=True, exist_ok=True)
                if path.is_symlink():
                    target = os.readlink(path)
                    if os.path.isabs(target) or '..' in Path(target).parts:
                        raise ValueError('Unexpected external symlink: '+str(path))
                    out.symlink_to(target)
                else:
                    with path.open('rb') as reader, out.open('xb') as writer:
                        shutil.copyfileobj(reader, writer)
                    out.chmod(0o755 if rel.name == name else 0o644)
                item = dict(path=str(out), framework=str(dst), **identity(out))
                if identity(out) != identity(path):
                    raise ValueError('Copy hash differs: '+str(out))
                entries.append(item)
            with (DB/'installed.txt').open('a') as ledger:
                for entry in entries:
                    ledger.write(json.dumps(entry, sort_keys=True)+'\n')
                ledger.flush()
                os.fsync(ledger.fileno())
        except BaseException:
            shutil.rmtree(dst)
            raise
        print('INSTALLED '+name+' files='+str(len(entries)), flush=True)


if __name__ == '__main__':
    main()
