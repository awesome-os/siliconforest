#!/usr/bin/env python3
"""Original host-only join tests; actual kernel APIs are never executed."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
HERE=Path(__file__).resolve().parent
class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc=shutil.which('clang');cls.cxx=shutil.which('clang++');assert cls.cc and cls.cxx
        cls.tmp=tempfile.TemporaryDirectory(prefix='npp-host-');cls.out=Path(cls.tmp.name)
        cls.flags=['-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(HERE),'-O1']
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def run_tool(self,args,good=True):
        r=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=90,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
        if good:self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        return r
    def unit(self,name,source=None,extra=()):
        out=self.out/name
        self.run_tool([self.cc,'-std=c11',*self.flags,*extra,source or HERE/'private_policy.c',HERE/'test_private_policy.c','-o',out]);return out
    def test_sanitized_host_and_kernel_defined(self):
        for name,extra in [('host',[]),('kernel-defined-host',['-DKERNEL'])]:
            r=self.run_tool([self.unit(name,extra=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',*extra])]);print(r.stdout.strip())
    def test_ndebug_and_CXX_private_layout_refusals(self):
        r=self.run_tool([self.cc,'-std=c11',*self.flags,'-DNDEBUG','-c',HERE/'test_private_policy.c','-o',self.out/'no.o'],False)
        self.assertNotEqual(r.returncode,0);self.assertIn('tests require assertions',r.stderr)
        r=self.run_tool([self.cxx,'-std=c++17','-x','c++','-fsyntax-only',HERE/'private_policy_internal.h'],False)
        self.assertNotEqual(r.returncode,0);self.assertIn('private C atomics',r.stderr)
    def test_actual_same_owner_join(self):
        root=HERE.parent;objs=[]
        sources=[HERE/'private_policy.c',HERE/'test_reader_fixture.c',root/'native_policy_reader/policy_reader.c',
            root/'native_policy_snapshot/policy_snapshot.c',root/'mcc_snapshot/mcc_snapshot.c',
            root/'owned_control/owned_control.c',root/'native_owned/owned.c',root/'private_lifecycle/lifecycle.c']
        flags=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
        for path in sources:
            obj=self.out/(path.stem+'.o');self.run_tool([self.cc,'-std=c11',*self.flags,*flags,'-c',path,'-o',obj]);objs.append(obj)
        out=self.out/'same-owner'
        self.run_tool([self.cxx,'-std=c++17','-fno-exceptions','-fno-rtti','-pthread',*self.flags,*flags,
            '-I'+str(root/'owned_control/combined_sdk'),root/'owned_control/owned_control_native.cpp',
            root/'native_owned/native_owned.cpp',HERE/'test_same_owner.cpp',*objs,'-o',out])
        for scenario in range(3):
            r=self.run_tool([out,scenario]);self.assertIn('ACTUAL SAME-OWNER JOIN',r.stdout);print('same-owner scenario',scenario,r.stdout.strip())
    def test_compiled_semantic_mutants(self):
        original=(HERE/'private_policy.c').read_text()
        mutants={
          'zero-domain':('!p->dart_domain || ','0 || '),
          'out-alias':('if(overlap(p[i],n[i],p[j],n[j]))','if(0 && overlap(p[i],n[i],p[j],n[j]))'),
          'context-alias':('if(overlap(operations->context,operations->context_bytes,p[i],n[i]))','if(0 && overlap(operations->context,operations->context_bytes,p[i],n[i]))'),
          'allow-retry':('return empty==NPP_CONFIGURED || empty==NPP_FAILED?NPP_USED:NPP_BUSY;','return NPP_BUSY;'),
          'ignore-reader-failure':('if(w->reader_status)goto failed;','if(0 && w->reader_status)goto failed;'),
          'expected-drift':('!expected_equal(expected,&w->captured)','(0 && !expected_equal(expected,&w->captured))'),
          'ops-drift':('!ops_equal(operations,&ops)','(0 && !ops_equal(operations,&ops))'),
          'round-down-end':('(end+BPL_PAGE-1)&~(BPL_PAGE-1)','end&~(BPL_PAGE-1)'),
          'unrounded-base':('uint64_t base=r.base&~(BPL_PAGE-1);','uint64_t base=r.base;'),
          'bridge-holes':('r.base<=w->policy.reserved[count-1].base+w->policy.reserved[count-1].bytes','r.base<=w->policy.reserved[count-1].base+w->policy.reserved[count-1].bytes+BPL_PAGE*8'),
          'drop-reservation':('w->policy.reserved_count=count;','w->policy.reserved_count=count?count-1:0;'),
          'RAM-overlap':('(i && r.base<f->ram[i-1].base+f->ram[i-1].bytes)','(0 && i && r.base<f->ram[i-1].base+f->ram[i-1].bytes)'),
          'ignore-configure-failure':('if(w->configure_status) {rc=NPP_CONFIGURE;goto failed;}','if(0 && w->configure_status) {rc=NPP_CONFIGURE;goto failed;}'),
          'ignore-configured':('w->receipt.configured!=1','0'),
          'ignore-querycount':('w->receipt.query_count!=2*w->receipt.page_count','0'),
          'early-output':('failed:\n','failed:\n    *out=w->staged;\n'),
        }
        # Both checks independently reject this drift; deliberately remove the
        # pair, rather than claiming one redundant survivor as a killed mutant.
        altered=original.replace('memcmp(&w->policy,&w->policy_before,sizeof w->policy)','0').replace('memcmp(&w->receipt.base_policy,&w->policy,sizeof w->policy)','0')
        p=self.out/'policy-drift-pair.c';p.write_text(altered)
        r=self.run_tool([self.unit('policy-drift-pair',p)],False)
        self.assertNotEqual(r.returncode,0);self.assertIn('Assertion',r.stderr)
        print('policy-drift-pair compiled/executed/assertion-killed')
        for name,(a,b) in mutants.items():
            self.assertGreaterEqual(original.count(a),1,name)
            p=self.out/(name+'.c');p.write_text(original.replace(a,b));binary=self.unit(name,p)
            r=self.run_tool([binary],False);self.assertNotEqual(r.returncode,0,name+' survived')
            self.assertIn('Assertion',r.stderr,name+': not an assertion kill');print(name,'compiled/executed/assertion-killed')
if __name__=='__main__':unittest.main(verbosity=2)
