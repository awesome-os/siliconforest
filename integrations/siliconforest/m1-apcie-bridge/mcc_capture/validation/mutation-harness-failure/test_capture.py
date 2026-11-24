"""Synthetic callbacks only; compiled frozen C relative-plan oracle."""
from dataclasses import replace, FrozenInstanceError
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
SOURCE=Path(os.environ.get('MCC_CAPTURE_TEST_SOURCE',HERE/'capture.py'))
spec=importlib.util.spec_from_file_location('capture_under_test',SOURCE)
c=importlib.util.module_from_spec(spec);sys.modules[spec.name]=c;spec.loader.exec_module(c)

def fixture():
    e=c.Expected(b'A'*32,b'N'*24)
    r=c.Region(1,'/arm-io/mcc','mcc,t8103',1,0x200000000,0x100000,e.adt_sha256,e.nonce)
    return r,e

class CaptureTests(unittest.TestCase):
    def test_exact_frozen_C_plan(self):
        cc=shutil.which('clang');self.assertTrue(cc)
        with tempfile.TemporaryDirectory(prefix='mcc-capture-oracle-') as t:
            t=Path(t);h=t/'plan.c';exe=t/'plan'
            h.write_text('#include "mcc_snapshot.h"\n#include <assert.h>\n#include <stdio.h>\nint main(void){for(unsigned i=0;i<96;i++){struct mcp_read r;assert(mcp_plan_read(1,i,0xc06dc,&r)==MCP_OK);printf("%llu\\n",(unsigned long long)r.offset);}return 0;}\n')
            mcc=HERE.parent/'mcc_snapshot'
            p=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(mcc),str(h),str(mcc/'mcc_snapshot.c'),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr)
            actual=tuple(map(int,p.stdout.splitlines()))
            self.assertEqual(c.relative_plan(1,0xc06dc),actual)
            self.assertEqual(len(actual),96);self.assertEqual(actual[47]+4,0xc06dc)
            self.assertEqual(actual[:48],actual[48:])

    def test_nominal_raw_is_immutable_and_unvalidated(self):
        r,e=fixture();seen=[]
        def read(off):seen.append(off);return off
        owner=c.Collector(r,e,read);o=owner.capture()
        self.assertEqual(o.status,'RAW_COMPLETE_UNVALIDATED')
        self.assertEqual((o.attempted,o.completed,len(o.raw)),(96,96,384))
        self.assertEqual(tuple(seen),c.relative_plan(1,r.extent))
        self.assertEqual(o.prefix,o.raw);self.assertTrue(o.passes_equal);self.assertFalse(o.planes_equal)
        self.assertIs(type(o.raw),bytes);self.assertIsNot(o.region,r);self.assertIsNot(o.expected,e)
        with self.assertRaises(FrozenInstanceError):o.status='valid'
        self.assertEqual(owner.capture().status,'STATE_REFUSED');self.assertEqual(len(seen),96);self.assertIs(owner.last,o)
        words=[9,9,1]+[0]*9
        count=0
        def semantic_bad(off):
            nonlocal count
            v=words[count%12];count+=1;return v
        bad=c.Collector(r,e,semantic_bad).capture()
        self.assertEqual(bad.status,'RAW_COMPLETE_UNVALIDATED');self.assertTrue(bad.planes_equal);self.assertTrue(bad.passes_equal)
        self.assertEqual(struct.unpack_from('<3I',bad.raw),(9,9,1))
        count=0
        def unstable(off):
            nonlocal count
            v=count;count+=1;return v
        changed=c.Collector(r,e,unstable).capture()
        self.assertEqual(changed.status,'RAW_COMPLETE_UNVALIDATED');self.assertFalse(changed.passes_equal)
        self.assertEqual(len(changed.raw),384)

    def test_preflight_no_callback(self):
        r,e=fixture();calls=[]
        invalid=[(None,e),(r,None),(replace(r,profile=True),e),(replace(r,profile=2),e),
          (replace(r,node_path='/arm-io/mcc/'),e),(replace(r,compatible='mcc,t8112'),e),
          (replace(r,reg_index=0),e),(replace(r,reg_index=2),e),(replace(r,reg_index=True),e),
          (replace(r,physical_base=True),e),(replace(r,physical_base=0),e),(replace(r,physical_base=-1),e),
          (replace(r,physical_base=(1<<36)),e),(replace(r,physical_base=0x200000001),e),
          (replace(r,physical_base=(1<<36)-4),e),(replace(r,extent=0xc06d8),e),
          (replace(r,extent=0xc06df),e),(replace(r,extent=1<<64),e),(replace(r,extent=-1),e),
          (replace(r,extent=True),e),(replace(r,adt_sha256=bytes(32)),e),
          (replace(r,adt_sha256=bytearray(b'A'*32)),e),(replace(r,nonce=bytes(24)),e),
          (r,replace(e,adt_sha256=b'B'*32)),(r,replace(e,nonce=b'K'*24)),
          (r,replace(e,nonce=b'N'*23)),(r,replace(e,adt_sha256=bytes(32)))]
        for rr,ee in invalid:
            with self.subTest(region=rr,expected=ee):
                owner=c.Collector(rr,ee,lambda off:calls.append(off));o=owner.capture()
                self.assertEqual((o.attempted,o.completed,o.prefix,o.raw),(0,0,b'',None));self.assertNotEqual(o.status,'RAW_COMPLETE_UNVALIDATED')
                self.assertEqual(owner.capture().status,'STATE_REFUSED')
        self.assertEqual(calls,[])
        self.assertEqual(c.Collector(r,e,None).capture().status,'READER_TYPE')
        for profile,extent in [(True,0xc06dc),(2,0xc06dc),(1,0xc06db),(1,1<<64),(1,-1),(1,True)]:
            with self.assertRaises(ValueError):c.relative_plan(profile,extent)

    def test_all_fault_positions_and_value_widths(self):
        for at in range(96):
            for fault in ['throw','bool','negative','wide','float','none']:
                r,e=fixture();seen=[]
                def read(off):
                    index=len(seen);seen.append(off)
                    if index==at:
                        if fault=='throw':raise OSError('private details must not leak')
                        return {'bool':True,'negative':-1,'wide':1<<32,'float':1.0,'none':None}[fault]
                    return 0xffffffff
                owner=c.Collector(r,e,read);o=owner.capture()
                with self.subTest(at=at,fault=fault):
                    self.assertEqual(o.attempted,at+1);self.assertEqual(o.completed,at)
                    self.assertEqual(len(seen),at+1);self.assertEqual(o.prefix,b'\xff'*(at*4));self.assertIsNone(o.raw)
                    self.assertEqual(o.status,'READ_EXCEPTION' if fault=='throw' else 'READ_VALUE')
                    owner.capture();self.assertEqual(len(seen),at+1)

    def test_all_binding_change_positions(self):
        for at in range(96):
            for change in ['region','expected','reader','object','badtype','throw-change']:
                r,e=fixture();seen=[];owner=None
                def read(off):
                    index=len(seen);seen.append(off)
                    if index==at:
                        if change in ('region','throw-change'):object.__setattr__(r,'extent',r.extent+4)
                        elif change=='expected':object.__setattr__(e,'nonce',b'Z'*24)
                        elif change=='reader':owner._reader=lambda x:0
                        elif change=='object':owner._region=replace(r)
                        elif change=='badtype':object.__setattr__(r,'reg_index',True)
                        if change=='throw-change':raise ValueError('private')
                    return 0x12345678
                owner=c.Collector(r,e,read);o=owner.capture()
                with self.subTest(at=at,change=change):
                    self.assertEqual(o.status,'BINDING_CHANGED');self.assertEqual(len(seen),at+1)
                    self.assertEqual(o.attempted,at+1);self.assertEqual(o.completed,at+(change!='throw-change'))
                    self.assertIsNone(o.raw);self.assertEqual(len(o.prefix),o.completed*4)
                    self.assertEqual(o.region.extent,0x100000);self.assertEqual(o.expected.nonce,b'N'*24)
                    owner.capture();self.assertEqual(len(seen),at+1)

    def test_reentry_abort_and_last_binding(self):
        r,e=fixture();seen=[];owner=None
        def read(off):
            seen.append(off);self.assertEqual(owner.capture().status,'STATE_REFUSED');return 42
        owner=c.Collector(r,e,read);o=owner.capture()
        self.assertEqual((o.status,o.attempted,o.completed),('REENTRY',1,1));self.assertIsNone(o.raw);self.assertEqual(len(seen),1)
        def abort(off):raise KeyboardInterrupt()
        owner=c.Collector(r,e,abort)
        with self.assertRaises(KeyboardInterrupt):owner.capture()
        self.assertEqual((owner.last.status,owner.last.attempted,owner.last.completed),('CALLBACK_ABORTED',1,0))
        self.assertEqual(owner.capture().status,'STATE_REFUSED')
        # Before first read: supplied source identity changed since construction.
        owner=c.Collector(r,e,lambda off:self.fail('must not access'))
        object.__setattr__(e,'nonce',b'Y'*24)
        self.assertEqual(owner.capture().status,'SOURCE_BINDING')

    def test_compiled_python_mutants(self):
        if os.environ.get('MCC_CAPTURE_TEST_SOURCE'):self.skipTest('avoid recursive mutation runner')
        source=SOURCE.read_text()
        mutants={
          'wrong-reg-index':('REG_INDEX = 1','REG_INDEX = 0'),
          'wrong-plane-stride':('plane * 0x40000 +','plane * 0x20000 +'),
          'one-pass':('for _pass in range(2)','for _pass in range(1)'),
          'prefix-before-read':('attempted += 1  # Journal','attempted += 0  # Journal'),
          'bool-u32':('type(value) is int and','isinstance(value, int) and'),
          'no-after-binding':('reason = unchanged()\n                if not _uint(value, 1 << 32):','reason = None\n                if not _uint(value, 1 << 32):'),
          'no-full-source-match':('if region.adt_sha256 != expected.adt_sha256 or region.nonce != expected.nonce:','if False:'),
          'retry-after-failure':("self._state = 'DONE'","self._state = 'NEW'"),
          'no-reentry-poison':('self._violation = True','self._violation = False'),
          'wrong-stability':('passes = raw[:192] == raw[192:]', 'passes = True'),
          'wrong-planes':('for p in range(2) for q in range(1, 4)', 'for p in range(2) for q in range(1, 1)'),
          'wrong-byte-order':("struct.pack('<96I', *words)","struct.pack('>96I', *words)"),
        }
        with tempfile.TemporaryDirectory(prefix='mcc-capture-mutants-') as tmp:
            for name,(old,new) in mutants.items():
                self.assertEqual(source.count(old),1,name)
                changed=source.replace(old,new);compile(changed,name,'exec')
                p=Path(tmp)/(name+'.py');p.write_text(changed)
                run=subprocess.run([sys.executable,'-B',str(Path(__file__))],env=dict(os.environ,MCC_CAPTURE_TEST_SOURCE=str(p)),capture_output=True,text=True,timeout=60)
                self.assertNotEqual(run.returncode,0,'SURVIVED '+name)
                self.assertIn('AssertionError',run.stderr,'nonassertion failure '+name)
                print('compiled Python mutant assertion-killed:',name,flush=True)

if __name__=='__main__':unittest.main(verbosity=2)
