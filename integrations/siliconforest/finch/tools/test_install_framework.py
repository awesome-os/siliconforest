# SPDX-License-Identifier: MIT
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('installer', Path(__file__).with_name('install-framework.py'))
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class FrameworkInstallTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)/'root'
        self.source = Path(self.tmp.name)/'source'
        self.rel = Path('Versions/A/Example')
        self.dest = installer.framework_root(self.root, 'Example')/self.rel
        self.dest.parent.mkdir(parents=True)
        self.dest.write_bytes(b'old implementation')
        (self.source/self.rel).parent.mkdir(parents=True)
        (self.source/self.rel).write_bytes(b'new implementation')
        self.manifest = dict(framework='Example', entries=[dict(path=str(self.rel), kind='file', sha256=hashlib.sha256(b'new implementation').hexdigest())])
        self.journal = Path(self.tmp.name)/'receipt.json'

    def test_bad_hash_changes_no_target(self):
        (self.source/self.rel).write_bytes(b'tampered')
        with self.assertRaises(ValueError):
            installer.install(self.manifest, self.source, self.root, self.journal)
        self.assertEqual(self.dest.read_bytes(), b'old implementation')
        self.assertFalse(self.dest.with_name('Example.pre-finch').exists())

    def test_cold_hash_backup_and_rollback(self):
        receipt = installer.install(self.manifest, self.source, self.root, self.journal)
        self.assertEqual(self.dest.read_bytes(), b'new implementation')
        self.assertTrue(json.loads(self.journal.read_text())['verified'][0]['before_activation'])
        self.assertEqual(self.dest.with_name('Example.pre-finch').read_bytes(), b'old implementation')
        installer.rollback(receipt, self.root)
        self.assertEqual(self.dest.read_bytes(), b'old implementation')
        self.assertEqual(self.dest.with_name('Example.pre-finch').read_bytes(), b'old implementation')

    def test_existing_backup_is_never_overwritten(self):
        self.dest.with_name('Example.pre-finch').write_bytes(b'original')
        with self.assertRaises(ValueError):
            installer.install(self.manifest, self.source, self.root, self.journal)
        self.assertEqual(self.dest.read_bytes(), b'old implementation')
        self.assertEqual(self.dest.with_name('Example.pre-finch').read_bytes(), b'original')

    def test_explicit_update_rollback_restores_prior_version_and_first_original(self):
        first = installer.install(self.manifest, self.source, self.root, self.journal)
        self.assertEqual(self.dest.read_bytes(), b'new implementation')
        (self.source/self.rel).write_bytes(b'updated implementation')
        self.manifest['entries'][0]['sha256'] = hashlib.sha256(b'updated implementation').hexdigest()
        second = installer.install(self.manifest, self.source, self.root,
                                   self.journal.with_name('update.json'), update=True)
        self.assertEqual(self.dest.read_bytes(), b'updated implementation')
        self.assertEqual(self.dest.with_name('Example.pre-finch').read_bytes(), b'old implementation')
        snapshot = installer.framework_root(self.root, 'Example')/second['activated'][0]['backup_path']
        self.assertEqual(snapshot.read_bytes(), b'new implementation')
        installer.rollback(second, self.root)
        self.assertEqual(self.dest.read_bytes(), b'new implementation')
        self.assertEqual(self.dest.with_name('Example.pre-finch').read_bytes(), b'old implementation')
        installer.rollback(first, self.root)
        self.assertEqual(self.dest.read_bytes(), b'old implementation')

    def test_external_link_rejected_before_activation(self):
        (self.source/'Escape').symlink_to('/etc')
        self.manifest['entries'].append(dict(path='Escape', kind='symlink', target='/etc'))
        with self.assertRaises(ValueError):
            installer.install(self.manifest, self.source, self.root, self.journal)
        self.assertEqual(self.dest.read_bytes(), b'old implementation')

    def test_real_current_directory_is_preserved_and_restored(self):
        base = self.dest.parents[2]
        current = base/'Versions/Current'
        current.mkdir()
        (current/'Example').write_bytes(b'legacy Current copy')
        (self.source/'Versions/Current').symlink_to('A')
        self.manifest['entries'].insert(0, dict(path='Versions/Current', kind='symlink', target='A'))
        receipt = installer.install(self.manifest, self.source, self.root, self.journal)
        self.assertTrue(current.is_symlink())
        self.assertEqual((current.with_name('Current.pre-finch')/'Example').read_bytes(), b'legacy Current copy')
        installer.rollback(receipt, self.root)
        self.assertFalse(current.is_symlink())
        self.assertEqual((current/'Example').read_bytes(), b'legacy Current copy')

    def test_retry_after_rollback_preserves_first_original(self):
        receipt = installer.install(self.manifest, self.source, self.root, self.journal)
        installer.rollback(receipt, self.root)
        receipt = installer.install(self.manifest, self.source, self.root, self.journal.with_name('retry.json'))
        self.assertEqual(self.dest.with_name('Example.pre-finch').read_bytes(), b'old implementation')


if __name__ == '__main__':
    unittest.main()
