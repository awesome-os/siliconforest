# SPDX-License-Identifier: MIT
"""Synthetic accounting controls; not target CPU measurements."""
import unittest
from process_cpu import compare,capture


def process(pid,kind,cpu,identity='start1',child=1):
    return dict(pid=pid,type=kind,cpuTime=cpu,os_start_identity=identity,childID=child,
                threads=[dict(tid=7,name='MediaPDecoder',cpuTime=cpu//2)])
def snap(time,*processes):return dict(begin_monotonic=time,end_monotonic=time+.1,processes=list(processes))
class Cases(unittest.TestCase):
    def test_capture_restores_content_when_parent_is_wrong(self):
        class Client:
            calls=[]
            def command(self,name,args):
                self.calls.append((name,args))
                return dict(parent=process(99,'browser',1e9),children=[])
        client=Client()
        with self.assertRaises(RuntimeError):capture(client,1,{1:'start'})
        self.assertEqual(client.calls[-1],('Marionette:SetContext',{'value':'content'}))
    def test_capture_records_start_identity_and_restores_content(self):
        class Client:
            calls=[]
            def command(self,name,args):
                self.calls.append((name,args))
                return dict(parent=process(1,'browser',1e9),children=[process(2,'rdd',1e9)])
        client=Client();result=capture(client,1,{1:'start',2:'rdd-start'})
        self.assertEqual(result['processes'][1]['os_start_identity'],'rdd-start')
        self.assertEqual(client.calls[-1],('Marionette:SetContext',{'value':'content'}))
    def test_rdd_is_separate_from_family_and_threads(self):
        result=compare(snap(0,process(1,'browser',1e9),process(2,'rdd',1e9)),snap(10,process(1,'browser',2e9),process(2,'rdd',4e9)))
        self.assertAlmostEqual(result['matched_family_percent_one_core'],40)
        self.assertAlmostEqual(result['percent_one_core_by_type']['rdd'],30)
        self.assertTrue(result['family_complete']);self.assertTrue(result['rdd_counter_present'])
        self.assertEqual(result['processes'][0]['threads'][0]['cpu_ns'],1500000000)
    def test_reused_pid_does_not_subtract_unrelated_counter(self):
        result=compare(snap(0,process(2,'rdd',1e9)),snap(10,process(2,'rdd',4e9,identity='new-start')))
        self.assertFalse(result['family_complete']);self.assertEqual(result['cpu_ns_by_type'],{})
        self.assertEqual(len(result['unmatched_new']),1);self.assertEqual(len(result['unmatched_gone']),1)
    def test_zero_counter_is_unavailable(self):
        result=compare(snap(0,process(2,'rdd',0)),snap(10,process(2,'rdd',0)))
        self.assertFalse(result['rdd_counter_present']);self.assertFalse(result['family_complete'])
    def test_zero_thread_counter_is_unavailable_not_idle(self):
        before=process(2,'rdd',1e9);after=process(2,'rdd',4e9)
        before['threads'][0]['cpuTime']=after['threads'][0]['cpuTime']=0
        row=compare(snap(0,before),snap(10,after))['processes'][0]
        self.assertEqual(row['threads'],[])
        self.assertEqual(len(row['thread_counters_unavailable']),1)
    def test_reset_cannot_prove_negative_cpu(self):
        result=compare(snap(0,process(2,'rdd',4e9)),snap(10,process(2,'rdd',1e9)))
        self.assertEqual(result['cpu_ns_by_type'],{});self.assertFalse(result['family_complete'])
    def test_missing_os_identity_cannot_be_counted(self):
        result=compare(snap(0,process(2,'rdd',1e9,identity=None)),snap(10,process(2,'rdd',4e9,identity=None)))
        self.assertFalse(result['family_complete']);self.assertFalse(result['rdd_counter_present'])
    def test_overlapping_snapshot_windows_refused(self):
        with self.assertRaises(ValueError):compare(snap(0),snap(.05))
if __name__=='__main__':unittest.main()
