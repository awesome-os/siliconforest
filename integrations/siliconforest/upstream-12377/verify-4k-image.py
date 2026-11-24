#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only comparison of two regular image files; never opens a device."""
from pathlib import Path
import argparse,hashlib,json,stat,struct,zlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('before',type=Path);p.add_argument('after',type=Path);a=p.parse_args()
for path in [a.before,a.after]:assert stat.S_ISREG(path.lstat().st_mode),'Regular images only'
assert a.before.stat().st_size==a.after.stat().st_size
def table(path,bs):
    with path.open('rb') as f:
        headers=[];tables=[]
        for offset in [bs,path.stat().st_size-bs]:
            f.seek(offset);h=f.read(bs);assert h[:8]==b'EFI PART'
            n=struct.unpack_from('<I',h,12)[0];assert 92<=n<=bs
            assert zlib.crc32(h[:16]+bytes(4)+h[20:n])==struct.unpack_from('<I',h,16)[0]
            me,alt,first,last=struct.unpack_from('<QQQQ',h,24);assert me==offset//bs
            entry,count,size,crc=struct.unpack_from('<QIII',h,72);assert size==128 and count==128
            f.seek(entry*bs);t=f.read(count*size);assert len(t)==count*size and zlib.crc32(t)==crc
            headers.append((me,alt,first,last,h[56:72]));tables.append(t)
    assert tables[0]==tables[1] and headers[0][0]==headers[1][1] and headers[0][1]==headers[1][0]
    assert headers[0][4]==headers[1][4]
    return headers[0],tables[0]
old,ot=table(a.before,512);new,nt=table(a.after,4096);assert old[4]==new[4]
def digest(path,start,nbytes):
    h=hashlib.sha256()
    with path.open('rb') as f:
        f.seek(start)
        while nbytes:
            data=f.read(min(nbytes,8<<20));assert data;h.update(data);nbytes-=len(data)
    return h.hexdigest()
rows=[]
for i in range(128):
    x=ot[i*128:(i+1)*128];y=nt[i*128:(i+1)*128]
    assert x[:32]==y[:32] and x[48:]==y[48:]
    if x[:16]==bytes(16):continue
    lo,hi=struct.unpack_from('<QQ',x,32);ln,hn=struct.unpack_from('<QQ',y,32)
    assert lo*512==ln*4096 and (hi+1)*512==(hn+1)*4096
    assert new[2]<=ln<=hn<=new[3]
    nbytes=(hi+1-lo)*512;h=digest(a.before,lo*512,nbytes)
    assert digest(a.after,ln*4096,nbytes)==h
    rows.append({'partition':i+1,'bytes':nbytes,'sha256':h})
print(json.dumps({'status':'PASS','partitions':rows,'both_header_and_table_crcs':True,'partition_bytes_preserved':True,
    'limits':'Regular-image geometry/content only; not a boot test, physical disk modification or installer.'},indent=2))
