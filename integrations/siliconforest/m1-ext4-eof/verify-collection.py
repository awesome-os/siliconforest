#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Require only ext4's UUID/input to change from qualified44; never boots."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

def modules(path):
    data = path.read_bytes()
    result = {}
    def commands(base):
        assert struct.unpack_from('<I', data, base)[0] == 0xfeedfacf
        count, size = struct.unpack_from('<II', data, base + 16)
        at = base + 32
        end = at + size
        assert end <= len(data)
        for _ in range(count):
            cmd, length = struct.unpack_from('<II', data, at)
            assert length >= 8 and at + length <= end
            yield at, cmd, length
            at += length
        assert at == end
    for at, cmd, length in commands(0):
        if cmd != 0x80000035:
            continue
        assert length >= 32
        base = struct.unpack_from('<Q', data, at + 16)[0]
        nameoff = struct.unpack_from('<I', data, at + 24)[0]
        assert 32 <= nameoff < length
        name = data[at + nameoff:data.index(b'\0', at + nameoff, at + length)].decode()
        assert name not in result
        ids = [data[q + 8:q + 24].hex() for q, c, n in commands(base) if c == 0x1b and n == 24]
        assert len(ids) == 1
        result[name] = ids[0]
    assert len(result) == 57
    return result

old, new = map(Path, sys.argv[1:])
baseline = Path(os.environ['SF_EOF_BASELINE_KC'])
a, b, c = modules(old), modules(new), modules(baseline / 'kernel')
assert a.keys() == b.keys() == c.keys()
# The unrelated RavynOS compatibility module also contains "ext4" in its ID.
ext4 = ['com.apple.filesystems.ext4']
assert ext4[0] in a
changes44 = sorted(k for k in b if b[k] != c[k])
assert changes44 == ext4, changes44
changes29 = sorted(k for k in b if b[k] != a[k])
expected = sorted(ext4 + ['com.apple.kernel', 'com.apple.iokit.IOHIDFamily',
    'org.siliconforest.driver.SFM1SPIHID', 'org.siliconforest.driver.SFDRMDri',
    'org.siliconforest.driver.SFAgx', 'org.siliconforest.driver.SFDcp',
    'org.siliconforest.driver.SFAppleSound', 'org.siliconforest.driver.SFAppleAVD'])
assert changes29 == expected, changes29
assert not any('ave' in k.lower() for k in b)
# Source identity before link/relocations, in addition to collection UUIDs.
before = json.loads((baseline / 'nix-support/kc-strip-proof.json').read_text())
after = json.loads(Path('kc-strip-proof.json').read_text())
assert len(before) == len(after) == 56
aa = {r['bundle']: r for r in before}
bb = {r['bundle']: r for r in after}
assert aa.keys() == bb.keys()
assert [k for k in aa if aa[k]['source'] != bb[k]['source']] == ['ext4']
assert all(r['all_non_debug_sections_identical'] for r in after)
print(json.dumps({'baseline': str(old), 'qualified44': str(baseline),
    'candidate': str(new), 'baseline_modules': a, 'candidate_modules': b,
    'changed': changes29, 'changed_from_qualified44': changes44,
    'unchanged_modules': 48, 'module_count': 57,
    'other55_kext_sources_identical_to44': True,
    'candidate_sha256': hashlib.sha256(new.read_bytes()).hexdigest(),
    'installed': False, 'hardware_qualified': False,
    'priority': 'ext4 EOF allocation bound; qualified44 otherwise retained'}, indent=2))
