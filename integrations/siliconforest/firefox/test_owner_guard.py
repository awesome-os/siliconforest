# SPDX-License-Identifier: MIT
"""Synthetic owner-marker policies only; no target execution or input."""
from pathlib import Path
import tempfile
import unittest
from owner_guard import OwnerInputGuard


class OwnerGuardPolicy(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'owner-input'
        self.fields=dict(active=0,window_ms=10000,last_real_input_unix_ms=100000,
                         updated_unix_ms=100000,real_pointer_records=5,real_key_records=2)
        self.write()
    def write(self):
        self.path.write_text('# DESK7: active=1 means real input; this comment is not state\n'+''.join(f'{k}={v}\n' for k,v in self.fields.items()))
    def test_idle_uses_exact_state_lines(self):
        guard=OwnerInputGuard(self.path,120000);guard.check(130000)
    def test_active_and_new_session_refuse(self):
        self.fields['active']=1;self.write()
        with self.assertRaises(RuntimeError):OwnerInputGuard(self.path,120000)
        self.fields['active']=0;self.write()
        with self.assertRaises(RuntimeError):OwnerInputGuard(self.path,109999)
    def test_real_input_cannot_be_erased_by_idle(self):
        for key in ['last_real_input_unix_ms','real_pointer_records','real_key_records']:
            with self.subTest(key=key):
                self.setUp();guard=OwnerInputGuard(self.path,120000)
                self.fields[key]+=1;self.write()
                with self.assertRaises(RuntimeError):guard.check(150000)
    def test_missing_and_future_markers_refuse(self):
        self.path.unlink()
        with self.assertRaises(OSError):OwnerInputGuard(self.path,120000)
        self.fields['updated_unix_ms']=200000;self.write()
        with self.assertRaises(RuntimeError):OwnerInputGuard(self.path,120000)

    def test_functional_checks_record_active_owner_without_waiting(self):
        self.fields['active']=1;self.write()
        guard=OwnerInputGuard(self.path,100001,require_idle=False)
        self.fields['real_key_records']+=1;self.write()
        self.assertEqual(guard.check(100002)['real_key_records'],3)


if __name__=='__main__':unittest.main()
