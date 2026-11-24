#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""An explicit replacement for the import-only Force-CMO gate while that driver is excluded."""
from pathlib import Path
import json,struct,sys
b=Path(sys.argv[1]).read_bytes();off=32;found=None
for _ in range(struct.unpack_from('<I',b,16)[0]):
    cmd,n=struct.unpack_from('<II',b,off)
    if cmd==0x80000035:
        where=struct.unpack_from('<Q',b,off+16)[0];p=struct.unpack_from('<I',b,off+24)[0]
        name=b[off+p:b.index(b'\0',off+p,off+n)].decode()
        if name=='org.siliconforest.driver.SFAppleAVD':found=where
    off+=n
assert found is not None
off=found+32;uuids=[]
for _ in range(struct.unpack_from('<I',b,found+16)[0]):
    cmd,n=struct.unpack_from('<II',b,off)
    if cmd==0x1b:uuids.append(b[off+8:off+24].hex())
    off+=n
assert uuids==['d6dafe7af9c336ab93d65fe3926c67bb'],uuids
print(json.dumps({'AVD':'exact CIO29 module UUID','import_driver_included':False,
                  'Force_CMO_import_gate':'not applicable: import/cache candidate excluded after native42 panic',
                  'hardware_executed':False},indent=2))
