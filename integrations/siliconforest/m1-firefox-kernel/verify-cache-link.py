#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Follow the final AVD ARM64 BL/stub/chained pointer to the kernel Force CMO."""
from pathlib import Path
import json
import struct as st
import sys

data=Path(sys.argv[1]).read_bytes()
def commands(base):
    assert st.unpack_from('<I',data,base)[0]==0xfeedfacf
    off=base+32
    for _ in range(st.unpack_from('<I',data,base+16)[0]):
        cmd,n=st.unpack_from('<II',data,off);assert n>=8 and off+n<=len(data)
        yield off,cmd,n;off+=n
modules={};cache_base=None;fixups=None
for off,cmd,n in commands(0):
    if cmd==0x80000035:
        base=st.unpack_from('<Q',data,off+16)[0];a=st.unpack_from('<I',data,off+24)[0]
        modules[data[off+a:data.index(b'\0',off+a,off+n)].decode()]=base
    elif cmd==0x19 and data[off+8:off+24].rstrip(b'\0')==b'__TEXT':cache_base=st.unpack_from('<Q',data,off+24)[0]
    elif cmd==0x80000034:fixups=st.unpack_from('<II',data,off+8)
assert cache_base==0xfffffe0006000000 and fixups
# dyld_chained_starts_in_segment.pointer_format must be 64_KERNEL_CACHE (8).
fbase=fixups[0];starts=fbase+st.unpack_from('<I',data,fbase+4)[0]
formats=set()
for i in range(st.unpack_from('<I',data,starts)[0]):
    offset=st.unpack_from('<I',data,starts+4+i*4)[0]
    if offset:formats.add(st.unpack_from('<H',data,starts+offset+6)[0])
assert formats=={8},formats

def image(name):
    base=modules[name];adjust=0 if name=='com.apple.kernel' else base
    segments=[];symbols={}
    for off,cmd,n in commands(base):
        if cmd==0x19:
            vm,size,fo,fs=st.unpack_from('<QQQQ',data,off+24);segments.append((vm,size,fo+adjust,fs))
        elif cmd==2:
            sym,count,strings,length=st.unpack_from('<IIII',data,off+8);sym+=adjust;strings+=adjust
            for i in range(count):
                ix,typ,sec,desc,value=st.unpack_from('<IBBHQ',data,sym+i*16)
                if ix and typ&0xe==0xe:
                    key=data[strings+ix:data.index(b'\0',strings+ix,strings+length)].decode()
                    symbols[key]=value
    def read(vm,size):
        matches=[fo+vm-start for start,_,fo,fs in segments if start<=vm and vm+size<=start+fs]
        assert len(matches)==1,(name,hex(vm),size)
        return data[matches[0]:matches[0]+size]
    return symbols,read

ks,kr=image('com.apple.kernel');avd,ar=image('org.siliconforest.driver.SFAppleAVD')
start=avd['_sf_avd_os_dma_sync'];end=min(v for v in avd.values() if v>start)
calls=[]
for offset in range(0,end-start,4):
    word=st.unpack('<I',ar(start+offset,4))[0]
    if word&0xfc000000==0x94000000:
        imm=word&0x3ffffff;imm=imm-(1<<26) if imm&(1<<25) else imm
        calls.append((start+offset,start+offset+imm*4))
assert len(calls)==1,calls
call,stub=calls[0]
a,b,c=st.unpack('<III',ar(stub,12))
# kc-builder relaxed the import stub to NOP; LDR-literal x16; BR x16.
assert a==0xd503201f and b&0xff00001f==0x58000010 and c==0xd61f0200
imm=(b>>5)&0x7ffff;imm=imm-(1<<19) if imm&(1<<18) else imm
pointer=stub+4+imm*4
raw=st.unpack('<Q',ar(pointer,8))[0]
assert (raw>>30)&3==0 and raw>>63==0 # cacheLevel0, unauthenticated
resolved=cache_base+(raw&0x3fffffff)
assert resolved==ks['_CleanPoC_DcacheRegion_Force']
assert kr(resolved,12).hex()=='fd7bbfa9fd030091e007bfa9'
print(json.dumps({'cache_function':'_CleanPoC_DcacheRegion_Force','AVD_BL':hex(call),
                  'stub':hex(stub),'chained_pointer':hex(pointer),
                  'pointer_format':8,'resolved_kernel_function':hex(resolved),
                  'actual_link_to_existing_Force_CMO':True,'hardware_executed':False},indent=2))
