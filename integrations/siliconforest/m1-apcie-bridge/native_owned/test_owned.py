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
        'j->phase=BNO_RETIRING;',
        'j->phase=BNO_RETIRING; (void)bpl_retire_private(&m->registry,&j->lease);'),
    'partial-cohort-reset': ('r->pair_balanced=1;',
        'r->pair_balanced=1; (void)call(m,j,i,BNO_RESET_REF,0,NULL,0,&result);'),
    'extra-constructor-complete': ('r->pair_balanced=1;',
        '(void)call(m,j,i,BNO_COMPLETE,0,NULL,0,&result); r->pair_balanced=1;'),
    'retry-prepare-failure': (
        'if(result.outcome!=BNO_CALL_OK){rc=retain(m,j,result.native_error);goto done;}',
        'if(result.outcome!=BNO_CALL_OK){(void)call(m,j,i,BNO_PREPARE,0,NULL,0,&result);rc=retain(m,j,result.native_error);goto done;}'),
    'retry-complete-failure': (
        'if(result.outcome!=BNO_CALL_OK)return retain(m,j,result.native_error);',
        'if(result.outcome!=BNO_CALL_OK){(void)call(m,j,i,BNO_COMPLETE,0,NULL,0,&result);return retain(m,j,result.native_error);}'),
    'zero-before-policy-bind': ('struct bpl_span spans[3];rc=views_valid(m,j,spans);',
        'struct bno_return premature; (void)call(m,j,0,BNO_ZERO,0,NULL,BPL_PAGE,&premature); struct bpl_span spans[3];rc=views_valid(m,j,spans);'),
    'ignore-changed-view': ('memcmp(&result.view,&j->records[i].view,sizeof result.view)', '0'),
    'ignore-protected-pa': ('if(overlap(v.pa,BPL_PAGE,p.base,p.bytes))', 'if(overlap(v.pa,BPL_PAGE,p.base,p.bytes) && 0)'),
    'ignore-protected-cpu': ('if(overlap(v.cpu,BPL_PAGE,p.base,p.bytes))', 'if(overlap(v.cpu,BPL_PAGE,p.base,p.bytes) && 0)'),
    'reentrant-retire': ('if(m->busy || m->halted)return BNO_BUSY;', 'if(m->halted)return BNO_BUSY;'),
    'ignore-stale-result': ('result->sequence!=c.sequence ||', '0 ||'),
    'forget-pending-clear': ('j->call_pending=0;', 'j->call_pending=1;'),
    'stale-token': ('j->generation!=t->generation ||', '0 ||'),
    'uncertainty-domain-reuse': ('m->halted=1;\n    if (!j->core_retired)', 'm->halted=0;\n    if (!j->core_retired)'),
    'missing-protection': ('protection->complete!=1 ||', '0 ||'),
    'duplicate-descriptor': ('if(!identities_unique(m,j)){rc=retain(m,j,BNO_VALIDATION);goto done;}', 'if(0){rc=retain(m,j,BNO_VALIDATION);goto done;}'),
}
NATIVE_MUTATIONS = {
    'wrong-physical-mask': ('BPL_PAGE,BNO_MASK);', 'BPL_PAGE,UINT64_C(0xfffffffff));'),
    'implicit-mapper': ('kIODirectionOutIn | kIOMemoryMapperNone | kIOMemoryHostPhysicallyContiguous,', 'kIODirectionOutIn | kIOMemoryHostPhysicallyContiguous,'),
    'implicit-contiguity': ('kIODirectionOutIn | kIOMemoryMapperNone | kIOMemoryHostPhysicallyContiguous,', 'kIODirectionOutIn | kIOMemoryMapperNone,'),
    'unanchored-factory-result': ('ref=IOBufferMemoryDescriptor::inTaskWithPhysicalMask', 'auto transient=IOBufferMemoryDescriptor::inTaskWithPhysicalMask'),
    'page-size-assumption': ('creation_attempted=true;\n    if(PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return BNO_PAGE;', 'creation_attempted=true;'),
}

