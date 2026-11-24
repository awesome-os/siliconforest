#!/usr/bin/env python3
"""Original host ownership tests plus actual adapter under TEST SDK stand-ins."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
FROZEN = HERE.parent/'private_lifecycle'
MUTATIONS = {
    'retire-before-completes': (
        'j->phase=BPN_RETIRING;',
        'j->phase=BPN_RETIRING; (void)bpl_retire_private(&m->registry,&j->lease);'),
    'partial-cohort-reset': ('r->pair_balanced=1;',
        'r->pair_balanced=1; (void)call(m,j,i,BPN_RESET_REF,0,NULL,0,&result);'),
    'extra-constructor-complete': ('r->pair_balanced=1;',
        '(void)call(m,j,i,BPN_COMPLETE,0,NULL,0,&result); r->pair_balanced=1;'),
    'retry-prepare-failure': (
        'if(result.outcome!=BPN_CALL_OK){rc=retain(m,j,result.native_error);goto done;}',
        'if(result.outcome!=BPN_CALL_OK){(void)call(m,j,i,BPN_PREPARE,0,NULL,0,&result);rc=retain(m,j,result.native_error);goto done;}'),
    'retry-complete-failure': (
        'if(result.outcome!=BPN_CALL_OK)return retain(m,j,result.native_error);',
        'if(result.outcome!=BPN_CALL_OK){(void)call(m,j,i,BPN_COMPLETE,0,NULL,0,&result);return retain(m,j,result.native_error);}'),
    'zero-before-policy-bind': ('struct bpl_span spans[3];rc=views_valid(m,j,spans);',
        'struct bpn_return premature; (void)call(m,j,0,BPN_ZERO,0,NULL,BPL_PAGE,&premature); struct bpl_span spans[3];rc=views_valid(m,j,spans);'),
    'ignore-changed-view': ('memcmp(&result.view,&j->records[i].view,sizeof result.view)', '0'),
    'ignore-protected-pa': ('if(overlap(v.pa,BPL_PAGE,p.base,p.bytes))', 'if(overlap(v.pa,BPL_PAGE,p.base,p.bytes) && 0)'),
    'ignore-protected-cpu': ('if(overlap(v.cpu,BPL_PAGE,p.base,p.bytes))', 'if(overlap(v.cpu,BPL_PAGE,p.base,p.bytes) && 0)'),
    'reentrant-retire': ('if(m->busy || m->halted)return BPN_BUSY;', 'if(m->halted)return BPN_BUSY;'),
    'ignore-stale-result': ('result->sequence!=c.sequence ||', '0 ||'),
    'forget-pending-clear': ('j->call_pending=0;', 'j->call_pending=1;'),
    'stale-token': ('j->generation!=t->generation ||', '0 ||'),
    'uncertainty-domain-reuse': ('m->halted=1;\n    if (!j->core_retired)', 'm->halted=0;\n    if (!j->core_retired)'),
    'missing-protection': ('protection->complete!=1 ||', '0 ||'),
    'duplicate-descriptor': ('if(!identities_unique(m,j)){rc=retain(m,j,BPN_VALIDATION);goto done;}', 'if(0){rc=retain(m,j,BPN_VALIDATION);goto done;}'),
}
NATIVE_MUTATIONS = {
    'wrong-physical-mask': ('BPL_PAGE,BPN_MASK);', 'BPL_PAGE,UINT64_C(0xfffffffff));'),
    'implicit-mapper': ('kIODirectionOutIn | kIOMemoryMapperNone | kIOMemoryHostPhysicallyContiguous,', 'kIODirectionOutIn | kIOMemoryHostPhysicallyContiguous,'),
    'implicit-contiguity': ('kIODirectionOutIn | kIOMemoryMapperNone | kIOMemoryHostPhysicallyContiguous,', 'kIODirectionOutIn | kIOMemoryMapperNone,'),
    'unanchored-factory-result': ('ref=IOBufferMemoryDescriptor::inTaskWithPhysicalMask', 'auto transient=IOBufferMemoryDescriptor::inTaskWithPhysicalMask'),
    'page-size-assumption': ('creation_attempted=true;\n    if(PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return BPN_PAGE;', 'creation_attempted=true;'),
}


class BackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which('clang')
        cls.cxx = shutil.which('clang++')
        if not cls.cc or not cls.cxx:
            raise RuntimeError('clang and clang++ required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='bpn-tests-')
        cls.out = Path(cls.tmp.name)
        cls.flags = ['-Wall', '-Wextra', '-Werror', '-Wpedantic', '-O1', '-g', '-pthread',
                     '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I'+str(HERE)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_tool(self, args):
        return subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=60,
                              env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))

    def compile(self, args):
        p = self.run_tool(args)
        self.assertEqual(p.returncode, 0, p.stdout+p.stderr)

    def core(self, name, source=None, extra=()):
        out = self.out/name
        self.compile([self.cc, '-std=c11', *self.flags, *extra, FROZEN/'lifecycle.c',
                      source or HERE/'backing.c', HERE/'test_backing.c', '-o', out])
        return out

    def adapter(self, name, source=None):
        objects = []
        for key, path in [('lifecycle', FROZEN/'lifecycle.c'), ('backing', HERE/'backing.c')]:
            obj = self.out/(name+'-'+key+'.o')
            self.compile([self.cc, '-std=c11', *self.flags, '-c', path, '-o', obj])
            objects.append(obj)
        out = self.out/name
        self.compile([self.cxx, '-std=c++17', '-fno-exceptions', '-fno-rtti', *self.flags,
                      '-I'+str(HERE/'test_sdk'), source or HERE/'native_iokit.cpp',
                      HERE/'test_native.cpp', *objects, '-o', out])
        return out

    def test_core_host_and_kernel(self):
        for name, flags in [('host', []), ('host-kernel', ['-DKERNEL'])]:
            p = self.run_tool([self.core(name, extra=flags)])
            self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
            self.assertIn('PRIVATE BACKING HOST PASS', p.stdout)
            print(name+': '+p.stdout.strip())

    def test_actual_adapter_with_test_sdk(self):
        binary = self.adapter('adapter')
        cases = [(0, 0, 0), (99, 0, 0)]+[(1, role, 0) for role in range(3)]
        cases += [(action, role, after) for action in (2, 6) for role in range(3) for after in range(2)]
        for case in cases:
            with self.subTest(case=case):
                p = self.run_tool([binary, *case])
                self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
                self.assertIn('ACTUAL ADAPTER WITH TEST SDK PASS', p.stdout)
        print('actual native adapter under TEST SDK scenarios='+str(len(cases)))

    def test_compiled_manager_mutants(self):
        original = (HERE/'backing.c').read_text()
        for name, (before, after) in MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1, name)
                source = self.out/(name+'.c');source.write_text(original.replace(before, after))
                p = self.run_tool([self.core(name, source)])
                self.assertNotEqual(p.returncode, 0, 'survived '+name)
                self.assertIn('Assertion', p.stderr, p.stdout+p.stderr)
        print('compiled manager mutants rejected='+str(len(MUTATIONS)))

    def test_compiled_native_adapter_mutants(self):
        original = (HERE/'native_iokit.cpp').read_text()
        for name, (before, after) in NATIVE_MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1, name)
                mutated = original.replace(before, after)
                if name=='unanchored-factory-result':
                    mutated = mutated.replace('BPL_PAGE,BPN_MASK);', 'BPL_PAGE,BPN_MASK); (void)transient;')
                source = self.out/(name+'.cpp');source.write_text(mutated)
                p = self.run_tool([self.adapter(name, source), 99 if name=='page-size-assumption' else 0, 0, 0])
                self.assertNotEqual(p.returncode, 0, 'survived '+name)
                self.assertIn('Assertion', p.stderr, p.stdout+p.stderr)
        print('compiled native adapter mutants rejected='+str(len(NATIVE_MUTATIONS)))

    def test_ndebug_refusal(self):
        for source, compiler, dialect in [('test_backing.c', self.cc, '-std=c11'), ('test_native.cpp', self.cxx, '-std=c++17')]:
            p = self.run_tool([compiler, dialect, *self.flags, '-DNDEBUG', '-c', HERE/source, '-o', self.out/'refused.o'])
            self.assertNotEqual(p.returncode, 0)
            self.assertIn('tests require assertions', p.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
