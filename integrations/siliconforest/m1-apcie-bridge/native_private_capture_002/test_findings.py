"""Exact independent001 false-positive regressions; synthetic files only."""
import json,struct,tempfile,unittest
from pathlib import Path
import audit as a
from test_audit import fixture,put,sha

class FindingTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='private-capture002-regression-')
        self.cycle,self.context,self.recovery=fixture(Path(self.temp.name))
        self.g=self.cycle/'guest'
        baseline=a.audit(self.cycle,self.context,self.recovery)
        self.assertTrue(baseline['qualified'],baseline['checks'])
    def tearDown(self):self.temp.cleanup()
    def edit(self,path,fn):
        path=self.g/path if isinstance(path,str) else path
        value=json.loads(path.read_bytes());fn(value);put(path,value)
    def refused(self,index):
        result=a.audit(self.cycle,self.context,self.recovery)
        self.assertFalse(result['qualified']);self.assertEqual(tuple(result['checks']),a.CHECKS)
        self.assertEqual(result['checks'][a.CHECKS[index]]['status'],'FAILED',result['checks'])
        return result
    def region(self,extent):
        original=(self.g/'original-adt.bin').read_bytes()
        old=struct.pack('<6Q',0,0x1000,0x100000,0x400000,0x800000,0x1000)
        new=struct.pack('<6Q',0,0x1000,0x100000,extent,0x800000,0x1000)
        self.assertEqual(original.count(old),1);original=original.replace(old,new)
        put(self.g/'original-adt.bin',original)
        for name in ('policy-mcc-capture.json','policy-mcc-validated.json'):
            self.edit(name,lambda j:j.update(original_adt_sha256=sha(original),region_bytes=extent,reg_property_sha256=sha(new)))
    def test_F1_exact_short_original_reg1(self):
        self.region(0x1000);self.refused(1)
    def test_F1_exact96read_last_word_boundary(self):
        self.region(0xc06dc)
        r=a.audit(self.cycle,self.context,self.recovery);self.assertTrue(r['qualified'],r['checks'])
    def test_F1_last_word_outside_aperture(self):
        self.region(0xc06d8);self.refused(1)
    def test_F2_exact_zero_pointer_metadata(self):
        def change(j):
            j['start_pointer_state'].update(guest_base=0,bootargs_off=0,address=0,entry=0)
            j['checked_allocation'].update(base=0,expected_address=0)
        self.edit('guest-bootargs-record.json',change)
        self.edit('before-start.json',lambda j:j['layout'].update(guest_base=0,bootargs_off=0,address=0,entry=0))
        self.refused(2)
    def test_F2_coherent_positive_forged_allocation(self):
        def change(j):
            for key in ('bootargs_off','address'):j['start_pointer_state'][key]-=16384
            for key in ('base','expected_address'):j['checked_allocation'][key]-=16384
        self.edit('guest-bootargs-record.json',change)
        for name in ('before-start.json','original-observations.json'):
            def change_layout(j):
                for key in ('bootargs_off','address'):j['layout'][key]-=16384
            self.edit(name,change_layout)
        self.refused(2)
    def test_F2_coherent_entry_report_substitution(self):
        self.edit('guest-bootargs-record.json',lambda j:j['start_pointer_state'].update(entry=j['start_pointer_state']['entry']+4))
        for name in ('before-start.json','original-observations.json'):
            self.edit(name,lambda j:j['layout'].update(entry=j['layout']['entry']+4))
        self.edit(self.cycle/'verified-inputs.json',lambda j:j['candidate']['kernel'].update(entry=j['candidate']['kernel']['entry']+4))
        r=self.refused(2);self.assertIn('actual selected kernel bytes',str(r['checks'][a.CHECKS[2]]['errors']))
    def artifact_maps(self,fn):
        for path,key in ((self.cycle/'verified-inputs.json','candidate'),(self.g/'original-observations.json','packet')):
            self.edit(path,lambda j:fn(j[key]['artifacts']))
    def test_F3_exact_unbound_source_and_build_hashes(self):
        self.artifact_maps(lambda m:m.update(build_record='00'*32,source_patch='11'*32));self.refused(8)
    def test_F3_missing_or_extra_artifact(self):
        for extra in (False,True):
            paths=[self.cycle/'verified-inputs.json',self.g/'original-observations.json'];saved=[p.read_bytes() for p in paths]
            self.artifact_maps(lambda m:m.update(unreviewed='22'*32) if extra else m.pop('build_record'))
            self.refused(8)
            for p,data in zip(paths,saved):put(p,data)

if __name__=='__main__':unittest.main(verbosity=2)
