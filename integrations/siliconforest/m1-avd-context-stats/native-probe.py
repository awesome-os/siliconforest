#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Owned live-context diagnostic gate: no decode/GPU jobs, no input or foreign action."""
import ctypes,errno,fcntl,json,os,signal,struct,subprocess,sys
from pathlib import Path

WIRE=struct.Struct('<IIII11QII2Q')
assert WIRE.size==128
if sys.argv[1:]==['--self-test']:
    b=bytearray(128);struct.pack_into('<Q',b,16,123);struct.pack_into('<Q',b,24,456)
    assert WIRE.unpack(b)[4:6]==(123,456)
    print('PASS 128-byte wire layout/generation16/jobs24; no target opened');sys.exit(0)
if len(sys.argv)!=3 or os.uname().sysname!='Darwin':sys.exit(77)
expected_uuid,record=sys.argv[1:]
actual=subprocess.check_output(['sysctl','-n','kern.kernelcacheuuid'],text=True).strip()
if len(expected_uuid)!=36 or actual.upper()!=expected_uuid.upper():
    print('REFUSED wrong kernel',actual);sys.exit(77)
signal.alarm(30)
data=Path(record).read_bytes()
assert len(data)>=76
magic,kind,length=struct.unpack_from('<III',data)
assert magic==0x4a414450 and kind==1 and length>=64 and len(data)>=12+length
body=data[12:12+length];sps_size=struct.unpack_from('<I',body,20)[0]
assert length==64+sps_size and 0<sps_size<=65536
sps=ctypes.create_string_buffer(body[64:])
fd=os.open('/dev/sfavd',os.O_RDWR|os.O_CLOEXEC);contexts=[];rows=[]
def snapshot(ctx,generation=0):
    q=bytearray(128);struct.pack_into('<IIIIQ',q,0,128,1,ctx,0,generation)
    fcntl.ioctl(fd,0xc0805607,q,True);v=WIRE.unpack(q)
    return {'wire':q,'ctx':v[2],'flags':v[3],'generation':v[4],
        'jobs':v[5],'done':v[6],'errors':v[7],'timeouts':v[8],
        'capture_bytes':v[12],'iova_bytes':v[13],'prepared_bytes':v[14],
        'buffers':v[15],'api_calls':v[17],'api_errors':v[18]}
def refused(q):
    before=bytes(q)
    try:fcntl.ioctl(fd,0xc0805607,q,True)
    except OSError as e:assert e.errno==errno.EINVAL and bytes(q)==before
    else:raise AssertionError('Invalid query accepted')
def create():
    c=bytearray(body[:64]);struct.pack_into('<Q',c,24,ctypes.addressof(sps));struct.pack_into('<I',c,32,0)
    struct.pack_into('<I',c,12,2)  # two captures suffice: this test submits no pictures
    fcntl.ioctl(fd,0xc0405602,c,True);ctx=struct.unpack_from('<I',c,32)[0];contexts.append(ctx)
    q=snapshot(ctx);assert q['generation'] and q['buffers']==struct.unpack_from('<I',c,12)[0]
    sizeimage=struct.unpack_from('<I',c,56)[0]  # format.sizeimage: format starts36
    assert q['capture_bytes']==((sizeimage+16383)//16384)*16384*q['buffers']
    assert 0<=q['prepared_bytes']<=q['capture_bytes'] and q['iova_bytes']==q['capture_bytes']
    assert not any(q[k] for k in ['jobs','done','errors','timeouts','api_calls','api_errors'])
    return q
try:
    info=bytearray(64);fcntl.ioctl(fd,0x40405601,info,True);abi,caps,_,global_contexts=struct.unpack_from('<IIII',info)
    if abi!=1 or not caps&(1<<17):print('REFUSED missing diagnostic capability');sys.exit(77)
    a=create();b=create();assert a['ctx']!=b['ctx'] and a['generation']!=b['generation']
    for field,offset,value in [('size',0,127),('version',4,2),('reserved',108,1)]:
        q=bytearray(a['wire']);struct.pack_into('<I',q,offset,value);refused(q)
    q=bytearray(a['wire']);struct.pack_into('<Q',q,16,a['generation']+1);refused(q)
    child=os.fork()
    if child==0:
        try:refused(bytearray(a['wire']));os._exit(0)
        except BaseException:os._exit(1)
    assert os.waitpid(child,0)[1]==0
    # Deliberately empty DECODE is rejected before a real job. Only API totals
    # should change; this is NOT a decoder throughput or completion test.
    for ctx in [a['ctx'],b['ctx'],b['ctx']]:
        req=bytearray(176);struct.pack_into('<I',req,0,ctx)
        try:fcntl.ioctl(fd,0xc0b05604,req,True)
        except OSError as e:assert e.errno==errno.EINVAL
        else:raise AssertionError('Empty DECODE accepted')
    for before,count in [(a,1),(b,2)]:
        q=snapshot(before['ctx'],before['generation'])
        assert q['api_calls']==q['api_errors']==count
        assert not any(q[k] for k in ['jobs','done','errors','timeouts'])
        rows.append({k:v for k,v in q.items() if k!='wire'})
    old=a
    fcntl.ioctl(fd,0x80045605,struct.pack('<I',a['ctx']));contexts.remove(a['ctx'])
    refused(bytearray(a['wire']))
    newer=create();assert newer['generation']!=old['generation'];refused(bytearray(old['wire']))
    print(json.dumps({'status':'PASS','kernel':actual,'two_owned_contexts':rows,'foreign_pid_task_refused':True,
        'stale_generation_refused':True,'decode_jobs_submitted':0,'global_contexts_before':global_contexts,
        'limits':'Internal captures only; no real decode counters, import, IRQ, GPU fence or post-destroy DART retirement proof'},indent=2))
finally:
    for ctx in contexts:
        try:fcntl.ioctl(fd,0x80045605,struct.pack('<I',ctx))
        except OSError:pass
    os.close(fd)
