# SPDX-License-Identifier: MIT
"""Host-only failure-path checks; these do not establish framework conformance."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('compare_tests', Path(__file__).resolve().parents[1]/'tools/compare-tests.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ComparisonPolicy(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('masks', 'exceptions'):
            (self.root/name).write_text('')
        self.row = dict(name='sample', role='candidate', mode='reference',
                        status='OK', sha256='same', skip_lines=1,
                        stdout='candidate image\nvalue 3\n', stderr='')
        self.ref = dict(self.row, role='reference', stdout='reference image\nvalue 3\n')

    def compare(self, reference=True):
        (self.root/'candidate').write_text(json.dumps([self.row]))
        (self.root/'reference').write_text(json.dumps([self.ref]))
        args = SimpleNamespace(candidate=self.root/'candidate',
                               reference=self.root/'reference' if reference else None,
                               masks=self.root/'masks', exceptions=self.root/'exceptions',
                               output=self.root/'result')
        code = runner.compare(args)
        return code, json.loads(args.output.read_text())[0]['status']

    def test_reference_is_required_and_binary_bytes_must_match(self):
        self.assertEqual(self.compare(False), (1, 'BLOCKED'))
        self.ref['sha256'] = 'different'
        self.assertEqual(self.compare(), (1, 'FAIL'))

    def test_image_line_only_is_ignored(self):
        self.assertEqual(self.compare(), (0, 'MATCH'))
        self.row['stdout'] = 'candidate image\nvalue 4\n'
        self.assertEqual(self.compare(), (1, 'DIFF'))

    def test_empty_reference_cannot_pass(self):
        self.row['stdout'] = self.ref['stdout'] = 'image only\n'
        self.assertEqual(self.compare(), (1, 'FAIL'))

    def test_failed_exit_is_not_excused_by_equal_output(self):
        self.row['status'] = 'FAIL'
        self.assertEqual(self.compare(), (1, 'FAIL'))
        self.row['status'] = 'OK'
        self.ref['status'] = 'TIMEOUT'
        self.assertEqual(self.compare(), (1, 'FAIL'))

    def test_pinned_mask_and_exception_still_report_failure(self):
        (self.root/'masks').write_text('sample s#value [0-9]+#value X#\n')
        self.ref['stdout'] = 'reference image\nvalue 20\n'
        self.assertEqual(self.compare(), (0, 'MATCH'))
        (self.root/'exceptions').write_text('sample: documented difference\n')
        self.assertEqual(self.compare(), (1, 'XPASS'))
        (self.root/'masks').write_text('')
        self.assertEqual(self.compare(), (1, 'XFAIL'))

    def test_expected_is_distinct_from_reference_mode(self):
        self.row.update(mode='expected', expected=self.row['stdout'])
        self.assertEqual(self.compare(False), (0, 'MATCH'))
        self.row['expected'] = 'wrong\n'
        self.assertEqual(self.compare(False), (1, 'DIFF'))


if __name__ == '__main__':
    unittest.main()
