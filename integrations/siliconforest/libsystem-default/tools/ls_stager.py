#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""PCIE_STAGER for a boot that replaces a file the base RAM root already has.

tools/pcie_assemble.py's PCIE_EXTRA_FILES gives a replacement entry no `replaces_sha256`,
and the stager then refuses to write over the existing file ("Ext2 file already exists").
This wrapper puts the field back, for /usr/lib/libSystem.B.dylib only, from the default
mapping (the digest of the file in the base RAM root that the default libSystem also
replaces), in this boot's own mapping.json, and then runs the slim stager with the same
arguments. Nothing outside the boot's artifact directory is written.
"""
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
DEFAULT = ROOT / 'artifacts/m1-desktop-010/apcie-port-candidate-003/mapping.json'
SLIM = ROOT / 'integrations/siliconforest/m1-boot-speed/tools/stage_ramdisk_slim.py'
DEST = '/usr/lib/libSystem.B.dylib'


def main():
    argv = sys.argv[1:]
    mapping = Path(argv[argv.index('--mapping') + 1])
    base = next(e for e in json.loads(DEFAULT.read_text())['files'] if e['destination'] == DEST)
    data = json.loads(mapping.read_text())
    for e in data['files']:
        if e['destination'] == DEST and 'replaces_sha256' not in e:
            e['replaces_sha256'] = base['replaces_sha256']
            if 'replaces_metadata' in base:
                e['replaces_metadata'] = base['replaces_metadata']
    mapping.write_text(json.dumps(data, indent=1) + '\n')
    os.execv(sys.executable, [sys.executable, '-B', str(SLIM)] + argv)


if __name__ == '__main__':
    main()
