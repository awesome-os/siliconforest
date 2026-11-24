#!/usr/bin/env python3
"""Host-only corpus/decoder/mutation tests; no IOKit or target execution."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
NONCE = '0123456789abcdef' * 3
spec = importlib.util.spec_from_file_location('apcie_decode', HERE / 'decode.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)


def compile_test(output, collector=None, sanitize=False):
    args = ['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-g', '-O1']
    if sanitize:
        args += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    args += ['-I' + str(HERE), '-I' + str(REPO / 'integrations/puredarwin/m1-apcie-bridge'),
             '-I' + str(REPO / 'integrations/puredarwin/m1-dart-survey'),
             str(HERE / 'test_collector.c'), str(collector or HERE / 'collector.c'),
             str(REPO / 'integrations/puredarwin/m1-apcie-bridge/observe.c'), '-lcrypto', '-o', str(output)]
    subprocess.run(args, check=True, capture_output=True)


class CollectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='apcie-collector-')
        cls.root = Path(cls.temp.name)
        cls.corpus = cls.root / 'corpus'
        cls.corpus.mkdir()
        compile_test(cls.root / 'test', sanitize=True)
        result = subprocess.run([str(cls.root / 'test'), str(cls.corpus)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.c_report = result.stderr.strip()
        cls.records = [(p.read_bytes(), p.with_suffix('.raw').read_bytes(),
                        int(p.with_suffix('.rc').read_text())) for p in sorted(cls.corpus.glob('*.txt'))]
        cls.valid = next(output for output, raw, rc in cls.records if rc == 0)
        cls.raw = decoder.decode(cls.valid, NONCE)['raw_hex']

    @classmethod
    def tearDownClass(cls):
        if os.environ.get('APC_TEST_REPORT'):
            Path(os.environ['APC_TEST_REPORT']).write_text(json.dumps({
                'scope': 'HOST_SYNTHETIC_ONLY_NO_DARWIN_EXECUTION',
                'c_source_oracle': cls.c_report, 'collector_records': len(cls.records),
                'corpus_sha256': hashlib.sha256(b''.join(x[0] for x in cls.records)).hexdigest(),
                'compiler': subprocess.check_output(['clang', '--version'], text=True).splitlines()[0],
                'ASan_UBSan': True,
            }, indent=2) + '\n')
        cls.temp.cleanup()

    def test_actual_source_corpus(self):
        seen = set()
        for output, raw, rc in self.records:
            with self.subTest(raw=len(raw), rc=rc):
                got = decoder.decode(output, NONCE)
                self.assertEqual(got['expected_exit'], rc)
                if got['raw_hex'] is not None:
                    self.assertEqual(bytes.fromhex(got['raw_hex']), raw)
                    self.assertEqual(got['raw_sha256'], hashlib.sha256(raw).hexdigest())
                if got.get('summary'):
                    seen.add(got['summary']['result'])
        self.assertEqual(seen, {-3, -2, -1, 0, 1, 2, 3, 4, 5})

    def test_full_observation_is_not_linkup(self):
        got = decoder.decode(self.valid, NONCE)
        self.assertEqual(struct.unpack('<21I', bytes.fromhex(got['raw_hex']))[20], 0)
        self.assertEqual(got['verdict'], 'OBSERVED')
        self.assertFalse(got['summary']['initialization_proven'])

    def test_trailing_CR_only(self):
        for n in (1, 2, 3):
            self.assertEqual(decoder.decode(self.valid.replace(b'\n', b'\r' * n + b'\n'), NONCE)['raw_hex'], self.raw)
        with self.assertRaises(ValueError):
            decoder.decode(self.valid.replace(b'index=0 ', b'index=0\r ', 1), NONCE)

    def test_every_metadata_bit(self):
        for output, raw, rc in self.records:
            if len(raw) != 84:
                continue
            try:
                original = decoder.classify(raw)
            except ValueError:
                continue
            for field in range(7):
                for bit in range(32):
                    new = bytearray(raw); new[field * 4 + bit // 8] ^= 1 << (bit % 8)
                    value = struct.unpack('<i', new[16:20])[0]
                    if field == 4 and original['reads'] == 0 and value in (-1, -2, -3):
                        self.assertEqual(decoder.classify(bytes(new))['result'], value)
                    else:
                        with self.assertRaises(ValueError):
                            decoder.classify(bytes(new))

    def test_no_verdict_promotion(self):
        for output, raw, rc in self.records:
            if rc != 0:
                promoted = re.sub(rb'verdict=[A-Z_]+', b'verdict=OBSERVED', output)
                with self.assertRaises(ValueError):
                    decoder.decode(promoted, NONCE)

    def test_length_sha_index_nonce_noise(self):
        mutations = [self.valid[:-1], self.valid + b'noise\n', b'noise\n' + self.valid,
                     self.valid.replace(b'bytes=84', b'bytes=83', 1),
                     self.valid.replace(b'chunk_bytes=64', b'chunk_bytes=32'),
                     self.valid.replace(b'index=0 ', b'index=1 ', 1),
                     self.valid.replace(b'index=1 ', b'index=0 ', 1),
                     self.valid.replace(NONCE.encode(), b'f' * 48, 1),
                     self.valid.replace(b'index=0 ', b'index=00 ', 1),
                     self.valid.replace(b'sha256=', b'sha256=0', 1),
                     self.valid.replace(b'initialization_proven=false', b'initialization_proven=true'),
                     self.valid.replace(b'reads=14', b'reads=13'), self.valid.replace(b'result=0', b'result=-0'),
                     self.valid.replace(b'stage=5', b'stage=6'), self.valid + self.valid,
                     self.valid.replace(b'PDApcieBridgeObservationV1', b'PDApcieBridgeObservationV2', 1),
                     b'X' * (decoder.OUTPUT_CAP + 1), b'\xff\n', b'\n', b'']
        for output in mutations:
            with self.subTest(output=output[:80]):
                with self.assertRaises(ValueError):
                    decoder.decode(output, NONCE)
        lines = self.valid.splitlines(keepends=True)
        for i in range(len(lines)):
            with self.assertRaises(ValueError):
                decoder.decode(b''.join(lines[:i] + lines[i + 1:]), NONCE)
        for nonce in ('', 'A' * 48, 'f' * 47, 'f' * 49, None):
            with self.assertRaises(ValueError):
                decoder.decode(self.valid, nonce)

    def test_integer_and_copy_bounds(self):
        for number in ('-0', '+1', '01', '2147483648', '-2147483649', '1' * 100):
            with self.assertRaises(ValueError):
                decoder._sint(number)
        for number in ('-1', '00', '4097', '1' * 100):
            with self.assertRaises(ValueError):
                decoder._uint(number, 4096)
        missing = next(out for out, raw, rc in self.records if b'verdict=MISSING ' in out)
        with self.assertRaises(ValueError):
            decoder.decode(missing.replace(b'detail=-123', b'detail=2147483648'), NONCE)
        with self.assertRaises(ValueError):
            decoder.decode(missing.replace(b'cleanup=-7', b'cleanup=-0'), NONCE)

    def test_all_short_and_long_classify(self):
        raw = bytes.fromhex(self.raw)
        for n in range(84):
            with self.assertRaises(ValueError):
                decoder.classify(raw[:n])
        with self.assertRaises(ValueError):
            decoder.classify(raw + b'\0')

    def test_source_counter_mutants_compile_and_execute(self):
        source = (HERE / 'collector.c').read_text()
        mutations = [
            ('version', 'word(b, 0) != 1', 'word(b, 0) != 2'),
            ('power-flag', '0x90001c00', '0x10001c00'),
            ('power-low', '(v & 255u) == 255u', '(v & 254u) == 254u'),
            ('read-count', 'word(b, 1) != r->reads', '0'),
            ('stage', 'word(b, 2) != r->stage', '0'),
            ('valid', 'word(b, 3) != r->mask', '0'),
            ('result', 'word(b, 4) != (uint32_t)result', '0'),
            ('region', 'word(b, 5) != r->region', '0'),
            ('offset', 'word(b, 6) != r->offset', '0'),
            ('unread-zero', 'if (!(r->mask & (1u << i)) && word(b, word_for_bit[i]))', 'if (!(r->mask & (1u << i)) && word(b, word_for_bit[i]) && 0)'),
            ('PHY-reset', '(word(b, 15) & 0x8fu) != 15u', '(word(b, 15) & 0x0fu) != 15u'),
            ('final-power', 'return finish(&r, 5, out);', 'return finish(&r, result, out);'),
            ('CLEANUP', 'if (cleanup)', 'if (0)'),
            ('read-failure', 'if (!(word(r->raw, 3) & (1u << bit)))', 'if (0)'),
            ('CLI', 'strcmp(argv[1], "--nonce")', '0'),
        ]
        for name, old, new in mutations:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, (name, old))
                file = self.root / (name + '.c'); file.write_text(source.replace(old, new, 1))
                binary = self.root / ('mutant-' + name)
                compile_test(binary, file)
                result = subprocess.run([str(binary), str(self.corpus)], capture_output=True)
                self.assertNotEqual(result.returncode, 0, name)


if __name__ == '__main__':
    unittest.main(verbosity=2)
