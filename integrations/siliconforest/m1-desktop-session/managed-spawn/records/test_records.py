#!/usr/bin/env python3
"""Read-only exporter/decoder checks on host files, never Darwin spawn proof."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from decode_records import decode

HERE = Path(__file__).resolve().parent
NONCE = '0123456789abcdef' * 3
FILES = {'result.json': b'{"synthetic":true}\n', 'stdout.bin': b'out\x00\xff',
         'stderr.bin': b'err\r\n', 'status.bin': b'status\n'}


class Records(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='spawn-records-host-')
        cls.binary = Path(cls.build.name) / 'records'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-O1', str(HERE / 'spawn-records.c'), '-lcrypto', '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='spawn-records-files-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name) / 'records'
        self.directory.mkdir(mode=0o700)
        for name, data in FILES.items():
            p = self.directory / name
            p.write_bytes(data)
            p.chmod(0o600)

    def export(self, phase='closed', directory=None):
        return subprocess.run([str(self.binary), '--directory', str(directory or self.directory),
                               '--nonce', NONCE, '--phase', phase], capture_output=True, timeout=2)

    def test_exact_closed_records(self):
        result = self.export()
        self.assertEqual(result.returncode, 0)
        records = decode(result.stdout, NONCE)
        self.assertEqual(set(records), set(FILES))
        for name, value in FILES.items():
            self.assertEqual(records[name]['data'], value)
            self.assertEqual(records[name]['file_bytes_after'], len(value))

    def test_all_required_and_no_live(self):
        self.assertEqual(self.export('live').returncode, 2)
        with self.assertRaises(ValueError):
            decode(self.export().stdout, NONCE, 'live')
        (self.directory / 'status.bin').unlink()
        result = self.export()
        self.assertEqual(result.returncode, 1)
        with self.assertRaises(ValueError):
            decode(result.stdout, NONCE)

    def test_hash_length_order_count_and_complete_set(self):
        (self.directory / 'stdout.bin').write_bytes(bytes(range(256)))
        raw = self.export().stdout
        self.assertEqual(decode(raw, NONCE)['stdout.bin']['data'], bytes(range(256)))
        mutations = [raw.replace(b'index=0 ', b'index=1 ', 1),
                     raw.replace(b'00010203', b'00010204', 1),
                     raw.replace(b'chunks=4 ', b'chunks=3 ', 1),
                     raw.replace(b'chunk_bytes=64', b'chunk_bytes=63', 1),
                     raw.replace(b'DL19_RECORD_END', b'LOST_END', 1),
                     raw.replace(b'DL19_RECORD_HEX', b'LOST_HEX', 1),
                     raw + raw, raw[:-40], raw.replace(NONCE.encode(), b'a' * 48, 1)]
        for mutant in mutations:
            with self.subTest(mutant=mutant[:60]), self.assertRaises(ValueError):
                decode(mutant, NONCE)

    def test_symlink_hardlink_mode_and_oversize(self):
        p = self.directory / 'stdout.bin'
        alias = self.directory / 'alias'
        os.link(p, alias)
        self.assertEqual(self.export().returncode, 1)
        alias.unlink()
        p.chmod(0o644)
        self.assertEqual(self.export().returncode, 1)
        p.chmod(0o600)
        p.write_bytes(b'a' * 257)
        self.assertEqual(self.export().returncode, 1)
        p.unlink()
        p.symlink_to(self.directory / 'stderr.bin')
        self.assertEqual(self.export().returncode, 1)

    def test_nonregular_or_public_directory(self):
        p = self.directory / 'stdout.bin'
        p.unlink()
        os.mkfifo(p, 0o600)
        self.assertEqual(self.export().returncode, 1)
        p.unlink()
        p.mkdir(mode=0o700)
        self.assertEqual(self.export().returncode, 1)
        self.directory.chmod(0o755)
        self.assertEqual(self.export().returncode, 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
