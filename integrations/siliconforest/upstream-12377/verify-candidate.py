#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify candidate membership/source changes against the actual qualified45 artifact."""
from pathlib import Path
import hashlib,json,os,plistlib,struct,sys

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

_,new=map(Path,sys.argv[1:]);baseline=Path(os.environ['SF_SELECTED_BASELINE'])
a,b=modules(baseline/'kernel'),modules(new)
added=['org.siliconforest.driver.SFFBSDem','org.siliconforest.driver.SFFBSDigc'] if os.environ['SF_SELECTED_NETWORK_MODULES']=='1' else []
assert len(a)==57 and len(b)==57+len(added) and sorted(b.keys()-a.keys())==added and not a.keys()-b.keys()
changed=sorted(k for k in a if a[k]!=b[k])
assert changed==['com.apple.iokit.IOPCIFamily','com.ravynos.driver.RavynXHCIPort','org.siliconforest.driver.SFAppleAVD'],changed
assert a['com.apple.kernel']==b['com.apple.kernel'] and not any('ave' in k.lower() for k in b)
aa={r['bundle']:r for r in json.loads((baseline/'nix-support/kc-strip-proof.json').read_text())}
bb={r['bundle']:r for r in json.loads(Path('kc-strip-proof.json').read_text())}
assert len(aa)==56 and len(bb)==56+len(added)
source_changes=sorted(k for k in aa if aa[k]['source']!=bb[k]['source'])
assert source_changes==['IOPCIFamily','IOUSBCompositeDriver','SFAppleAVD','RavynXHCIPort'],source_changes
assert all(r['all_non_debug_sections_identical'] for r in bb.values())
profile=Path(os.environ['SF_SELECTED_PROFILE'])/'System/Library/Extensions'
for path in profile.rglob('Info.plist'):
    for p in plistlib.loads(path.read_bytes()).get('IOKitPersonalities',{}).values():
        if p.get('IOProviderClass')=='IOPCIDevice':assert p.get('IOClass')=='IOPCI2PCIBridge',p
print(json.dumps({'baseline':str(baseline),'candidate':str(new),'baseline_modules':a,'candidate_modules':b,
    'changed_from45':changed,'added':added,'unchanged_from45':54,'source_changes':source_changes,
    'core_identical_to45':True,'new_endpoint_matching':False,'installed':False,'hardware_qualified':False,
    'candidate_sha256':hashlib.sha256(new.read_bytes()).hexdigest()},indent=2))
