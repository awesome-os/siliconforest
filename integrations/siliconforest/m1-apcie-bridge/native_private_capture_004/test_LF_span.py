"""Synthetic CRCRLF regression only; never actual-capture requalification."""
import importlib.util,json,tempfile,unittest
from pathlib import Path
import audit as a
from test_audit import fixture,put,reference,sha

class PhysicalLFTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='private-capture004-LF-')
        self.cycle,self.context,self.recovery=fixture(Path(self.temp.name));self.g=self.cycle/'guest'
        self.original=(self.g/'console.bin').read_bytes()
        r=a.audit(self.cycle,self.context,self.recovery);self.assertTrue(r['qualified'],r['checks'])
    def tearDown(self):self.temp.cleanup()
    def change_console(self,raw):
        put(self.g/'console.bin',raw)
        p=self.g/'console-result.json';j=json.loads(p.read_bytes());j.update(console_sha256=sha(raw),console_bytes=len(raw));put(p,j)
        p=Path(self.recovery['path']);j=json.loads(p.read_bytes());j['console_sha256']=sha(raw);put(p,j);self.recovery=reference(p)
    def test_CRCRLF_exact_span_old_failure_new_acceptance(self):
        path=a.BRIDGE/'native_private_capture_003/audit.py';source=path.read_bytes()
        self.assertEqual(sha(source),'7fb0bc19c26cdf7325f68eb899aa6a07f82d9cb527c1effa97be887c88d3edbb')
        old=importlib.util.module_from_spec(importlib.util.spec_from_loader('private_capture_frozen003_LF_regression',loader=None));old.__file__=str(path);exec(compile(source,str(path),'exec'),old.__dict__)
        self.assertTrue(old.audit(self.cycle,self.context,self.recovery)['qualified'])
        changed=self.original.replace(b'\n',b'\r\r\n');self.change_console(changed)
        prior=old.audit(self.cycle,self.context,self.recovery);self.assertFalse(prior['qualified']);self.assertEqual(prior['checks'][a.CHECKS[6]]['status'],'FAILED')
        result=a.audit(self.cycle,self.context,self.recovery);self.assertTrue(result['qualified'],result['checks'])
        self.assertEqual(result['input_sha256']['guest/console.bin'],sha(changed))
    def test_trailing_CR_policy_and_raw_hash_preserved(self):
        for count in (1,2,3):
            with self.subTest(CR=count):
                changed=self.original.replace(b'\n',b'\r'*count+b'\n');self.change_console(changed)
                r=a.audit(self.cycle,self.context,self.recovery);self.assertTrue(r['qualified'],r['checks']);self.assertEqual(r['input_sha256']['guest/console.bin'],sha(changed))
    def test_interior_blank_noise_and_CR_only_lines_refused(self):
        needle=b'DL32_PRIVATE_RECORD_HEX '
        at=self.original.index(needle)
        for extra in (b'\n',b'UNEXPECTED_INTERIOR_NOISE\n',b'\r\n',b'\r\r\n',b'\r'):
            with self.subTest(extra=extra):
                self.change_console(self.original[:at]+extra+self.original[at:])
                r=a.audit(self.cycle,self.context,self.recovery);self.assertFalse(r['qualified']);self.assertEqual(tuple(r['checks']),a.CHECKS);self.assertEqual(r['checks'][a.CHECKS[6]]['status'],'FAILED',r['checks'])
                self.assertEqual(r['checks'][a.CHECKS[1]]['status'],'PASS')

if __name__=='__main__':unittest.main(verbosity=2)
