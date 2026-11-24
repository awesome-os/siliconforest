#!/usr/bin/env python3
"""Original bounded algorithm tests; native adapter uses TEST SDK only here."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
CORE=HERE.parent/'private_lifecycle/lifecycle.c'
MUTATIONS={
    'skip-mutex-pages':('if(!rc)rc=collect(w,controls[1]);','if(0)rc=collect(w,controls[1]);'),
    'pointer-size-mutex':('struct bpl_range mutex={source.mutex_cpu,source.mutex_size};','struct bpl_range mutex={source.mutex_cpu,sizeof(void*)};'),
    'first-page-only':('if(at==last)break;','if(at==last || 1)break;'),
    'wrong-shift12':('uint64_t pa=(uint64_t)ppnum<<14;','uint64_t pa=(uint64_t)ppnum<<12;'),
    'shift-before-widen':('uint64_t pa=(uint64_t)ppnum<<14;','uint64_t pa=ppnum<<14;'),
    'truncate-ppnum':('uint64_t pa=(uint64_t)ppnum<<14;','uint64_t pa=(uint16_t)ppnum<<14;'),
    'zero-ppnum':('if(!ppnum || ppnum>0x3fffffu)','if(ppnum>0x3fffffu)'),
    'skip-RAM-membership':('if(!contained)return 0;','if(!contained && 0)return 0;'),
    'skip-reservation':('if(overlap(pa,BPL_PAGE,p->reserved[i].base,p->reserved[i].bytes))','if(overlap(pa,BPL_PAGE,p->reserved[i].base,p->reserved[i].bytes) && 0)'),
    'skip-second-pass':('pass<2;pass++','pass<1;pass++'),
    'ignore-mapping-change':('if(ppnum!=w->first[i])return BCP_CHANGED;','if(0)return BCP_CHANGED;'),
    'ignore-map-identity':('a->pmap_identity==b->pmap_identity &&','1 &&'),
    'bridge-PA-holes':('v[i].base<=v[count-1].base+v[count-1].bytes','v[i].base<=v[count-1].base+v[count-1].bytes+BPL_PAGE'),
    'drop-base-reservations':('n=union_ranges(w->union_ranges,n+e->base_policy.reserved_count);','n=union_ranges(w->union_ranges,n);'),
    'premature-complete-output':('memset(w,0,sizeof *w);w->original_policy=captured_policy;','out->complete=1;memset(w,0,sizeof *w);w->original_policy=captured_policy;'),
    'workspace-output-alias':('aliases(w,sizeof *w,out,sizeof *out))','0)'),
    'output-before-configure':('if(captured.configure(captured.reads.context,','*out=w->candidate; if(captured.configure(captured.reads.context,'),
    'ignore-receipt-provenance':('b->protection.provenance!=a->protection.provenance ||','0 ||'),
}
NATIVE_MUTATIONS={
    'native-getter-bypass':('lck_mtx_t *mutex=IOLockGetMachLock(reinterpret_cast<IOLock *>(static_cast<uintptr_t>(opaque)));','lck_mtx_t *mutex=reinterpret_cast<lck_mtx_t *>(static_cast<uintptr_t>(opaque));'),
    'no-pin-once-guard':('if(pin_attempted)return BCP_NATIVE;','if(pin_attempted && false)return BCP_NATIVE;'),
}


class ControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc=shutil.which('clang');cls.cxx=shutil.which('clang++')
        if not cls.cc or not cls.cxx:raise RuntimeError('clang and clang++ required')
        cls.tmp=tempfile.TemporaryDirectory(prefix='bcp-tests-');cls.out=Path(cls.tmp.name)
        cls.flags=['-Wall','-Wextra','-Werror','-Wpedantic','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(HERE)]

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def tool(self,args):
        return subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=60,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))

    def compile(self,args):
        p=self.tool(args);self.assertEqual(p.returncode,0,p.stdout+p.stderr)

    def algorithm(self,name,source=None,extra=()):
        out=self.out/name
        self.compile([self.cc,'-std=c11',*self.flags,*extra,source or HERE/'control_protection.c',CORE,HERE/'test_control.c','-o',out])
        return out

    def native(self,name,source=None):
        objs=[]
        for key,path in [('algorithm',HERE/'control_protection.c'),('core',CORE)]:
            obj=self.out/(name+'-'+key+'.o');self.compile([self.cc,'-std=c11',*self.flags,'-c',path,'-o',obj]);objs.append(obj)
        out=self.out/name
        self.compile([self.cxx,'-std=c++17','-fno-exceptions','-fno-rtti',*self.flags,'-I'+str(HERE/'test_sdk'),
            source or HERE/'control_native.cpp',HERE/'test_native.cpp',*objs,'-o',out])
        return out

    def test_algorithm_host_and_kernel(self):
        for name,flags in [('host',[]),('host-kernel',['-DKERNEL'])]:
            p=self.tool([self.algorithm(name,extra=flags)]);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn('CONTROL PROTECTION HOST PASS',p.stdout);print(name+': '+p.stdout.strip())

    def test_native_adapter_under_test_sdk(self):
        binary=self.native('native')
        for scenario in range(9):
            with self.subTest(scenario=scenario):
                p=self.tool([binary,scenario]);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                self.assertIn('ACTUAL CONTROL ADAPTER WITH TEST SDK PASS',p.stdout)
        print('actual native adapter / TEST SDK scenarios=9')

    def test_compiled_algorithm_mutants(self):
        original=(HERE/'control_protection.c').read_text()
        for name,(before,after) in MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before),1,name)
                path=self.out/(name+'.c');path.write_text(original.replace(before,after))
                p=self.tool([self.algorithm(name,path)]);self.assertNotEqual(p.returncode,0,'survived '+name)
                self.assertIn('Assertion',p.stderr,p.stdout+p.stderr)
        print('compiled algorithm mutants rejected='+str(len(MUTATIONS)))

    def test_compiled_native_mutants(self):
        original=(HERE/'control_native.cpp').read_text()
        for name,(before,after) in NATIVE_MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before),1,name)
                path=self.out/(name+'.cpp');path.write_text(original.replace(before,after))
                p=self.tool([self.native(name,path),0]);self.assertNotEqual(p.returncode,0,'survived '+name)
                self.assertIn('Assertion',p.stderr,p.stdout+p.stderr)
        print('compiled native mutants rejected='+str(len(NATIVE_MUTATIONS)))

    def test_ndebug_refusal(self):
        p=self.tool([self.cc,'-std=c11',*self.flags,'-DNDEBUG','-c',HERE/'test_control.c','-o',self.out/'refused.o'])
        self.assertNotEqual(p.returncode,0);self.assertIn('control tests require assertions',p.stderr)


if __name__=='__main__':unittest.main(verbosity=2)
