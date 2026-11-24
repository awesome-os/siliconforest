#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare actual fileset UUIDs against qualified29, without executing either image."""
import hashlib
import json
from pathlib import Path
import struct
import sys

def modules(path):
    data=path.read_bytes();entries=[];result={}
    def commands(base):
        assert struct.unpack_from('<I',data,base)[0]==0xfeedfacf
        n,size=struct.unpack_from('<II',data,base+16);start=base+32;end=start+size
        assert end<=len(data)
        for _ in range(n):
            cmd,length=struct.unpack_from('<II',data,start)
            assert length>=8 and start+length<=end
            yield start,cmd,length
            start+=length
        assert start==end
    for off,cmd,length in commands(0):
        if cmd==0x80000035:
            assert length>=32
            base=struct.unpack_from('<Q',data,off+16)[0]
            nameoff=struct.unpack_from('<I',data,off+24)[0]
            assert 32<=nameoff<length
            name=data[off+nameoff:data.index(b'\0',off+nameoff,off+length)].decode()
            assert name not in dict(entries);entries.append((name,base))
    for name,base in entries:
        for off,cmd,length in commands(base):
            if cmd==0x1b:
                assert length==24 and name not in result
                result[name]=data[off+8:off+24].hex()
        assert name in result,name
    assert len(result)==57,len(result)
    return result

old,new=map(Path,sys.argv[1:])
a,b=modules(old),modules(new)
assert a.keys()==b.keys()
changes=sorted(k for k in a if a[k]!=b[k])
expected=['com.apple.iokit.IOHIDFamily','org.siliconforest.driver.SFAppleAVD','org.siliconforest.driver.SFM1SPIHID']
assert changes==expected,changes
assert not any('AVE' in k for k in b)
print(json.dumps({'baseline':str(old),'candidate':str(new),'baseline_modules':a,'candidate_modules':b,
                  'changed':changes,'unchanged_modules':54,'module_count':57,
                  'candidate_sha256':hashlib.sha256(new.read_bytes()).hexdigest(),
                  'installed':False,'hardware_qualified':False},indent=2))