EFFECT_MUTATIONS = {
 'skip-journal': ('rc=bpl_effect_begin(&m->registry,&j->lease,kind,resources,&call_scope.ticket);', 'rc=BPL_OK;'),
 'wrong-lease-ticket': ('j->phase=BNO_EFFECT_PENDING;j->call_pending=1;', 'call_scope.ticket.lease.generation++;j->phase=BNO_EFFECT_PENDING;j->call_pending=1;'),
 'omit-resource-union': ('kind,resources,&call_scope.ticket);', 'kind,8,&call_scope.ticket);'),
 'uncaptured-request': ('call_scope.resources=resources;call_scope.request=q;', 'call_scope.resources=resources;call_scope.request=*request;'),
 'ignored-return-identity': ('result.identities[i]!=call_scope.identities[i] ||', '0 ||'),
 'ignored-ticket-result': ('result.ticket_sequence!=call_scope.ticket.sequence ||', '0 ||'),
 'ignored-effect-sequence': ('result.sequence!=call_scope.sequence ||', '0 ||'),
 'ignored-effect-generation': ('result.generation!=call_scope.generation ||', '0 ||'),
 'ignored-effect-operation': ('result.operation!=call_scope.operation ||', '0 ||'),
 'ignored-effect-slot': ('result.slot!=call_scope.slot ||', '0 ||'),
 'reported-failure-success': ('rc=bpl_effect_result(&m->registry,&call_scope.ticket,result.outcome,result.error);', 'result.outcome=BPL_REPORTED_SUCCESS;result.error=0;rc=bpl_effect_result(&m->registry,&call_scope.ticket,result.outcome,result.error);'),
 'effect-history-erasure': ('j->call_pending = 0;j->phase=BNO_EFFECT_RETAINED;', 'j->call_pending = 0;j->phase=BNO_EFFECT_RETAINED;m->registry.entries[j->lease.slot].effect_count=0;'),
 'effect-private-reentry': ('j->call_pending = 0;j->phase=BNO_EFFECT_RETAINED;', 'j->call_pending = 0;j->phase=BNO_INITIALIZED;'),
 'effect-pending-left-live': ('j->call_pending = 0;j->phase=BNO_EFFECT_RETAINED;', 'j->call_pending = 1;j->phase=BNO_EFFECT_RETAINED;'),
 'sequence-reservation-wrap': ('m->next_sequence>UINT64_MAX-4', 'm->next_sequence==UINT64_MAX'),
 'late-budget-refusal': ('snapshot.effect_count>=BPL_EFFECTS ||', '0 ||'),
 'data-permission-widen': ('!(permissions&BPL_WRITE)', '(!(permissions&BPL_WRITE) && 0)'),
 'data-bound-overflow': ('q->length>BPL_PAGE-q->offset ||', '0 ||'),
 'output-owned-alias': ('!external_span(m,out,sizeof *out) ||', '0 ||'),
}
UNAVAILABLE_MUTATIONS = {
 'unavailable-success': ('if(!m->ops.effect_backend_available){unlock(m);return BNO_UNAVAILABLE;}', 'if(!m->ops.effect_backend_available){unlock(m);return BNO_OK;}'),
 'unavailable-query': ('if(!m->ops.effect_backend_available){unlock(m);return BNO_UNAVAILABLE;}', 'if(!m->ops.effect_backend_available){m->busy=1;(void)recheck(m,j);m->busy=0;unlock(m);return BNO_UNAVAILABLE;}'),
}


class BackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which('clang')
        cls.cxx = shutil.which('clang++')
        if not cls.cc or not cls.cxx:
            raise RuntimeError('clang and clang++ required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='bno-tests-')
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
                      source or HERE/'owned.c', HERE/'test_owned.c', '-o', out])
        return out

    def adapter(self, name, source=None, extra=(), manager=None):
        objects = []
        for key, path in [('lifecycle', FROZEN/'lifecycle.c'), ('backing', manager or HERE/'owned.c')]:
            obj = self.out/(name+'-'+key+'.o')
            self.compile([self.cc, '-std=c11', *self.flags, '-c', path, '-o', obj])
            objects.append(obj)
        out = self.out/name
        self.compile([self.cxx, '-std=c++17', '-fno-exceptions', '-fno-rtti', *self.flags,
                      *extra, '-I'+str(HERE/'test_sdk'), source or HERE/'native_owned.cpp',
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
        original = (HERE/'owned.c').read_text(); killed = 0
        for name, (before, after) in MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1, name)
                source = self.out/(name+'.c');source.write_text(original.replace(before, after))
                p = self.run_tool([self.core(name, source)])
                self.assertNotEqual(p.returncode, 0, 'survived '+name)
                self.assertIn('Assertion', p.stderr, p.stdout+p.stderr)
                killed += 1
        print('compiled manager mutants rejected='+str(killed))

    def test_compiled_native_adapter_mutants(self):
        original = (HERE/'native_owned.cpp').read_text(); killed = 0
        for name, (before, after) in NATIVE_MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1, name)
                mutated = original.replace(before, after)
                if name=='unanchored-factory-result':
                    mutated = mutated.replace('BPL_PAGE,BNO_MASK);', 'BPL_PAGE,BNO_MASK); (void)transient;')
                source = self.out/(name+'.cpp');source.write_text(mutated)
                p = self.run_tool([self.adapter(name, source), 99 if name=='page-size-assumption' else 0, 0, 0])
                self.assertNotEqual(p.returncode, 0, 'survived '+name)
                self.assertIn('Assertion', p.stderr, p.stdout+p.stderr)
                killed += 1
        print('compiled native adapter mutants rejected='+str(killed))

    def effects(self, name, source=None, extra=()):
        out = self.out/name
        self.compile([self.cc, '-std=c11', *self.flags, *extra, FROZEN/'lifecycle.c',
                      source or HERE/'owned.c', HERE/'test_effects.c', '-o', out])
        return out

    def test_effect_core(self):
        for name, flags in [('effects-host', []), ('effects-kernel', ['-DKERNEL'])]:
            p = self.run_tool([self.effects(name, extra=flags)])
            self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
            self.assertIn('OWNED EFFECT HOST PASS', p.stdout)
            print(p.stdout.strip())

    def test_native_effect_dispatch_host_only(self):
        binary = self.adapter('effect-adapter', extra=['-DBNO_TEST_DISPATCH'])
        for case in [(0,0,0)]+[(8,0,fault) for fault in range(1,7)]:
            with self.subTest(case=case):
                p = self.run_tool([binary, *case])
                self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
                self.assertIn('ACTUAL ADAPTER WITH TEST SDK PASS', p.stdout)
        print('actual adapter HOST ONLY effect dispatcher scenarios=7')

    def test_effect_mutants(self):
        original = (HERE/'owned.c').read_text(); killed = 0
        for name, (before, after) in EFFECT_MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1, name)
                source = self.out/(name+'.c'); source.write_text(original.replace(before, after))
                p = self.run_tool([self.effects(name, source)])
                self.assertNotEqual(p.returncode, 0, 'survived '+name)
                self.assertIn('Assertion', p.stderr, p.stdout+p.stderr)
                killed += 1
        print('compiled owned-effect mutants rejected='+str(killed))

    def test_unavailable_mutants(self):
        original = (HERE/'owned.c').read_text(); killed = 0
        for name, (before, after) in UNAVAILABLE_MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1, name)
                source = self.out/(name+'.c'); source.write_text(original.replace(before, after))
                p = self.run_tool([self.adapter(name, manager=source), 0, 0, 0])
                self.assertNotEqual(p.returncode, 0, 'survived '+name)
                self.assertIn('Assertion', p.stderr, p.stdout+p.stderr)
                killed += 1
        print('compiled unavailable-boundary mutants rejected='+str(killed))

    def test_test_dispatch_kernel_refused(self):
        p = self.run_tool([self.cxx, '-std=c++17', *self.flags, '-DBNO_TEST_DISPATCH', '-DKERNEL',
                          '-I'+str(HERE/'test_sdk'), '-c', HERE/'native_owned.cpp', '-o', self.out/'refused-dispatch.o'])
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('TEST dispatcher cannot be compiled with KERNEL', p.stderr)

    def test_ndebug_refusal(self):
        for source, compiler, dialect in [('test_owned.c', self.cc, '-std=c11'), ('test_effects.c', self.cc, '-std=c11'), ('test_native.cpp', self.cxx, '-std=c++17')]:
            p = self.run_tool([compiler, dialect, *self.flags, '-DNDEBUG', '-c', HERE/source, '-o', self.out/'refused.o'])
            self.assertNotEqual(p.returncode, 0)
            self.assertIn('tests require assertions', p.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
