# SPDX-License-Identifier: MIT
"""Synthetic timestamp controls only; not native video or performance evidence."""
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('cadence',Path(__file__).with_name('analyze-cadence.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
HEADER='SFCADENCE_BEGIN pid=1 numer=125 denom=3 host=0 epoch=1000\n'
def event(n,host,kind='cv-callback',duration=24000):return f'SFCADENCE pid=1 sequence={n} event={kind} context=0 host={host} duration={duration} pts={n} scale=60 status=0 plane=0 token=0x1\n'
class Cases(unittest.TestCase):
    def test_actual_timebase_distinguishes60from25(self):
        for ticks,hz in [(400000,60),(960000,25)]:
            r=m.analyze(HEADER+''.join(event(i,i*ticks) for i in range(10)))
            self.assertAlmostEqual(r['summaries'][0]['observed_events_per_second'],hz)
            self.assertAlmostEqual(r['summaries'][0]['duration_ms']['p50'],1)
            self.assertFalse(r['presented_fps_proved'])
    def test_missing_hook_is_unobserved_not_zero(self):
        r=m.analyze(HEADER+event(0,0))
        self.assertIn('cgl-plane',r['unobserved_stages']);self.assertIsNone(r['summaries'][0]['interval_ms'])
    def test_epoch_window_selects_active_interval(self):
        r=m.analyze(HEADER+''.join(event(i,i*960000) for i in range(10)),1000.08,1000.2)
        self.assertEqual(r['summaries'][0]['records'],4)
    def test_missing_timebase_cannot_generate_rate(self):
        r=m.analyze(event(0,0));self.assertEqual(r['missing_timebase_pids'],[1]);self.assertEqual(r['summaries'],[])
    def test_overflow_is_preserved(self):
        self.assertTrue(m.analyze(HEADER+'SFCADENCE_END pid=1 lost=1\n')['overflow'])
    def test_duplicate_process_or_event_is_refused(self):
        for text in [HEADER+HEADER,HEADER+event(0,0)+event(0,0)]:
            with self.assertRaises(ValueError):m.analyze(text)
    def test_browser_serial_jumps_are_not_called_dropped_or_presented_fps(self):
        v=dict(source='own.mp4',timeOrigin=1000000,measureStart=0,measureEnd=400,
               presented=[dict(wall=i*40,presentedFrames=i*2) for i in range(10)])
        r=m.browser_intervals(HEADER+''.join(event(i,i*400000) for i in range(25)),
                              dict(video_measurements=[dict(video=v)]))
        c=r['clips'][0]
        self.assertEqual(c['callback_interval_ms']['p50'],40)
        self.assertEqual(c['reported_picture_serial_steps']['p50'],2)
        self.assertFalse(c['presented_fps_proved'])
        self.assertAlmostEqual(c['boundaries']['summaries'][0]['observed_events_per_second'],60)
    def test_incomplete_browser_interval_is_refused(self):
        v=dict(source='own.mp4',timeOrigin=1000000,measureStart=400,measureEnd=0)
        with self.assertRaises(ValueError):m.browser_intervals(HEADER,dict(video_measurements=[dict(video=v)]))
if __name__=='__main__':unittest.main()
