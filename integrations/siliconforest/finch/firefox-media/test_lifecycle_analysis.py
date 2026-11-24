# SPDX-License-Identifier: MIT
"""Synthetic classifier controls; no hardware, media or browser evidence."""
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('analysis',Path(__file__).with_name('analyze-lifecycle.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
def state(event,time,active=0,status=0,unit='0x1'):
    return f'SFMEDIA_AU phase=state pid=1 device=272 unit={unit} generation=1 tracked=1 event={event} status={status} active={active} monotonic={time}\n'
def ring(time,cursor=100,running=0,active=0):
    return f'SFMEDIA_RING pid=1 device=272 protocol=2 monotonic={time} read={cursor} running={running} active-tracked-units={active}\n'
def callback(time,calls=10,successes=None,errors=0):
    if successes is None:successes=calls-errors
    return f'SFMEDIA_CALLBACK pid=1 unit=0x1 monotonic={time} calls={calls} successes={successes} errors={errors}\n'
BASE=state('start',0,1)+state('stop',1)+state('dispose',1.01)
class Cases(unittest.TestCase):
    def classification(self,text):return module.analyze(text)['results'][0]['classification']
    def test_stable_own_ring_and_callbacks(self):
        self.assertEqual(self.classification(BASE+ring(1.2)+ring(1.6)+callback(1.2)+callback(1.6)),'QUIESCENT_OWN_DEVICE_RING_AND_CALLBACKS')
    def test_missing_ring_cannot_pass(self):
        self.assertEqual(self.classification(BASE),'INSUFFICIENT_POST_TEARDOWN_RING_EVIDENCE')
    def test_missing_callbacks_cannot_prove_full_quiescence(self):
        self.assertEqual(self.classification(BASE+ring(1.2)+ring(1.6)),'HAL_RING_QUIET_CALLBACKS_UNOBSERVED')
    def test_cursor_advance_is_activity_not_sound(self):
        result=module.analyze(BASE+ring(1.2)+ring(1.6,200))
        self.assertIn('HAL_ACTIVITY_CONTINUES',result['results'][0]['classification'])
        self.assertFalse(result['browser_document_or_acoustic_stop_proved'])
    def test_failed_stop_does_not_clear_active(self):
        self.assertIn('TRACKED_STREAM_REMAINS_ACTIVE',self.classification(state('start',0,1)+state('stop',1,1,-5)))
    def test_other_unit_active_is_not_failed_tab_teardown(self):
        self.assertIn('TRACKED_STREAM_REMAINS_ACTIVE',self.classification(BASE+state('start',1.1,1,unit='0x2')))
    def test_callback_activity_not_hidden_by_quiet_ring(self):
        self.assertEqual(self.classification(BASE+ring(1.2)+ring(1.6)+callback(1.2)+callback(1.6,20)),'CALLBACK_ACTIVITY_CONTINUES_HAL_RING_QUIET')
    def test_capacity_exhaustion_prevents_quiet_verdict(self):
        self.assertEqual(self.classification(BASE+'SFMEDIA_CALLBACK pid=1 tracking-capacity-exhausted=1\n'),'INCOMPLETE_TRACKING')
    def test_inflight_callback_cannot_pass(self):
        self.assertEqual(self.classification(BASE+ring(1.2)+ring(1.6)+callback(1.2,successes=9)+callback(1.6,successes=9)),'HAL_RING_QUIET_CALLBACK_COMPLETION_UNRESOLVED')
    def test_callback_finishing_without_new_call_is_activity(self):
        self.assertEqual(self.classification(BASE+ring(1.2)+ring(1.6)+callback(1.2,successes=9)+callback(1.6)),'CALLBACK_ACTIVITY_CONTINUES_HAL_RING_QUIET')
    def test_missing_completion_counters_cannot_pass(self):
        rows='SFMEDIA_CALLBACK pid=1 unit=0x1 monotonic=1.2 calls=10\nSFMEDIA_CALLBACK pid=1 unit=0x1 monotonic=1.6 calls=10\n'
        self.assertEqual(self.classification(BASE+ring(1.2)+ring(1.6)+rows),'HAL_RING_QUIET_CALLBACKS_UNOBSERVED')
    def test_reused_unit_on_another_device_is_ambiguous(self):
        different=state('start',2,1).replace('device=272','device=273')
        result=module.analyze(BASE+ring(1.2)+ring(1.6)+callback(1.2)+callback(1.6)+different)
        self.assertTrue(all(r['classification']=='AMBIGUOUS_UNIT_ADDRESS_DEVICE_REUSE' for r in result['results']))
    def test_completed_errors_are_quiescent_not_successful_audio(self):
        self.assertEqual(self.classification(BASE+ring(1.2)+ring(1.6)+callback(1.2,errors=2)+callback(1.6,errors=2)),'QUIESCENT_OWN_DEVICE_RING_AND_CALLBACKS')
    def test_no_records_does_not_pass(self):self.assertEqual(module.analyze('')['results'],[])

if __name__=='__main__':unittest.main()
