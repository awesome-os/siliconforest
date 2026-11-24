#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only initialized-data mapping check for our exact native EOF fixtures.

st_blocks includes extent-tree metadata. F_LOG2PHYS_EXT goes through BLOCKMAP
with flags0 and reports initialized data or -1 (hole/unwritten); it allocates
nothing. This checks initialized EOF tails, not unwritten preallocation or fsck.
"""
import fcntl
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ident, directory = sys.argv[1:]
assert subprocess.check_output(['sysctl', '-n', 'kern.kernelcacheuuid'], text=True).strip() == ident
assert re.fullmatch(r'/private/var/db/sftb-ext4-eof-[0-9]+', directory)
p = Path(directory)
sizes = [1,4095,4096,4097,16383,16384,16385,49151,49152,49153,52208,65535,65536,65537]
expected = {f'{kind}-{n}': n for kind in 'mw' for n in sizes}
expected['resized'] = 52208
assert {x.name for x in p.iterdir()} == expected.keys()
rows = {}
for name, size in expected.items():
    fd = os.open(p / name, os.O_RDONLY | os.O_CLOEXEC)
    try:
        st = os.fstat(fd)
        assert st.st_size == size
        valid = (size + 4095) // 4096
        mappings = []
        for block in range(32):  # covers the entire largest write/resize request
            # XNU sys/fcntl.h: pragma pack(4), unsigned flags + two off_t.
            out = fcntl.fcntl(fd, 65, struct.pack('<Iqq', 0, 4096, block * 4096))
            flags, run, physical = struct.unpack('<Iqq', out)
            assert flags == 0 and run == 4096, (name, block, flags, run)
            assert (physical >= 0) if block < valid else (physical == -1), (name, block, physical)
            mappings.append(physical)
        rows[name] = {'size': size, 'blocks512_including_metadata': st.st_blocks,
            'initialized_data_blocks': valid, 'tail_holes_checked': 32 - valid,
            'physical_by_logical_block': mappings}
    finally:
        os.close(fd)
print(json.dumps({'status': 'PASS', 'files': rows,
    'scope': 'Initialized data to EOF and hole/unwritten tails through128KiB; no unwritten-allocation or whole-filesystem cleanliness proof'}, indent=2))
