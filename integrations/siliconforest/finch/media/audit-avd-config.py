#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read only a SiliconForest record list; report AVD members/flag without identifiers."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image',type=Path)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    source=Path(__file__).resolve().parents[2]/'m1-native-boot/b1/pdsf_pack.py'
    spec=importlib.util.spec_from_file_location('pdsf_pack',source)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    data=a.image.read_bytes();records,_=module.parse(data)
    result=dict(sha256=hashlib.sha256(data).hexdigest(),records_crc_good=all(r[4] for r in records),sfavd_argument=None,sfavd_members=[])
    for kind,flags,stored,raw,crc,offset in records:
        if not crc:raise ValueError('Record CRC failure')
        body=data[offset:offset+stored]
        if flags&module.F_GZIP:body=gzip.decompress(body)
        if len(body)!=raw:raise ValueError('Record size mismatch')
        if kind==module.CONFIG:
            match=re.search(rb'(?:^|\s)sfavd=([^\s]+)',body)
            if match:result['sfavd_argument']=match[1].decode('ascii')
        if kind==module.KERNEL:
            magic,_,_,filetype,ncmds,commandBytes,_,_=struct.unpack_from('<IiiIIIII',body)
            if magic!=0xfeedfacf or filetype!=12:raise ValueError('Not a Mach-O fileset')
            pos=32
            for _ in range(ncmds):
                cmd,size=struct.unpack_from('<II',body,pos)
                if size<8 or pos+size>32+commandBytes:raise ValueError('Invalid load command')
                if cmd==0x80000035:
                    nameOffset=struct.unpack_from('<I',body,pos+24)[0]
                    if nameOffset>=size:raise ValueError('Invalid fileset entry name')
                    name=body[pos+nameOffset:pos+size].split(b'\0',1)[0].decode('ascii')
                    if 'SFAppleAVD' in name:result['sfavd_members'].append(name)
                pos+=size
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
