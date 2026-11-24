# SPDX-License-Identifier: MIT
"""Host marker-policy cases; no device, GUI, SSH or input."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('owner_input',Path(__file__).parents[1]/'tools/owner_input.py')
owner=importlib.util.module_from_spec(spec);spec.loader.exec_module(owner)

class OwnerPolicy(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'owner-input'
        self.fields=dict(active=0,window_ms=10000,last_real_input_unix_ms=0,
                         real_pointer_records=0,real_key_records=0,updated_unix_ms=100000)
        self.write()
    def write(self):self.path.write_text(''.join(f'{k}={v}\n' for k,v in self.fields.items()))
    def test_current_recorder_allows_active_owner(self):
        self.fields['active']=1;self.fields['real_pointer_records']=5;self.write()
        recorder=owner.OwnerActivityRecorder(self.path)
        self.assertEqual(recorder.before['active'],1)
        self.fields['real_key_records']=1;self.write()
        self.assertEqual(recorder.check()['real_key_records'],1)
    def test_current_recorder_records_missing_marker_without_wait(self):
        self.path.unlink();recorder=owner.OwnerActivityRecorder(self.path)
        self.assertEqual(recorder.before['marker_unavailable'],'FileNotFoundError')
    def test_idle_marker_and_unchanged_old_update(self):
        guard=owner.OwnerInputGuard(self.path,400000);guard.check(500000)
    def test_missing_or_malformed_refuses(self):
        self.path.unlink()
        with self.assertRaises(OSError):owner.OwnerInputGuard(self.path,400000)
        self.path.write_text('active=0\n')
        with self.assertRaises(RuntimeError):owner.OwnerInputGuard(self.path,400000)
    def test_active_refuses(self):
        self.fields['active']=1;self.write()
        with self.assertRaises(RuntimeError):owner.OwnerInputGuard(self.path,400000)
    def test_three_minutes_required(self):
        with self.assertRaises(RuntimeError):owner.OwnerInputGuard(self.path,279999)
    def test_changed_real_counters_refuse_after_active_cleared(self):
        for key in ['last_real_input_unix_ms','real_pointer_records','real_key_records']:
            with self.subTest(key=key):
                self.setUp();guard=owner.OwnerInputGuard(self.path,400000)
                self.fields[key]=110000 if key.startswith('last') else 1;self.write()
                with self.assertRaises(RuntimeError):guard.check(500000)
    def test_future_marker_refuses(self):
        self.fields['updated_unix_ms']=600000;self.write()
        with self.assertRaises(RuntimeError):owner.OwnerInputGuard(self.path,400000)

if __name__=='__main__':unittest.main()
