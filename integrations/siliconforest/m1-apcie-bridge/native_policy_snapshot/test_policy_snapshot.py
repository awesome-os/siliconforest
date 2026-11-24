#!/usr/bin/env python3
"""Compile host C decoder, independent Python/C parity and semantic mutants."""
import ctypes as c
from dataclasses import replace
from pathlib import Path
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode=True
import facts_wire as w
from test_facts_wire import fixture,with_records
HERE=Path(__file__).resolve().parent
class Range(c.Structure):_fields_=[('base',c.c_uint64),('bytes',c.c_uint64)]
class Expected(c.Structure):_fields_=[('nonce',c.c_uint8*24),('sha256',(c.c_uint8*32)*6)]
class Source(c.Structure):_fields_=[('tag',c.c_uint32),('status',c.c_uint32),('count',c.c_uint32),('sha256',c.c_uint8*32)]
class Record(c.Structure):_fields_=[('tag',c.c_uint32),('ordinal',c.c_uint32),('flags',c.c_uint32),('auxiliary',c.c_uint32),('range',Range)]
class Slot(c.Structure):_fields_=[('start',c.c_uint32),('end',c.c_uint32),('enable',c.c_uint32),('enabled',c.c_uint32),('range',Range)]
class MCC(c.Structure):_fields_=[('profile',c.c_uint32),('coverage',c.c_uint32),('passes',c.c_uint32),('planes',c.c_uint32),('slots',c.c_uint32),('enabled_count',c.c_uint32),('union_count',c.c_uint32),('dram',Range),('slot',Slot*4),('excluded',Range*4)]
class Result(c.Structure):_fields_=[(k,c.c_uint32) for k in ('version','board_profile','page_shift','pa_bits','wire_profile','coverage','ram_count','record_count','union_count')]+[('identity',Expected),('dram',Range),('envelope',Range),('retained_boot',Range),('ram',Range*8),('source',Source*6),('raw_mcc',c.c_uint8*384),('mcc',MCC),('record',Record*256),('relevant_union',Range*8)]
class Workspace(c.Structure):_fields_=[('staged',Result),('sorted',Range*256)]
def expected(e):return Expected((c.c_uint8*24).from_buffer_copy(e.nonce),((c.c_uint8*32)*6)(*[(c.c_uint8*32).from_buffer_copy(x) for x in e.identities]))
def span(x):return w.Range(x.base,x.bytes)

class DecoderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc=shutil.which('clang');assert cls.cc
        cls.temp=tempfile.TemporaryDirectory(prefix='native-policy-snapshot-');cls.work=Path(cls.temp.name)
        cls.flags=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(HERE)]
        cls.call([cls.cc,*cls.flags,'-O2','-fPIC','-shared',HERE/'policy_snapshot.c',HERE/'../mcc_snapshot/mcc_snapshot.c','-o',cls.work/'decoder.so'])
        cls.lib=c.CDLL(str(cls.work/'decoder.so'));cls.decode=cls.lib.nps_decode
        cls.decode.argtypes=[c.c_void_p,c.c_size_t,c.POINTER(Expected),c.POINTER(Workspace),c.POINTER(Result)];cls.decode.restype=c.c_int
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    @classmethod
    def call(cls,args,good=True):
        p=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=60,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
        if good:assert p.returncode==0,p.stdout+p.stderr
        return p
    def compare(self,raw,e):
        try:py=w.decode(raw,e)
        except w.Refused:py=None
        buf=c.create_string_buffer(raw);ex=expected(e);work=Workspace();out=Result()
        c.memset(c.byref(out),0xa5,c.sizeof(out));before=bytes(out)
        rc=self.decode(buf,len(raw),c.byref(ex),c.byref(work),c.byref(out))
        self.assertEqual(rc==0,py is not None,(rc,raw.hex()))
        if rc:
            self.assertEqual(bytes(out),before);return
        f=py.facts
        self.assertEqual(out.ram_count,len(f.ram));self.assertEqual(out.record_count,len(f.records))
        self.assertEqual(bytes(out.identity),bytes(ex));self.assertEqual(bytes(out.raw_mcc),f.mcc_raw)
        self.assertEqual((span(out.dram),span(out.envelope),span(out.retained_boot)),(f.dram,f.envelope,f.retained_boot))
        self.assertEqual(tuple(span(out.ram[i]) for i in range(out.ram_count)),f.ram)
        self.assertEqual(tuple(span(out.relevant_union[i]) for i in range(out.union_count)),py.exclusion_union)
        self.assertEqual(tuple((i,span(slot.range)) for i,slot in enumerate(out.mcc.slot) if slot.enabled),py.mcc_slots)
        for actual,want in zip(out.record,f.records):self.assertEqual((actual.tag,actual.ordinal,actual.flags,actual.auxiliary,span(actual.range)),(want.tag,want.ordinal,want.flags,want.auxiliary,want.span))
        for actual,want in zip(out.source,f.sources):self.assertEqual((actual.tag,actual.status,actual.count,bytes(actual.sha256)),(want.tag,want.status,want.count,want.declared_sha256))
    def test_Python_C_seeded_intervals_and_mutated_bytes(self):
        f=fixture();rng=random.Random(0x4378c)
        others=[r for r in f.records if r.tag!=2]
        for _ in range(2048):
            records=[w.Record(2,i,rng.getrandbits(32),rng.getrandbits(32),w.Range(f.envelope.base+rng.randrange(32)*4096,rng.randrange(1,8)*4096)) for i in range(rng.randrange(9))]
            facts=with_records(f,sorted(others+records,key=lambda r:(r.tag,r.ordinal)))
            try:raw=w.encode(facts,f.identity)
            except w.Refused:continue
            self.compare(raw,f.identity)
        raw=w.encode(f,f.identity)
        for _ in range(4096):
            at=rng.randrange(len(raw));bad=bytearray(raw);bad[at]^=1<<rng.randrange(8)
            self.compare(bytes(bad),f.identity)
        for length in (0,80,len(raw)-1,len(raw)+1,w.MAX_BYTES):self.compare((raw+b'\0'*w.MAX_BYTES)[:length],f.identity)
        print('Python/C parity:2048 intervalcases+4096 bytemutations+5framingcases')
    def test_host_and_kernel_defined_sanitizers(self):
        for mode,flags in (('host',[]),('kernel-defined-host',['-DKERNEL'])):
            out=self.work/mode
            self.call([self.cc,*self.flags,'-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',*flags,HERE/'policy_snapshot.c',HERE/'../mcc_snapshot/mcc_snapshot.c',HERE/'test_policy_snapshot.c','-o',out])
            print(mode+':'+self.call([out]).stdout.strip())
    def test_assertions_must_not_be_disabled(self):
        p=self.call([self.cc,*self.flags,'-DNDEBUG','-c',HERE/'test_policy_snapshot.c','-o',self.work/'disabled.o'],False)
        self.assertNotEqual(p.returncode,0);self.assertIn('require assertions',p.stderr)
    def test_compiled_semantic_mutants(self):
        source=(HERE/'policy_snapshot.c').read_text()
        mutants={
            'reserved-header':('!zero(raw+52,12)','0'),
            'identity-nonce':('!equal(raw+64,expected->nonce,NPS_NONCE_BYTES)','0'),
            'last-identity':('i<NPS_IDENTITIES;i++)','i<NPS_IDENTITIES-1;i++)'),
            'unused-RAM-slot':('if(next.base || next.bytes)return NPS_FORMAT;','if(0 && (next.base || next.bytes))return NPS_FORMAT;'),
            'RAM-containment':('!contains(r->envelope,next)','0'),
            'boot-one-original':('if(!boot_contained)return NPS_RANGE;','if(0 && !boot_contained)return NPS_RANGE;'),
            'source-reserved':('!zero(s+40,8)','0'),
            'source-digest-zero':('zero(s+8,NPS_SHA_BYTES)','0'),
            'source-status':('(status==1)!=(count==0)','0'),
            'MCC-slot-identity':('rec->ordinal!=slot','0'),
            'MCC-extent':('!same_range(rec->range,r->mcc.slot[slot].range)','0'),
            'boot-record-extent':('!same_range(rec->range,r->retained_boot)','0'),
            'non-PMAP-flags':('(rec->tag!=NPS_PMAP_IO && (rec->flags || rec->auxiliary))','0'),
            'source-only-remap-authority':('if(rec->tag==NPS_REMAP_SOURCE_ONLY)continue;','if(0 && rec->tag==NPS_REMAP_SOURCE_ONLY)continue;'),
            'union-gap-fill':('next.base<=r->relevant_union[count-1].base+r->relevant_union[count-1].bytes','next.base<=r->relevant_union[count-1].base+r->relevant_union[count-1].bytes+0x10000'),
            'early-output':('if(total!=n ||','*out=*r; if(total!=n ||'),
        }
        for name,(before,after) in mutants.items():
            self.assertEqual(source.count(before),1,name);p=self.work/(name+'.c');p.write_text(source.replace(before,after));out=self.work/name
            self.call([self.cc,*self.flags,'-O1',p,HERE/'../mcc_snapshot/mcc_snapshot.c',HERE/'test_policy_snapshot.c','-o',out])
            r=self.call([out],False);self.assertNotEqual(r.returncode,0,name+' survived');self.assertIn('Assertion',r.stderr,name+': not an assertion kill')
            print(name+': compiled/executed/assertion-killed')

if __name__=='__main__':unittest.main(verbosity=2)
