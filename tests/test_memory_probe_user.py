"""Real host POSIX control. No XNU/device execution and no successful skips."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class UserProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.binary=Path(cls.tmp.name)/'probe'
        cc=shutil.which('clang') or shutil.which('cc')
        if not cc:raise RuntimeError('C compiler required')
        r=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(ROOT/'probe'),
                          str(ROOT/'userspace/memory_probe_smoke.c'),'-o',str(cls.binary)],capture_output=True,text=True,timeout=60)
        if r.returncode:raise RuntimeError(r.stderr)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def run_probe(self,*args):
        return subprocess.run([str(self.binary),*args],capture_output=True,text=True,timeout=40)
    def test_actual_posix_control(self):
        r=self.run_probe('--posix-control');self.assertEqual(r.returncode,0,r.stderr)
        data=json.loads(r.stdout);self.assertEqual(data['status'],'PASSED_POSIX_CONTROL')
        self.assertFalse(data['xnu_claim'])
        self.assertEqual(data['evidence'],'host_posix_control')
        self.assertFalse(data['host_verified'])
        self.assertEqual(data['checks'],17)  # Count checks, not individual bytes.
    def test_no_implicit_device(self):self.assertEqual(self.run_probe().returncode,2)
    def test_requires_opt_in(self):self.assertEqual(self.run_probe('--device','/does/not/exist').returncode,2)
    def test_conflicting_modes(self):self.assertEqual(self.run_probe('--posix-control','--allow-experimental').returncode,2)
    def test_no_control_device_mix(self):self.assertEqual(self.run_probe('--posix-control','--device','/dev/null').returncode,2)
    def test_missing_device_fails(self):
        r=self.run_probe('--device','/does/not/exist','--allow-experimental')
        self.assertEqual(r.returncode,1);self.assertEqual(json.loads(r.stdout)['status'],'FAILED')
    def test_non_probe_device_fails(self):
        r=self.run_probe('--device','/dev/null','--allow-experimental')
        self.assertEqual(r.returncode,1);data=json.loads(r.stdout)
        self.assertFalse(data['xnu_claim']);self.assertEqual(data['evidence'],'guest_self_report')
        self.assertFalse(data['host_verified'])
    def test_unmapped_not_host_control(self):self.assertEqual(self.run_probe('--posix-control','--unmapped-only').returncode,2)
if __name__=='__main__':unittest.main()
