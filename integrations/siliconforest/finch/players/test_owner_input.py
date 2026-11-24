# SPDX-License-Identifier: MIT
"""Synthetic marker policy tests only; no SSH, windows, input or device use."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('vlc_launch',Path(__file__).with_name('launch.py'))
launch=importlib.util.module_from_spec(spec);spec.loader.exec_module(launch)

class OwnerPolicy(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'owner-input'
        self.fields=dict(active=0,window_ms=10000,last_real_input_unix_ms=0,
                         real_pointer_records=0,real_key_records=0,updated_unix_ms=100000)
        self.write()
    def write(self):self.path.write_text(''.join(f'{k}={v}\n' for k,v in self.fields.items()))
    def test_idle_and_unchanged_marker(self):
        guard=launch.OwnerInputGuard(self.path,120000);guard.check(150000)
    def test_missing_marker_refuses(self):
        self.path.unlink()
        with self.assertRaises(OSError):launch.OwnerInputGuard(self.path,120000)
    def test_real_active_refuses(self):
        self.fields['active']=1;self.write()
        with self.assertRaises(RuntimeError):launch.OwnerInputGuard(self.path,120000)
    def test_startup_requires_full_idle_window(self):
        with self.assertRaises(RuntimeError):launch.OwnerInputGuard(self.path,109999)
    def test_real_input_even_if_active_already_cleared(self):
        for key in ['last_real_input_unix_ms','real_pointer_records','real_key_records']:
            with self.subTest(key=key):
                self.setUp();guard=launch.OwnerInputGuard(self.path,120000)
                self.fields[key]=110000 if key.startswith('last') else 1;self.write()
                with self.assertRaises(RuntimeError):guard.check(150000)
    def test_malformed_or_future_marker_refuses(self):
        self.path.write_text('active=0\n')
        with self.assertRaises(RuntimeError):launch.OwnerInputGuard(self.path,120000)
        self.fields['updated_unix_ms']=200000;self.write()
        with self.assertRaises(RuntimeError):launch.OwnerInputGuard(self.path,120000)

if __name__=='__main__':unittest.main()
