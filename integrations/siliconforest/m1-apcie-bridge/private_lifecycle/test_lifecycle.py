#!/usr/bin/env python3
"""Original host-only tests; compile mutations of private lifecycle core."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
MUTATIONS = {
    'duplicate-rid': ('e->request.rid==q->rid ||', '0 ||'),
    'duplicate-sid': ('e->request.sid==q->sid ||', '0 ||'),
    'duplicate-vector': ('(e->request.vectors&q->vectors))', '(0))'),
    'stale-generation': ('e->generation!=l->generation', '0'),
    'foreign-registry': ('l->registry!=r ||', '0 ||'),
    'exhaustion': ('if (r->exhausted || !r->next_generation)', 'if (0)'),
    'repeat-init': ('if (bytes[i]) return BPL_STATE;', 'if (bytes[i] && 0) return BPL_STATE;'),
    'cpu-alias': ('overlap(a->cpu,a->bytes,b->cpu,b->bytes)', '0'),
    'pa-alias': ('overlap(a->pa,a->bytes,b->pa,b->bytes)', '0'),
    'iova-alias': ('overlap(a->iova,a->bytes,b->iova,b->bytes)', '0'),
    'handle-alias': ('a->handle==b->handle', '0'),
    'permission-widening': ('(s->permissions&~(BPL_READ|BPL_WRITE))', '(0)'),
    'reserved-overlap': ('if (overlap(s->pa,s->bytes,p.base,p.bytes))', 'if (overlap(s->pa,s->bytes,p.base,p.bytes) && 0)'),
    'pending-not-recorded': ('e->pending=seq;', 'e->pending=0;'),
    'history-overwrite': ('e->ever_resources|=resources;', 'e->ever_resources=resources;'),
    'unknown-not-quarantined': ('quarantine(e,error); return BPL_RETAINED;', 'return BPL_RETAINED;'),
    'wrong-result-not-quarantined': ('quarantine(e,BPL_STALE); return BPL_STALE;', 'return BPL_STALE;'),
    'first-error-overwritten': ('if (!e->first_error) e->first_error=error;', 'e->first_error=error;'),
    'exposed-retirement': ('if ((found->state!=BPL_CLAIMED && found->state!=BPL_PRIVATE_METADATA) ||\n        found->effect_count || found->pending || found->ever_resources)', 'if (0)'),
    'quarantine-claim-release': ('state<=BPL_QUARANTINED', 'state<BPL_QUARANTINED'),
    'resource-mask-underreport': ('if ((resources&required[kind])!=required[kind])', 'if ((resources&required[kind])!=required[kind] && 0)'),
    'unbound-data-effect': ('if ((resources&7u) && found->span_count!=BPL_SPANS)', 'if (0)'),
}


class LifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which('clang')
        if not cls.cc:
            raise RuntimeError('clang required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='bpl-tests-')
        cls.out = Path(cls.tmp.name)
        cls.flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                     '-O1', '-g', '-fsanitize=address,undefined',
                     '-fno-sanitize-recover=all', '-I'+str(HERE)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_tool(self, args):
        return subprocess.run([str(a) for a in args], capture_output=True, text=True,
                              timeout=60, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                                                   UBSAN_OPTIONS='halt_on_error=1'))

    def build(self, name, source, extra=()):
        out = self.out/name
        p = self.run_tool([self.cc, *self.flags, *extra, source,
                           HERE/'test_lifecycle.c', '-o', out])
        self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
        return out

    def test_host_sanitizers_and_kernel_metadata_build(self):
        for suffix, flags in [('host', []), ('kernel-metadata', ['-DKERNEL'])]:
            p = self.run_tool([self.build(suffix, HERE/'lifecycle.c', flags)])
            self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
            self.assertIn('PRIVATE METADATA PASS', p.stdout)
            print(suffix+': '+p.stdout.strip())

    def test_compiled_mutants(self):
        original = (HERE/'lifecycle.c').read_text()
        for name, (before, after) in MUTATIONS.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1, name)
                source = self.out/(name+'.c')
                source.write_text(original.replace(before, after))
                p = self.run_tool([self.build(name, source)])
                self.assertNotEqual(p.returncode, 0, 'survived: '+name)
                self.assertIn('Assertion', p.stderr, p.stdout+p.stderr)
        print('compiled core mutants rejected='+str(len(MUTATIONS)))

    def test_assertion_guard(self):
        p = self.run_tool([self.cc, *self.flags, '-DNDEBUG', '-c',
                           HERE/'test_lifecycle.c', '-o', self.out/'refused.o'])
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('lifecycle tests require assertions', p.stderr)

    def test_no_drain_or_activation_api(self):
        # Compile actual attempted unsupported API use; not a lexical pass.
        for api in ['bpl_activate', 'bpl_ack_drain', 'bpl_unmap', 'bpl_force_release']:
            source = self.out/(api+'.c')
            source.write_text('#include "lifecycle.h"\nint probe(void) { return '+api+'(0,1); }\n')
            p = self.run_tool([self.cc, *self.flags, '-c', source, '-o', self.out/(api+'.o')])
            self.assertNotEqual(p.returncode, 0)
            self.assertIn(api, p.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
