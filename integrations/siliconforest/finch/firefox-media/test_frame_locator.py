# SPDX-License-Identifier: MIT
"""Synthetic image/receipt refusal controls, not target capture evidence."""
import importlib.util
from pathlib import Path
import unittest
from PIL import Image,ImageDraw
import frame_marker
spec=importlib.util.spec_from_file_location('frame_locator',Path(__file__).with_name('locate-frame-marker.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def image(origins):
    result=Image.new('RGB',(600,400),(100,110,120));draw=ImageDraw.Draw(result)
    for x,y in origins:
        for row in range(2):
            for c,bit in enumerate(frame_marker.bits(123)):
                left=x+24+c*24;top=y+20+row*28
                color=224 if bit!=row else 32
                draw.rectangle((left,top,left+19,top+19),fill=(color,)*3)
    return result


class Locator(unittest.TestCase):
    def setUp(self):
        self.calibration=dict(renderer_capture=dict(source='renderer',input_injected=False),
                              rect=dict(width=480),devicePixelRatio=1)

    def test_unique_scaled_marker(self):
        result=m.locate(image([(30,70)]),self.calibration,123)
        self.assertEqual(result['frame_id'],123)
        self.assertLess(abs(result['video_screen_rect_estimate']['x']-30),2)
        self.assertLess(abs(result['video_screen_rect_estimate']['y']-70),2)
        self.assertFalse(result['panel_presentation_proved'])

    def test_cpu_capture_receipt_refused(self):
        self.calibration['renderer_capture']['source']='cpu-scene'
        with self.assertRaises(ValueError):m.locate(image([(30,70)]),self.calibration,123)

    def test_wrong_frame_refused(self):
        with self.assertRaises(ValueError):m.locate(image([(30,70)]),self.calibration,124)

    def test_spatially_ambiguous_markers_refused(self):
        with self.assertRaises(ValueError):m.locate(image([(30,30),(30,230)]),self.calibration,123)


if __name__=='__main__':unittest.main()
