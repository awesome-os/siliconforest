#!/usr/bin/env python3
"""Host-only evidence exporter tests. No Darwin, target or display access."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from decode_records import decode

HERE = Path(__file__).resolve().parent
NONCE = '0123456789abcdef' * 3
FILES = {'pty-rx.bin': b'\x00\r\nDarwin\narm64\n\xff', 'pty-tx.bin': b"echo 'x'\r",
         'child.json': b'{"pid":42}\n', 'child-ready.json': b'{"exec":true}\n'}


class RecordsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='terminal-records-host-')
        cls.binary = Path(cls.build.name) / 'records'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-O1', str(HERE / 'terminal-records.c'), '-lcrypto', '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='terminal-records-fixture-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name) / 'session'
        self.directory.mkdir(mode=0o700)
        for name, data in FILES.items():
            p = self.directory / name
            p.write_bytes(data)
            p.chmod(0o600)

    def run_export(self, phase='live', directory=None):
        return subprocess.run([str(self.binary), '--directory', str(directory or self.directory),
                               '--nonce', NONCE, '--phase', phase], capture_output=True, timeout=2)

    def test_live_exports_exact_bytes_in_hex_and_allows_only_optional_missing(self):
        r = self.run_export()
        self.assertEqual(r.returncode, 0, r.stdout)
        decoded = decode(r.stdout, NONCE)
        for name, data in FILES.items():
            self.assertEqual(decoded[name]['data'], data)
            self.assertEqual(decoded[name]['file_bytes_after'], len(data))
        self.assertEqual(r.stdout.count(b'DL11_RECORD_MISSING'), 2)

    def test_closed_requires_final_grid_and_result(self):
        self.assertEqual(self.run_export('closed').returncode, 1)
        for name in ('grid-final.txt', 'result.json'):
            p = self.directory / name
            p.write_text('actual fixture\n')
            p.chmod(0o600)
        r = self.run_export('closed')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertNotIn(b'DL11_RECORD_MISSING', r.stdout)
        self.assertEqual(len(decode(r.stdout, NONCE, 'closed')), 6)

    def test_digest_chunk_count_order_and_complete_export_are_required(self):
        p = self.directory / 'pty-rx.bin'
        p.write_bytes(bytes(range(256)))
        original = self.run_export().stdout
        self.assertEqual(decode(original, NONCE)['pty-rx.bin']['data'], bytes(range(256)))
        mutations = [original.replace(b'index=0 ', b'index=1 ', 1),
                     original.replace(b'00010203', b'00010204', 1),
                     original.replace(b'chunks=4 ', b'chunks=3 ', 1),
                     original.replace(b'chunk_bytes=64', b'chunk_bytes=63', 1),
                     original.replace(b'index=0 00010203', b'index=0 000103', 1),
                     original.replace(b'DL11_RECORD_END', b'CORRUPTED_END', 1),
                     original.replace(b'DL11_RECORD_BEGIN', b'CORRUPTED_BEGIN', 1),
                     original.replace(b'DL11_RECORD_HEX', b'CORRUPTED_HEX', 1),
                     original.replace(b'index=1 ', b'index=0 ', 1),
                     original + original, original[:-40]]
        for value in mutations:
            with self.subTest(value=value[:80]), self.assertRaises(ValueError):
                decode(value, NONCE)
        with self.assertRaises(ValueError):
            decode(original, 'a' * 48)
        # Unrelated console lines may interleave only between complete records.
        self.assertEqual(decode(b'other log\n' + original.replace(b'\n', b'\r\nnoise\r\n'), NONCE)
                         ['pty-rx.bin']['data'], bytes(range(256)))

    def test_missing_required_and_symlink_never_qualify(self):
        p = self.directory / 'pty-rx.bin'
        p.unlink()
        self.assertEqual(self.run_export().returncode, 1)
        p.symlink_to(self.directory / 'pty-tx.bin')
        self.assertEqual(self.run_export().returncode, 1)

    def test_hardlink_public_file_and_size_caps_refused(self):
        p = self.directory / 'pty-rx.bin'
        alias = self.directory / 'alias'
        os.link(p, alias)
        self.assertEqual(self.run_export().returncode, 1)
        alias.unlink()
        p.chmod(0o644)
        self.assertEqual(self.run_export().returncode, 1)
        p.chmod(0o600)
        p.write_bytes(b'a' * 262145)
        self.assertEqual(self.run_export().returncode, 1)

    def test_directory_as_file_is_refused_without_blocking(self):
        p = self.directory / 'pty-rx.bin'
        p.unlink()
        p.mkdir(mode=0o700)
        self.assertEqual(self.run_export().returncode, 1)
        p.rmdir()
        os.mkfifo(p, 0o600)
        self.assertEqual(self.run_export().returncode, 1)

    def test_directory_mode_symlink_and_bad_arguments_refused(self):
        self.directory.chmod(0o755)
        self.assertEqual(self.run_export().returncode, 1)
        self.directory.chmod(0o700)
        link = self.directory.parent / 'link'
        link.symlink_to(self.directory, target_is_directory=True)
        self.assertEqual(self.run_export(directory=link).returncode, 1)
        self.assertEqual(self.run_export('invalid').returncode, 2)
        r = subprocess.run([str(self.binary), '--directory', str(self.directory), '--nonce', 'a',
                            '--phase', 'live'], capture_output=True)
        self.assertEqual(r.returncode, 2)


if __name__ == '__main__':
    unittest.main()
