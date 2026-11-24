#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Remove journalled Finch files only when their recorded identity still matches."""
import json
import os
from pathlib import Path
from importlib.util import spec_from_file_location, module_from_spec


def main():
    if os.uname().sysname != 'Darwin':
        raise SystemExit('Run only on the running Darwin target')
    spec = spec_from_file_location('installer', Path(__file__).with_name('install-new.py'))
    installer = module_from_spec(spec)
    spec.loader.exec_module(installer)
    ledger = installer.DB/'installed.txt'
    if not ledger.exists():
        return
    entries = [json.loads(line) for line in ledger.read_text().splitlines()]
    for entry in entries:
        path = Path(entry['path'])
        root = Path(entry['framework'])
        if root.parent != installer.DEST or root.suffix != '.framework' or root not in path.parents:
            raise SystemExit('Invalid ledger path: '+str(path))
        expected = {key: entry[key] for key in ('kind', 'target', 'sha256') if key in entry}
        if not os.path.lexists(path) or installer.identity(path) != expected:
            raise SystemExit('Refusing changed/missing file: '+str(path))
    for entry in reversed(entries):
        path = Path(entry['path'])
        path.unlink()
        root = Path(entry['framework'])
        parent = path.parent
        while parent != root.parent:
            try:
                parent.rmdir()
            except OSError:
                break
            parent = parent.parent
    ledger.rename(ledger.with_name('uninstalled.txt'))
    print('UNINSTALLED files='+str(len(entries)))


if __name__ == '__main__':
    main()
