#!/usr/bin/env python3
"""Host sanitizer and compiled semantic mutations for original pure metadata."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class SnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which('clang')
        if not cls.cc:
            raise RuntimeError('clang required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='mcc-snapshot-')
        cls.work = Path(cls.tmp.name)
        cls.flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-O1', '-g',
                     '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I'+str(HERE)]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def call(self, args, success=True):
        r = subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1', UBSAN_OPTIONS='halt_on_error=1'))
        if success:
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        return r

    def build(self, name, source, flags=()):
        out = self.work/name
        self.call([self.cc, *self.flags, *flags, source, HERE/'test_mcc.c', '-o', out])
        return out

    def test_sanitized_modes(self):
        for mode, flags in [('host', []), ('kernel-defined-host', ['-DKERNEL'])]:
            with self.subTest(mode=mode):
                r = self.call([self.build(mode, HERE/'mcc_snapshot.c', flags)])
                self.assertIn('MCC metadata checks=', r.stdout)
                print(mode+': '+r.stdout.strip())

    def test_ndebug_refuses(self):
        r = self.call([self.cc, '-DNDEBUG', '-I'+str(HERE), '-c', HERE/'test_mcc.c',
                       '-o', self.work/'ndebug.o'], success=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('require assertions', r.stderr)

    def test_semantic_mutations(self):
        source = (HERE/'mcc_snapshot.c').read_text()
        mutations = {
            'plane-stride': ('plane*UINT64_C(0x40000)', 'plane*UINT64_C(0x20000)'),
            'slot-stride': ('slot*0x10+field*4', 'slot*0x14+field*4'),
            'incomplete-plan': ('region_bytes<UINT64_C(0xc06dc) || ', ''),
            'read-budget': ('index>=MCP_READS', 'index>MCP_READS'),
            'unstable': ('if(raw[i]!=raw[i+MCP_BYTES/2])', 'if(0 && raw[i]!=raw[i+MCP_BYTES/2])'),
            'different-planes': ('if(word(raw+at(0,0,slot,field))', 'if(0 && word(raw+at(0,0,slot,field))'),
            'enable-bit-zero-only': ('s->enabled=s->enable_word!=0;', 's->enabled=s->enable_word==1;'),
            'zero-start': ('if(!s->start_word || s->end_word<=s->start_word)', 'if(s->end_word<=s->start_word)'),
            'warning-skip': ('if(!s->start_word || s->end_word<=s->start_word)return MCP_ENABLED_INVALID;',
                             'if(!s->start_word || s->end_word<=s->start_word)continue;'),
            'cpu-page-shift': ('low=(uint64_t)s->start_word<<12', 'low=(uint64_t)s->start_word<<14'),
            'shift-before-widen': ('low=(uint64_t)s->start_word<<12', 'low=(uint64_t)(s->start_word<<12)'),
            'exclusive-end': ('high=((uint64_t)s->end_word+1)<<12', 'high=((uint64_t)s->end_word)<<12'),
            'addition-not-or': ('low|=MCP_DRAM_BASE;high|=MCP_DRAM_BASE;', 'low+=MCP_DRAM_BASE;high+=MCP_DRAM_BASE;'),
            'outside-dram': ('high>dram->base+dram->bytes', '0'),
            'bridge-hole': ('next.base<=r->excluded[n-1].base+r->excluded[n-1].bytes)',
                            'next.base<=r->excluded[n-1].base+r->excluded[n-1].bytes+0x1000)'),
            'omit-last-slot': ('for(unsigned i=0;i<MCP_SLOTS;i++) {', 'for(unsigned i=0;i<MCP_SLOTS-1;i++) {'),
            'early-output': ('if(!s->start_word ||', '*out=staged; if(!s->start_word ||'),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(mutation=name):
                self.assertEqual(source.count(before), 1, name)
                p = self.work/(name+'.c');p.write_text(source.replace(before, after))
                out = self.build(name, p)  # compilation must succeed, not count as a kill
                r = self.call([out], success=False)
                self.assertNotEqual(r.returncode, 0, 'survived: '+name)
                self.assertIn('Assertion', r.stderr)
                print('MUTANT '+name+': compiled, assertion killed', flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
