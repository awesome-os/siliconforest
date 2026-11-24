#!/usr/bin/env python3
"""Host-only seam validation and compiled assertion-only semantic mutants."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
HERE=Path(__file__).resolve().parent
class ReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc=shutil.which('clang');assert cls.cc
        cls.temp=tempfile.TemporaryDirectory(prefix='native-policy-reader-');cls.work=Path(cls.temp.name)
        cls.flags=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(HERE)]
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def call(self,args,good=True):
        p=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=60,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1',UBSAN_OPTIONS='halt_on_error=1'))
        if good:self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        return p
    def build(self,name,source,extra=()):
        out=self.work/name
        self.call([self.cc,*self.flags,'-O1',*extra,source,HERE/'../native_policy_snapshot/policy_snapshot.c',HERE/'../mcc_snapshot/mcc_snapshot.c',HERE/'test_policy_reader.c','-o',out])
        return out
    def test_host_and_kernel_defined_seam_sanitizers(self):
        for name,extra in (('host',[]),('kernel-defined-host',['-DKERNEL'])):
            out=self.build(name,HERE/'policy_reader.c',['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',*extra])
            print(name+': '+self.call([out]).stdout.strip())
    def test_NDEBUG_and_native_default_compilation_refuse(self):
        p=self.call([self.cc,*self.flags,'-DNDEBUG','-c',HERE/'test_policy_reader.c','-o',self.work/'disabled.o'],False)
        self.assertNotEqual(p.returncode,0);self.assertIn('require assertions',p.stderr)
        for extra in ([],['-DKERNEL']):
            p=self.call([self.cc,'-x','c++','-std=c++17','-fsyntax-only',*extra,HERE/'policy_reader_native.cpp'],False)
            self.assertNotEqual(p.returncode,0);self.assertIn('genuine ARM64 Darwin kernel compilation required',p.stderr)
    def test_compiled_semantic_mutants(self):
        source=(HERE/'policy_reader.c').read_text()
        mutants={
            'initialized-check':('boot.initialized!=1 || ','0 || '),
            'prefix-length':('boot.count!=NPR_COMMON_PREFIX_BYTES || ','0 || '),
            'borrowed-view-alias':('!view_ok(views[i].bytes,n,operations,expected,w,out)','0'),
            'cross-view-alias':('if(overlap(views[i].bytes,n,views[j].bytes,views[j].count))','if(0 && overlap(views[i].bytes,n,views[j].bytes,views[j].count))'),
            'Revision-consistency':('w->common_prefix[0]!=2 || ','0 || '),
            'Version-consistency':('w->common_prefix[2]!=2 || ','0 || '),
            'chosen-DRAM-base':('w->staged.dram.base!=u64(w->dram_bytes)','0'),
            'chosen-DRAM-size':('w->staged.dram.bytes!=u64(w->dram_bytes+8)','0'),
            'envelope-base':('w->staged.envelope.base!=phys','0'),
            'envelope-size':('w->staged.envelope.bytes!=mem','(0 && w->staged.envelope.bytes!=mem)'),
            'boot-base':('w->staged.retained_boot.base!=phys','0'),
            'top-nonempty':('top<=phys','0'),
            'top-covered':('top>w->staged.retained_boot.base+w->staged.retained_boot.bytes','0'),
            'boot-recheck':('!equal(again.bytes,w->common_prefix,NPR_COMMON_PREFIX_BYTES)','0'),
            'view-recheck':('!equal(next.bytes,saved,next.count)','(0 && !equal(next.bytes,saved,next.count))'),
            'trusted-expected-recheck':('if(!equal((const uint8_t *)expected,(const uint8_t *)&w->expected,sizeof *expected))','if(0 && !equal((const uint8_t *)expected,(const uint8_t *)&w->expected,sizeof *expected))'),
            'cleanup-status':('if(ops.finish(ops.context)!=NPR_OK)return NPR_CLEANUP;','if(ops.finish(ops.context)!=NPR_OK) { /* lost error */ }'),
            'early-output':('finished:\n','finished:\n    *out=w->staged;\n'),
        }
        for name,(old,new) in mutants.items():
            self.assertEqual(source.count(old),1,name);p=self.work/(name+'.c');p.write_text(source.replace(old,new))
            out=self.build(name,p);r=self.call([out],False)
            self.assertNotEqual(r.returncode,0,name+' survived');self.assertIn('Assertion',r.stderr,name+' not assertion-killed')
            print(name+': compiled/executed/assertion-killed')
if __name__=='__main__':unittest.main(verbosity=2)
