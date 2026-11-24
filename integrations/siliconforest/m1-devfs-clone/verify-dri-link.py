#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Require actual DRI code branches through a stub to the new kernel clone API."""
from pathlib import Path
import contextlib,io,json,os,runpy,struct
# Reuse the trusted fileset parser; a devfs-only image keeps the old AVD.
ns={'__name__':'devfs_link_parser'}
source=Path(os.environ['SF_FIREFOX_CACHE_VERIFY']).read_text()
assert source.count("\nks,kr=image(")==1
exec(compile(source.split("\nks,kr=image(",1)[0],'<fileset-parser>','exec'),ns)
image,commands=ns['image'],ns['commands'];data=ns['data'];base=ns['cache_base']
ks,kr=image('com.apple.kernel');ds,read=image('org.siliconforest.driver.SFDRMDri')
expected=ks['_devfs_make_node_clone_with_flags'];calls=[]
for off,cmd,n in commands(ns['modules']['org.siliconforest.driver.SFDRMDri']):
    if cmd!=0x19 or data[off+8:off+24].rstrip(b'\0')!=b'__TEXT_EXEC':continue
    vm,_,_,size=struct.unpack_from('<QQQQ',data,off+24)
    for i in range(0,size,4):
        pc=vm+i;word=struct.unpack('<I',read(pc,4))[0]
        if word&0x7c000000!=0x14000000:continue
        imm=word&0x3ffffff;imm=imm-(1<<26) if imm&(1<<25) else imm;stub=pc+imm*4
        try:
            a,b,c=struct.unpack('<III',read(stub,12))
            if a!=0xd503201f or b&0xff00001f!=0x58000010 or c!=0xd61f0200:continue
            imm=(b>>5)&0x7ffff;imm=imm-(1<<19) if imm&(1<<18) else imm
            pointer=stub+4+imm*4;raw=struct.unpack('<Q',read(pointer,8))[0]
            if (raw>>30)&3 or raw>>63:continue
            resolved=base+(raw&0x3fffffff)
            if resolved==expected:calls.append({'branch':hex(pc),'stub':hex(stub),'pointer':hex(pointer),'resolved':hex(resolved)})
        except AssertionError:continue
assert calls,'DRI registration never reaches the new kernel clone API'
print(json.dumps({'api':'_devfs_make_node_clone_with_flags','actual_linked_calls':calls,
                  'kernel_api_defined':True,'hardware_executed':False},indent=2))
