#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the updater's real rollback branch against scratch files, no mounts/devices.

Only mount/umount/sync/blkid are replaced with host recorders. The real shell
rename loop and image validator execute. The image bytes are structural fixtures,
not executable boot images or firmware. SF_INSTALL_SCRIPT selects the old script
for the before/after regression.
"""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

HERE = Path(__file__).resolve().parent
SCRIPT = Path(os.environ.get('SF_INSTALL_SCRIPT', HERE / 'linux/sf-bootbin-install.sh'))
ROOT = 'rollback-host-test-root'


def boot(size=65536, marker=b'current', root=ROOT):
    b = bytearray(size)
    b[0x808:0x810] = b'AURALOAD'
    struct.pack_into('<Q', b, 0x818, len(b))
    text = b'SF_STOPPED sf/kernel.bin siliconforest-boot-v ' + root.encode() + b' ' + marker
    b[0x1000:0x1000 + len(text)] = text
    return bytes(b)


def kernel(marker=b'current', extra='', root=ROOT):
    def record(kind, body):
        header = struct.pack('<IHHIIQQ', 0x46534450, 1, kind, 0, zlib.crc32(body), len(body), len(body))
        return header + body + bytes(-(32 + len(body)) % 16)
    config = ('rd=uuid boot-uuid=' + root + ' ' + extra + '\n').encode()
    return record(1, config) + record(2, marker) + struct.pack('<IHHIIQQ', 0x46534450, 1, 0, 0, 0, 0, 0)


class Rollback(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        self.sf = self.path / 'sf'
        self.sf.mkdir()
        (self.sf / 'boot.bin').write_bytes(boot())
        (self.sf / 'kernel.bin').write_bytes(kernel())

    def tree(self):
        return {p.name: p.read_bytes() for p in self.sf.iterdir()}

    def run_rollback(self):
        text = SCRIPT.read_text()
        start = text.index('check() {')
        end = text.index('\n}\n', start) + 3
        validator = text[start:end]
        start = text.index('if [ "$WHAT" = --rollback ]; then')
        # The normal-install file check immediately follows the rollback branch.
        end = text.index('[ -f "$WHAT" ] && [ -f "$KERNEL" ]', start)
        branch = text[start:end]
        source = '''set -euo pipefail
MNT=$1; DEV=fixture; ROOT=fixture; WHAT=--rollback; NAMES="boot.bin kernel.bin"
mount() { echo "MOUNT $*"; }
umount() { echo UMOUNT; }
sync() { echo SYNC; }
blkid() { echo rollback-host-test-root; }
refuse() { echo "REFUSED: $*" >&2; exit 3; }
mv() { echo "RENAME $*"; command mv "$@"; }
''' + validator + '\n' + branch
        return subprocess.run(['bash', '-c', source, 'rollback-test', str(self.path)],
                              capture_output=True, text=True)

    def assert_refused_unchanged(self, before, result):
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertNotIn('RENAME ', result.stdout)
        self.assertNotIn('rw,noatime', result.stdout)
        self.assertEqual(self.tree(), before)

    def test_original_oversized_stage2_is_refused_before_any_rename(self):
        (self.sf / 'boot.bin.old').write_bytes(boot(14423700, b'old'))
        (self.sf / 'kernel.bin.old').write_bytes(kernel(b'old'))
        before = self.tree()
        r = self.run_rollback()
        self.assert_refused_unchanged(before, r)
        self.assertIn('do not fit', r.stderr)

    def test_kernel_only_rollback_keeps_current_valid_boot(self):
        old = kernel(b'old')
        (self.sf / 'kernel.bin.old').write_bytes(old)
        before = self.tree()
        r = self.run_rollback()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual((self.sf / 'boot.bin').read_bytes(), before['boot.bin'])
        self.assertEqual((self.sf / 'kernel.bin').read_bytes(), old)
        self.assertEqual((self.sf / 'kernel.bin.old').read_bytes(), before['kernel.bin'])

    def test_valid_pair_swaps_both_and_can_swap_back(self):
        (self.sf / 'boot.bin.old').write_bytes(boot(marker=b'old'))
        (self.sf / 'kernel.bin.old').write_bytes(kernel(b'old'))
        before = self.tree()
        r = self.run_rollback()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual((self.sf / 'boot.bin').read_bytes(), before['boot.bin.old'])
        self.assertEqual((self.sf / 'kernel.bin').read_bytes(), before['kernel.bin.old'])
        self.assertEqual(self.run_rollback().returncode, 0)
        self.assertEqual(self.tree(), before)

    def test_bad_previous_kernel_checksum_is_refused(self):
        damaged = bytearray(kernel(b'old'))
        damaged[33] ^= 1
        (self.sf / 'kernel.bin.old').write_bytes(damaged)
        before = self.tree()
        r = self.run_rollback()
        self.assert_refused_unchanged(before, r)
        self.assertIn('checksum', r.stderr)

    def test_previous_bench_kernel_is_refused(self):
        (self.sf / 'kernel.bin.old').write_bytes(kernel(extra='sfsession_seconds=150 sfmark=1'))
        before = self.tree()
        r = self.run_rollback()
        self.assert_refused_unchanged(before, r)
        self.assertIn('bench image', r.stderr)

    def test_previous_image_for_another_root_is_refused(self):
        (self.sf / 'kernel.bin.old').write_bytes(kernel(root='another-host-test-root'))
        before = self.tree()
        r = self.run_rollback()
        self.assert_refused_unchanged(before, r)
        self.assertIn('another root', r.stderr)

    def test_no_previous_files_changes_nothing(self):
        before = self.tree()
        r = self.run_rollback()
        self.assertEqual(r.returncode, 2)
        self.assertEqual(self.tree(), before)
        self.assertNotIn('RENAME ', r.stdout)


if __name__ == '__main__':
    unittest.main()
