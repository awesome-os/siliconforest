# SPDX-License-Identifier: MIT
import unittest
from frame_probe_admission import native_zero
from test_frame_exports import row


class NativeCalibration(unittest.TestCase):
    def setUp(self):self.geometry=[1920,1472,24,208,1440,810,1]
    def test_own_actual_raw_zero(self):
        self.assertTrue(native_zero(row(0,0,0),100,self.geometry)['admitted_from_completed_owned_pixels'])
    def test_deferred_preview_requires_explicit_protocol(self):
        text=row(0,0,0).replace('FXFRAMEID ','FXFRAMEID_PREVIEW ')
        self.assertIsNone(native_zero(text,100,self.geometry))
        self.assertTrue(native_zero(text.replace('readback_bytes=','log_buffered=1 readback_bytes='),100,self.geometry))
    def test_other_pid_nonzero_or_dimensions_cannot_admit(self):
        for text in [row(0,0,0,pid=101),row(0,1,0),row(0,0,0).replace('1920x1472','1920x1440')]:
            self.assertIsNone(native_zero(text,100,self.geometry))
    def test_contradictory_raw_zero_refused(self):
        self.assertIsNone(native_zero(row(0,0,0).replace('readback_bytes=10368','readback_bytes=4'),100,self.geometry))
        self.assertIsNone(native_zero(row(0,1,0).replace('frame=1','frame=0'),100,self.geometry))


if __name__=='__main__':unittest.main()
