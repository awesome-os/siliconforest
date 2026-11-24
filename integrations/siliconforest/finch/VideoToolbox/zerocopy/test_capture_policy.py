# SPDX-License-Identifier: MIT
"""Host classifier policy only: synthetic receipts never prove target activity."""
import copy
from fractions import Fraction
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('capture_policy',Path(__file__).with_name('qualify-capture.py'))
policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)
class Policy(unittest.TestCase):
    def setUp(self):
        self.expected=[(Fraction(n,60),1920,1080,'a'*32) for n in range(65)]
        self.stream={'time_base':'1/15360'}
        self.before={k:0 for k in ['jobs','done','errors','timeouts','stale','dart_faults','contexts','dart_poisoned','quarantined_desc']}
        self.after=dict(self.before,jobs=65,done=65)
        self.record={'status':'OK','returncode':0,'stdout':'VT_SESSION codec avcC 1920x1080 hardware 1\n'+''.join(f'SFAVD_FRAME {n} pts {n*256} 1920x1080 '+ 'a'*32+' us 0 st 0x0\n' for n in range(65))+'CAPTURE_HELD stable-after-64-frames=1\nCAPTURE_SUMMARY GPU-frames=65 held-checks=1 errors=0\n','stderr':'SF_VT_GPU requested=1 required=1 available=1 kernel-cap=1 cv-lifetime=1\nSF_VT_GPU frames=65 copied-bytes=0 required=1\n'}
    def result(self):return policy.qualify(self.record,self.expected,self.stream,self.before,self.after)
    def test_synthetic_control_only(self):self.assertEqual(self.result()['status'],'PASS')
    def test_cpu_copy_and_ignored_strict_environment_refused(self):
        self.record['stderr']=self.record['stderr'].replace('copied-bytes=0','copied-bytes=202176000');self.assertEqual(self.result()['status'],'FAIL')
        self.record['stderr']='';self.assertEqual(self.result()['status'],'FAIL')
    def test_zero_frame_success_cannot_qualify(self):
        self.record['stdout']='';self.assertEqual(self.result()['status'],'FAIL')
        self.expected=[];self.assertEqual(self.result()['status'],'FAIL')
    def test_duplicate_or_wrong_timestamp_and_pixel_refused(self):
        for source,replacement in [('pts 256','pts 0'),('pts 256','pts 257'),('a'*32,'b'*32)]:
            r=copy.deepcopy(self.record);r['stdout']=r['stdout'].replace(source,replacement,1)
            self.assertEqual(policy.qualify(r,self.expected,self.stream,self.before,self.after)['status'],'FAIL')
    def test_kernel_activity_and_faults_are_required(self):
        for changes in [dict(jobs=64),dict(done=64),dict(timeouts=1),dict(quarantined_desc=1),dict(dart_poisoned=1),dict(contexts=1)]:
            self.assertEqual(policy.qualify(self.record,self.expected,self.stream,self.before,dict(self.after,**changes))['status'],'FAIL')
        self.after.pop('dart_poisoned');self.assertEqual(self.result()['status'],'FAIL')
    def test_inherited_nonunit_time_base_is_rejected(self):
        self.stream['time_base']='1001/60000'
        with self.assertRaises(ValueError):self.result()
if __name__=='__main__':unittest.main()
