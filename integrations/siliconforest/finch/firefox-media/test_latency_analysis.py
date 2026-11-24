# SPDX-License-Identifier: MIT
"""Synthetic clock arithmetic controls, no acoustic/runtime proof."""
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('latency_analysis',Path(__file__).with_name('analyze-latency.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
BASE='LATENCY_TIMEBASE numer=125 denom=3\nLATENCY_CASE case=0 client-rate=48000 device-rate=48000 device-latency=8192 stream-latency=0 unit-latency=0.010666666666666667\n'
DONE='LATENCY_DONE rows=1\n'
def row(hal=14800000,au=10000000,now=10000000,flags=3):
    return f'LATENCY_ROW case=0 now={now} hal-host={hal} au-host={au} hal-flags={flags} au-flags=3 cycle-callback=0\n'
class Cases(unittest.TestCase):
    def test_now_timestamp_misses_dynamic_lead(self):
        result=m.analyze(BASE+row()+DONE)['cases'][0]
        self.assertAlmostEqual(result['hal_lead_ms']['p50'],200)
        self.assertAlmostEqual(result['cubeb_model_ms']['p50'],181.33333333333333)
        self.assertAlmostEqual(result['model_minus_hal_ms']['p50'],-18.66666666666667)
    def test_naive_full_timestamp_would_double_count(self):
        result=m.analyze(BASE+row(au=14800000)+DONE)['cases'][0]
        self.assertAlmostEqual(result['model_minus_hal_ms']['p50'],181.33333333333333)
    def test_nonfuture_startup_cannot_be_measured_as_good(self):
        result=m.analyze(BASE+row(hal=0)+DONE)['cases'][0]
        self.assertEqual(result['future_samples'],0);self.assertIsNone(result['model_minus_hal_ms'])
    def test_missing_host_flag_refused(self):
        with self.assertRaises(ValueError):m.analyze(BASE+row(flags=1)+DONE)
    def test_aborted_probe_refused(self):
        with self.assertRaises(ValueError):m.analyze(BASE+row())
if __name__=='__main__':unittest.main()
