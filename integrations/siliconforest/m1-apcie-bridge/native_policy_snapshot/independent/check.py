#!/usr/bin/env python3
"""Independent synthetic wire corpus + real C callee. No device data/access."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
HERE=Path(__file__).resolve().parent
LEAF=HERE.parent
spec=importlib.util.spec_from_file_location('nps_review_model',LEAF/'facts_wire.py')
w=importlib.util.module_from_spec(spec);sys.modules[spec.name]=w;spec.loader.exec_module(w)
BASE=0x800000000
ENV=BASE+0x1000000
IDENT=bytes(range(1,25))+b''.join(bytes([j])*32 for j in range(1,7))
EXPECTED=w.Expected(IDENT[:24],tuple(IDENT[24+i*32:56+i*32] for i in range(6)))
def wire(extra=(),ram=None,mcc=None,omit=(),boot=None):
    """Original direct packing, does not use production encoder/fixture."""
    if ram is None:ram=[(ENV,0x1000000)]
    if boot is None:boot=(ENV,0x4000)
    if mcc is None:mcc=[0,0,0,0x800100,0x800101,2,0,0,0,0x800200,0x800202,0x80000000]
    records=[]
    for i in range(4):
        a,b,e=mcc[3*i:3*i+3]
        if e:records.append((1,i,0,0,BASE|(a*4096),((b+1)*4096|BASE)-(a*4096|BASE)))
    records.extend([(3,0,0,0,BASE+0x3000000,0x4000),(4,0,0,0,0x690000000,0x1000),(5,0,0,0,ENV+0x8000,0x4000),(6,0,0,0,*boot)])
    records=[r for r in records if r[0] not in omit]+list(extra)
    records.sort(key=lambda r:r[:2]);raw=bytearray(1280+32*len(records));raw[:8]=b'BCMFCT01'
    struct.pack_into('<11I',raw,8,1,len(raw),1,14,36,1,len(ram),len(records),6,384,1)
    raw[64:280]=IDENT
    for off,pair in [(320,(BASE,0x4000000)),(336,(ENV,0x1000000)),(352,boot)]:struct.pack_into('<QQ',raw,off,*pair)
    for i,pair in enumerate(ram):struct.pack_into('<QQ',raw,368+i*16,*pair)
    for tag in range(1,7):
        count=sum(r[0]==tag for r in records);off=496+(tag-1)*48
        struct.pack_into('<HHI',raw,off,tag,2 if count else 1,count);raw[off+8:off+40]=bytes([0x70+tag])*32
    struct.pack_into('<96I',raw,784,*(mcc*8))
    for i,r in enumerate(records):struct.pack_into('<IIIIQQ',raw,1280+i*32,*r)
    return bytes(raw)
def oracle(records,ram):
    """Connected components of intersecting/touching intervals, not sort/merge."""
    ranges=[(r[4],r[4]+r[5]) for r in records if r[0]!=5 and any(max(r[4],a)<min(r[4]+r[5],a+b) for a,b in ram)]
    components=[]
    for span in ranges:
        attached=[c for c in components if any(max(span[0],a)<=min(span[1],b) for a,b in c)]
        components=[c for c in components if c not in attached]
        components.append([span]+[v for c in attached for v in c])
    return sorted([[min(a for a,b in c),max(b for a,b in c)-min(a for a,b in c)] for c in components])
def model(raw,identity=IDENT):
    expected=w.Expected(identity[:24],tuple(identity[24+i*32:56+i*32] for i in range(6)))
    try:o=w.decode(raw,expected)
    except w.Refused:return None
    assert o.status=='STRUCTURALLY_VALID_FACTS_NOT_POLICY'
    assert w.encode(o.facts,expected)==raw
    f=o.facts
    result={'rc':0,'ranges':[[r.base,r.bytes] for r in (f.dram,f.envelope,f.retained_boot)],'ram':[[r.base,r.bytes] for r in f.ram],
            'records':[[r.tag,r.ordinal,r.flags,r.auxiliary,r.span.base,r.span.bytes] for r in f.records],
            'sources':[[s.tag,s.status,s.count] for s in f.sources],
            'mcc':[[i,r.base,r.bytes] for i,r in o.mcc_slots],
            'union':[[r.base,r.bytes] for r in o.exclusion_union]}
    assert result['union']==oracle(result['records'],result['ram'])
    return result
def corpus():
    cases=[]
    def add(label,raw,expected=IDENT,must=None):
        m=model(raw,expected)
        if must is not None:assert (m is not None)==must,label
        cases.append((label,raw,expected,m))
    nominal=wire();add('nominal',nominal,must=True)
    for i in range(len(nominal)):
        for bit in range(8):
            b=bytearray(nominal);b[i]^=1<<bit;add(f'bit-{i}-{bit}',bytes(b))
    for n in range(1280):add(f'short-{n}',nominal[:n],must=False)
    for suffix in (b'\0',b'\0'*32,b'\0'*8000):add('trailing-'+str(len(suffix)),nominal+suffix,must=False)
    for i in range(216):
        e=bytearray(IDENT);e[i]^=0x80;add('expected-mismatch-'+str(i),nominal,bytes(e),False)
    for off,n in [(0,24)]+[(24+i*32,32) for i in range(6)]:
        e=bytearray(IDENT);e[off:off+n]=b'\0'*n;b=bytearray(nominal);b[64+off:64+off+n]=b'\0'*n
        add('both-zero-identity-'+str(off),bytes(b),bytes(e),False)
    for tag in range(6):
        b=bytearray(nominal);off=504+tag*48;b[off]^=0x40;add('opaque-source-label-'+str(tag),bytes(b),must=True)
        b[off:off+32]=bytes(32);add('zero-source-label-'+str(tag),bytes(b),must=False)
    add('empty-mcc-and-other-observable-classes',wire(mcc=[0]*12,omit=(4,5)),must=True)
    for tag in (3,6):add('required-singleton-missing-'+str(tag),wire(omit=(tag,)),must=False)
    ram=[(ENV,0x4000),(ENV+0x4000,0xffc000)]
    add('adjacent-original-RAM-preserved',wire(ram=ram),must=True)
    add('boot-crosses-original-RAM',wire(ram=ram,boot=(ENV,0x8000)),must=False)
    add('full-boundary-no-clipping',wire(extra=[(2,0,0xffffffff,0x12345678,ENV-1,2)]),must=True)
    for n in (7,8):
        extra=[(2,i,0,0,ENV+0x10000+i*0x10000,0x1000) for i in range(n)]
        add('union-'+str(n+1),wire(extra),must=n==7)
    add('all256records',wire([(2,i,0,i,0x600000000+i*0x1000,0x1000) for i in range(250)]),must=True)
    add('records257',wire([(2,i,0,i,0x600000000+i*0x1000,0x1000) for i in range(251)]),must=False)
    for n,(base,size) in enumerate([(0,0),(0xffffffffffffffff,2),(1<<36,1),((1<<36)-1,2)]):
        add('physical-range-overflow-'+str(n),wire([(2,0,0,0,base,size)]),must=False)
    rng=random.Random(0xb57e291)
    for trial in range(2048):
        ram=[(ENV,0x10000),(ENV+0x10000*(1+trial%3),0x40000)]
        records=[(2,i,rng.getrandbits(32),rng.getrandbits(32),ENV+rng.randrange(-2,100)*4096,rng.randrange(1,9)*4096) for i in range(rng.randrange(1,15))]
        raw=wire(records,ram=ram)
        un=oracle([list(r) for r in records]+model(nominal)['records'],[list(r) for r in ram])
        add('independent-union-'+str(trial),raw,must=len(un)<=8)
    return cases

def run(binary,cases):
    stream=b''.join(struct.pack('<I',len(raw))+expected+raw for label,raw,expected,m in cases)
    p=subprocess.run([str(binary)],input=stream,capture_output=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'),timeout=120)
    assert p.returncode==0,p.stderr.decode(errors='replace')[-6000:]
    lines=p.stdout.splitlines();assert len(lines)==len(cases),(len(lines),len(cases))
    for line,(label,raw,expected,m) in zip(lines,cases):
        c=json.loads(line)
        assert (c['rc']==0)==(m is not None),(label,c,m)
        if m is not None:assert c==m,(label,c,m)
    return hashlib.sha256(p.stdout).hexdigest()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path);args=ap.parse_args()
    cc=shutil.which('clang');assert cc
    cases=corpus();print('generated cases',len(cases),flush=True)
    prod=[LEAF/'policy_snapshot.c',LEAF/'policy_snapshot.h',LEAF/'facts_wire.py',LEAF.parent/'mcc_snapshot/mcc_snapshot.c',LEAF.parent/'mcc_snapshot/mcc_snapshot.h']
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in prod}
    with tempfile.TemporaryDirectory(prefix='nps-independent-') as tmp:
        tmp=Path(tmp);exe=tmp/'runner'
        cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(HERE/'runner.c'),str(prod[0]),str(prod[3]),'-o',str(exe)]
        p=subprocess.run(cmd,capture_output=True);assert p.returncode==0,p.stderr.decode()
        digest=run(exe,cases)
    assert hashes=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in prod},'moving source changed during tests; rerun'
    result={'status':'PASS_HOST_SYNTHETIC_INDEPENDENT_C_PYTHON_PARITY_AND_INTERVAL_ORACLE','cases':len(cases),'accepted':sum(c[3] is not None for c in cases),'refused':sum(c[3] is None for c in cases),'alias_and_null_cases':16,'additional_interval_oracles':2048,'source_sha256':hashes,'canonical_C_output_sha256':digest,'atomic_failure_and_input_preservation_asserted':True,'source_declarations_not_authenticated':True,'no_runtime_policy_or_hardware_claim':True}
    if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
