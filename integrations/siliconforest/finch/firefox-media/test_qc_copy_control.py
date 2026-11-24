# SPDX-License-Identifier: MIT
"""Private provider/counter admission refusals; no native claims."""
import copy
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import qc_copy_control as qc

ROOT=Path('/private/var/db/sffx/firefox-media-cadence-packet-qc-test')
IDENTITY=dict(quartzcore=dict(uuid='aa'*16,offset=256),cgl=dict(uuid='bb'*16,offset=512))

def providers():
    images=[]
    for name in ['AppKit','Onyx2D','CoreText','QuartzCore','IOSurface']:
        images.append(f'1000 - 2000 {name} <{"cc"*16}> /System/Library/Frameworks/{name}.framework/Versions/A/{name}')
    for name,path,uuid in [('libWindowServer.dylib','/usr/lib/libWindowServer.dylib','cc'*16),
            ('libGL.1.dylib','/usr/local/lib/siliconforest/opengl/v1/lib/libGL.1.dylib','cc'*16),
            ('QuartzCore.pd',str(ROOT/'qc-nv12/lib/QuartzCore.pd'),'aa'*16),
            ('OpenGL',str(ROOT/'qc-nv12/Frameworks/OpenGL.framework/Versions/A/OpenGL'),'bb'*16)]:
        images.append(f'1000 - 2000 {name} <{uuid}> {path}')
    return dict(processes=[dict(pid=pid,role=role,status=0,images=list(images)) for pid,role in [(123,'parent'),(124,'gpu')]])

class Admission(unittest.TestCase):
    def test_only_private_qc_and_cgl_admitted(self):
        result=providers();self.assertEqual(len(qc.verify(result,IDENTITY,ROOT)),2)
        self.assertTrue(result['complete']);self.assertFalse(result['expected_system_gpu'])
        self.assertFalse(result['canonical_cocoa_complete'])

    def test_wrong_path_uuid_duplicate_or_foreign_ui_refused(self):
        for change in [lambda p:p['images'].__setitem__(0,p['images'][0].replace('/System/','/private/')),
                lambda p:p['images'].__setitem__(-2,p['images'][-2].replace('aa'*16,'dd'*16)),
                lambda p:p['images'].__setitem__(-1,p['images'][-1].replace('qc-nv12','other')),
                lambda p:p['images'].append(p['images'][-2]),lambda p:p.update(status=1)]:
            result=providers();change(result['processes'][0])
            with self.assertRaises(RuntimeError):qc.verify(result,IDENTITY,ROOT)
        result=providers();result['processes'].pop()
        with self.assertRaises(RuntimeError):qc.verify(result,IDENTITY,ROOT)

    def test_counter_reads_stay_within_exact_ten_value_array(self):
        verified=qc.verify(providers(),IDENTITY,ROOT)[:1];calls=[]
        def run(command,**kwargs):
            address=int(command[-1],16);calls.append(address)
            offset=(address-0x1100)//8
            return SimpleNamespace(returncode=0,stdout=json.dumps(dict(pid=123,task_status=0,read_status=0,
                read_bytes=32,raw_u64=list(range(offset,offset+4)))))
        with patch.object(qc.subprocess,'check_output',return_value='start\n'),patch.object(qc.subprocess,'run',side_effect=run):
            result=qc.snapshot(verified,IDENTITY,Path('/own/helper'),{123:'start'})
        self.assertEqual(calls,[0x1100,0x1120,0x1130])
        self.assertEqual(list(result['processes'][0]['values'].values()),list(range(10)))

    def test_changed_process_identity_or_native_pid_refused(self):
        verified=qc.verify(providers(),IDENTITY,ROOT)[:1]
        with patch.object(qc.subprocess,'check_output',return_value='other\n'):
            with self.assertRaises(RuntimeError):qc.snapshot(verified,IDENTITY,Path('/own/helper'),{123:'start'})
        fake=SimpleNamespace(returncode=0,stdout=json.dumps(dict(pid=99,read_bytes=32,raw_u64=[0]*4)))
        with patch.object(qc.subprocess,'check_output',return_value='start\n'),patch.object(qc.subprocess,'run',return_value=fake):
            with self.assertRaises(RuntimeError):qc.snapshot(verified,IDENTITY,Path('/own/helper'),{123:'start'})

    def test_reversed_or_replaced_counters_refused(self):
        before=dict(processes=[dict(pid=123,role='parent',os_start_identity='start',uuid='aa',provider='/own',values={k:1 for k in qc.FIELDS})])
        after=copy.deepcopy(before);after['processes'][0]['values']['gpu_frames']=2
        self.assertEqual(qc.compare(before,after)['total']['gpu_frames'],1)
        after['processes'][0]['values']['cpu_frames']=0
        with self.assertRaises(ValueError):qc.compare(before,after)
        after=copy.deepcopy(before);after['processes'][0]['os_start_identity']='new'
        with self.assertRaises(ValueError):qc.compare(before,after)

    def test_pair_requires_same_actual_cohort_and_retains_private_scope(self):
        spec=importlib.util.spec_from_file_location('qc_pair',Path(__file__).with_name('analyze-qc-pair.py'))
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        clip=dict(source='file:///private/var/db/sffx/media-highrate-v1/pattern-1080p60-h264.mp4',expected_frames=600,
            cpu_percent_one_core={'rdd':8},matched_family_cpu=100,qc_uploads={'total':{}},rvfc_ms={'median':40})
        value=dict(passed=True,launch=dict(release={'cohort':'same'},kernel_uuid='same'),identity=IDENTITY,
            installed_admission=False,providers=providers(),clips=[clip])
        self.assertTrue(module.compare(value,copy.deepcopy(value))['passed'])
        other=copy.deepcopy(value);other['launch']['release']['cohort']='different'
        self.assertFalse(module.compare(value,other)['passed'])
        other=copy.deepcopy(value);other['providers']['processes'][0]['images'][0]=other['providers']['processes'][0]['images'][0].replace('cc'*16,'dd'*16)
        self.assertFalse(module.compare(value,other)['passed'])
        other=copy.deepcopy(value);other['installed_admission']=True
        self.assertFalse(module.compare(value,other)['passed'])
        candidate=copy.deepcopy(value);candidate['identity']['cgl']['uuid']='dd'*16
        for process in candidate['providers']['processes']:
            process['images'][-1]=process['images'][-1].replace('bb'*16,'dd'*16)
        required=['same_exports','same_drv_inputs','env_only_postpatch_out','null_control_exact',
            'GLAPI_unchanged','GLU_unchanged','not_installed','paired_function_only_source_change']
        proof=dict(checks={k:True for k in required},baseline=value['identity']['cgl'],candidate=candidate['identity']['cgl'])
        self.assertFalse(module.compare(value,candidate)['passed'])
        self.assertTrue(module.compare(value,candidate,proof)['passed'])
        proof['checks']['paired_function_only_source_change']=False
        self.assertFalse(module.compare(value,candidate,proof)['passed'])
if __name__=='__main__':unittest.main()
