#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exact module and source-delta check against qualified48; no hardware claim."""
from pathlib import Path
import hashlib,json,os,struct,sys
def modules(path):
    data=path.read_bytes();result={}
    def commands(base):
        assert struct.unpack_from('<I',data,base)[0]==0xfeedfacf
        count,size=struct.unpack_from('<II',data,base+16);at=base+32;end=at+size;assert end<=len(data)
        for _ in range(count):
            cmd,n=struct.unpack_from('<II',data,at);assert n>=8 and at+n<=end;yield at,cmd,n;at+=n
        assert at==end
    for at,cmd,n in commands(0):
        if cmd!=0x80000035:continue
        base=struct.unpack_from('<Q',data,at+16)[0];nameoff=struct.unpack_from('<I',data,at+24)[0]
        assert 32<=nameoff<n;name=data[at+nameoff:data.index(b'\0',at+nameoff,at+n)].decode();assert name not in result
        ids=[data[q+8:q+24].hex() for q,c,z in commands(base) if c==0x1b and z==24];assert len(ids)==1
        result[name]=ids[0]
    return result

_,new=map(Path,sys.argv[1:]);baseline=Path(os.environ['SF_WIFI_BASELINE'])
a,b=modules(baseline/'kernel'),modules(new)
assert len(a)==57 and a.keys()==b.keys()
changed=sorted(k for k in a if a[k]!=b[k])
assert changed==['org.siliconforest.driver.SFAppleAVD'],changed
assert a['com.apple.kernel']==b['com.apple.kernel'] and not any('ave' in k.lower() for k in b)
aa={r['bundle']:r for r in json.loads((baseline/'nix-support/kc-strip-proof.json').read_text())}
bb={r['bundle']:r for r in json.loads(Path('kc-strip-proof.json').read_text())}
assert len(aa)==len(bb)==56
source_changes=sorted(k for k in aa if aa[k]['source']!=bb[k]['source'])
assert source_changes==['SFAppleAVD'],source_changes
assert all(r['all_non_debug_sections_identical'] for r in bb.values())
print(json.dumps({'baseline':str(baseline),'candidate':str(new),'changed_from48':changed,
    'modules':b,'unchanged_from48':56,'source_changes':source_changes,'core_identical_to48':True,
    'installed':False,'hardware_qualified':False,'candidate_sha256':hashlib.sha256(new.read_bytes()).hexdigest()},indent=2))
