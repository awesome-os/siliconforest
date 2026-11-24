# SPDX-License-Identifier: MIT
import unittest
import native_clock_bridge as c


class Bridge(unittest.TestCase):
    @staticmethod
    def observation(native,offset=1000,width=.2):
        return dict(kernel_uuid='synthetic-boot-kernel',boot_time='synthetic-start',clock_id=6,
            samples=[dict(epoch_before_ms=native+offset,epoch_after_ms=native+offset+width,
                          native_monotonic_ms=native)])

    def test_shared_brackets_intersect(self):
        r=c.summarize([dict(epoch_before_ms=1000,epoch_after_ms=1002,native_monotonic_ms=10),
                       dict(epoch_before_ms=1001,epoch_after_ms=1002,native_monotonic_ms=11)])
        self.assertEqual(r['epoch_minus_native_ms'],990.5);self.assertEqual(r['uncertainty_ms'],.5)

    def test_inconsistent_clocks_refused(self):
        with self.assertRaises(ValueError):c.summarize([dict(epoch_before_ms=1000,epoch_after_ms=1001,native_monotonic_ms=10),
                       dict(epoch_before_ms=1100,epoch_after_ms=1101,native_monotonic_ms=11)])

    def test_missing_backwards_nonfinite_refused(self):
        for rows in [[],[dict(epoch_before_ms=2,epoch_after_ms=1,native_monotonic_ms=1)],
                     [dict(epoch_before_ms=float('nan'),epoch_after_ms=2,native_monotonic_ms=1)]]:
            with self.assertRaises(ValueError):c.summarize(rows)

    def test_pair_preserves_drift_and_timing_bounds(self):
        bridge=c.paired(self.observation(10),self.observation(100,offset=1000.5))
        self.assertEqual(bridge['epoch_minus_native_bounds_ms'],[1000,1000.7])
        rows=[dict(sequence=4,surface=3,frame=0,before_ms=20,after_ms=20.1)]
        aligned=c.align_exports(rows,dict(timeOrigin=1000,measureStart=19,measureEnd=99),bridge)
        self.assertAlmostEqual(aligned['samples'][0]['after_js_measure_start_bounds_ms'][0],1)
        self.assertAlmostEqual(aligned['samples'][0]['after_js_measure_start_bounds_ms'][1],1.8)
        self.assertTrue(aligned['samples'][0]['inside_js_measurement'])

    def test_reboot_clock_change_backwards_and_excess_drift_refused(self):
        before=self.observation(10)
        for field,value in [('kernel_uuid','other'),('boot_time','other'),('clock_id',7)]:
            after=self.observation(100);after[field]=value
            with self.assertRaises(ValueError):c.paired(before,after)
        for after in [self.observation(9),self.observation(100,offset=1003)]:
            with self.assertRaises(ValueError):c.paired(before,after)

    def test_unbracketed_export_and_invalid_js_window_refused(self):
        bridge=c.paired(self.observation(10),self.observation(100))
        rows=[dict(sequence=0,surface=3,frame=0,before_ms=9,after_ms=10)]
        with self.assertRaises(ValueError):c.align_exports(rows,dict(timeOrigin=1000,measureStart=10,measureEnd=100),bridge)
        with self.assertRaises(ValueError):c.align_exports([],dict(timeOrigin=1000,measureStart=100,measureEnd=10),bridge)


if __name__=='__main__':unittest.main()
