"""Synthetic files in a temporary directory; no mounted ESP or hardware access."""
from contextlib import ExitStack
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools import m1_boot_install as install


def image(footprint, payload):
    data = bytearray(footprint)
    data[0x808:0x810] = b'AURALOAD'
    struct.pack_into('<IIQQ', data, 0x810, 1, 1, footprint, 0)
    data[0x1000:0x1000 + len(install.TAG)] = install.TAG
    return bytes(data) + payload


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.esp = root / 'esp'
        (self.esp / 'm1n1').mkdir(parents=True)
        self.target = self.esp / 'm1n1/boot.bin'
        self.backup = self.esp / 'm1n1/boot.bin.darwin-lab-original-fixture'
        self.state = root / 'state/operations'
        self.original = image(0x400000, b'SYNTHETIC LINUX PAYLOAD, NOT BOOTABLE')
        self.candidate = image(0x420000, self.original[0x400000:])
        self.original_path = root / 'original.bin'
        self.candidate_path = root / 'candidate.bin'
        self.original_path.write_bytes(self.original)
        self.candidate_path.write_bytes(self.candidate)
        self.target.write_bytes(self.original)
        self.old = self.target.with_name('boot.bin.old')
        self.old.write_bytes(b'UNRELATED LEGACY BACKUP')
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        for name, value in [('ESP', self.esp), ('TARGET', self.target), ('BACKUP', self.backup),
                            ('STATE', self.state), ('ORIGINAL_SHA', install.sha(self.original)),
                            ('CANDIDATE_SHA', install.sha(self.candidate)),
                            ('PREFIX_SHA', install.sha(self.candidate[:0x420000]))]:
            self.stack.enter_context(patch.object(install, name, value))
        self.stack.enter_context(patch.object(install, 'validate_machine'))

    def plan(self, action='install'):
        return install.inspect(action, self.original_path,
                               self.candidate_path if action == 'install' else None)

    def test_plan_does_not_create_backup_or_operation_state(self):
        plan, _, _ = self.plan()
        self.assertEqual(plan['status'], 'PLAN_ONLY_NO_BOOT_STORAGE_WRITES')
        self.assertFalse(plan['hardware_boot_validated'])
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertFalse(self.backup.exists())
        self.assertFalse(self.state.exists())

    def test_install_and_restore_preserve_original_and_legacy_backup(self):
        result = install.execute(*self.plan(), 'install-001')
        self.assertEqual(result['status'], 'BOOT_FILE_REPLACED_NOT_BOOT_VALIDATED')
        self.assertEqual(self.target.read_bytes(), self.candidate)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertTrue((self.state / 'install-001/result.json').is_file())
        install.execute(*self.plan('restore'), 'restore-001')
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertEqual(self.old.read_bytes(), b'UNRELATED LEGACY BACKUP')

    def test_target_drift_after_plan_refuses_overwrite(self):
        args = self.plan()
        self.target.write_bytes(b'OTHER WRITER')
        with self.assertRaisesRegex(ValueError, 'CAS drift'):
            install.execute(*args, 'drift-001')
        self.assertEqual(self.target.read_bytes(), b'OTHER WRITER')
        self.assertFalse(self.backup.exists())

    def test_bad_candidate_fails_before_any_state_or_backup(self):
        self.candidate_path.write_bytes(self.candidate + b'DRIFT')
        with self.assertRaises(ValueError):
            self.plan()
        self.assertFalse(self.state.exists())
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_failed_rename_keeps_original_and_verified_backup(self):
        with patch.object(install.os, 'replace', side_effect=OSError('injected rename failure')):
            with self.assertRaisesRegex(OSError, 'rename failure'):
                install.execute(*self.plan(), 'rename-001')
        self.assertEqual(self.target.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertFalse((self.state / 'rename-001/result.json').exists())

    def test_existing_operation_is_never_replayed_after_restore(self):
        install.execute(*self.plan(), 'once-001')
        install.execute(*self.plan('restore'), 'restore-001')
        with self.assertRaises(FileExistsError):
            install.execute(*self.plan(), 'once-001')
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_unexpected_target_and_symlink_backup_fail_before_writes(self):
        self.target.write_bytes(b'UNREVIEWED CURRENT FILE')
        with self.assertRaisesRegex(ValueError, 'drifted'):
            self.plan()
        self.target.write_bytes(self.original)
        self.backup.symlink_to(self.esp / 'missing')
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.plan()
        self.assertFalse(self.state.exists())

    def test_insufficient_space_refused_without_creating_files(self):
        with patch.object(install.os, 'statvfs', return_value=SimpleNamespace(f_bavail=0, f_frsize=4096)):
            with self.assertRaisesRegex(ValueError, 'space'):
                self.plan()
        self.assertFalse(self.backup.exists())

    def test_invalid_operation_id_refused_before_state_creation(self):
        with self.assertRaisesRegex(ValueError, 'operation ID'):
            install.execute(*self.plan(), '../outside')
        self.assertFalse(self.state.exists())

    def test_candidate_and_recovery_symlinks_refused(self):
        link = self.candidate_path.with_suffix('.link')
        link.symlink_to(self.candidate_path)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            install.inspect('install', self.original_path, link)

    def test_backup_verification_failure_never_changes_boot_file(self):
        self.backup.write_bytes(b'BAD BACKUP')
        with self.assertRaisesRegex(ValueError, 'backup differs'):
            self.plan()
        self.assertEqual(self.target.read_bytes(), self.original)


    def test_payload_change_cannot_be_hidden_by_relabelled_artifact_hash(self):
        changed = self.candidate[:-1] + b'!'
        self.candidate_path.write_bytes(changed)
        with patch.object(install, 'CANDIDATE_SHA', install.sha(changed)):
            with self.assertRaisesRegex(ValueError, 'payload changed'):
                self.plan()
        self.assertFalse(self.state.exists())

    def test_duplicate_tag_refused_even_if_byte_hashes_are_relabelled(self):
        changed = bytearray(self.candidate)
        changed[0x2000:0x2000+len(install.TAG)] = install.TAG
        self.candidate_path.write_bytes(changed)
        with patch.object(install, 'CANDIDATE_SHA', install.sha(changed)), \
             patch.object(install, 'PREFIX_SHA', install.sha(changed[:0x420000])):
            with self.assertRaisesRegex(ValueError, 'tag mismatch'):
                self.plan()

    def test_wrong_load_header_rejected_before_writing(self):
        changed = bytearray(self.original)
        struct.pack_into('<I', changed, 0x814, 0)
        self.original_path.write_bytes(changed)
        with patch.object(install, 'ORIGINAL_SHA', install.sha(changed)):
            with self.assertRaisesRegex(ValueError, 'header'):
                self.plan()
        self.assertFalse(self.backup.exists())

    def test_failed_operation_blocks_new_install_plan(self):
        with patch.object(install.os, 'replace', side_effect=OSError('injected failure')):
            with self.assertRaises(OSError):
                install.execute(*self.plan(), 'incomplete-001')
        with self.assertRaisesRegex(ValueError, 'staged boot file remains'):
            self.plan()
        self.target.with_name('.boot.bin.darwin-lab-incomplete-001.tmp').unlink()
        with self.assertRaisesRegex(ValueError, 'operation is incomplete'):
            self.plan()
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_postwrite_read_failure_keeps_original_backup_and_no_success_receipt(self):
        def verify(path, expected):
            if path == self.target:
                raise ValueError('BOOT_FILE_UNVERIFIED_DO_NOT_REBOOT')
        with patch.object(install, 'verify_after_sync', side_effect=verify):
            with self.assertRaisesRegex(ValueError, 'DO_NOT_REBOOT'):
                install.execute(*self.plan(), 'unverified-001')
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertFalse((self.state / 'unverified-001/result.json').exists())
        with self.assertRaisesRegex(ValueError, 'operation is incomplete'):
            self.plan()

    def test_live_esp_and_stage2_identity_must_match(self):
        uuid = '00000000-0000-4000-8000-0000feedface'
        install.validate_boot_identity(uuid + '\n', uuid.upper().encode() + b'\0', b'f5a82fdb-dirty\0')
        install.validate_boot_identity(uuid, uuid.encode(), install.TAG[len(b'##m1n1_ver##'):])
        with self.assertRaisesRegex(ValueError, 'ESP differs'):
            install.validate_boot_identity(uuid, b'other-esp', b'f5a82fdb-dirty')
        with self.assertRaisesRegex(ValueError, 'stage2 version'):
            install.validate_boot_identity(uuid, uuid.encode(), b'other-loader')

