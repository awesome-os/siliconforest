# SPDX-License-Identifier: MIT
"""Synthetic host classifier tests; no native display or font claims."""
import json
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

from legibility_pixels import analyze


class TextPaintClassifier(unittest.TestCase):
    def fixture(self, root, ink=True, source='renderer', missing_last=False):
        width=80;height=40;visible=bytearray([255])*(width*height*3);hidden=bytes(visible);runs=[]
        for index in range(5):
            x=4+index*14
            runs.append(dict(text='web',font='12px test-font',boxes=[dict(x=x,y=8,width=10,height=12)]))
            if ink and not (missing_last and index==4):
                for y in range(11,17):
                    for dx in range(3,7):
                        at=(y*width+x+dx)*3;visible[at:at+3]=b'\0\0\0'
        for name,data in [('visible',visible),('hidden',hidden)]:
            p=root/(name+'.ppm');p.write_bytes(b'P6\n80 40\n255\n'+data);p.with_suffix('.source.json').write_text(json.dumps(dict(source=source)))
        row=dict(url='https://synthetic.invalid/',calibration=dict(complete=True,scale=1,origin=[0,0]),runs=runs,
                 renderer_images=dict(visible='visible.ppm',text_hidden='hidden.ppm'))
        (root/'legibility.json').write_text(json.dumps([row]))

    def test_paired_gate_rejects_capturing_the_first_site_twice(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name in ['on','off']:
                folder=root/name;folder.mkdir()
                rows=[{'url':'https://first.synthetic.invalid/'}]*2
                (folder/'legibility-text-pixel-coverage.json').write_text(json.dumps(rows))
            output=root/'comparison.json'
            result=subprocess.run([sys.executable,str(Path(__file__).with_name('compare-legibility.py')),
                '--on',str(root/'on'),'--off',str(root/'off'),'--output',str(output)],
                capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('Repeated actual URLs',result.stderr)
            self.assertFalse(output.exists())

    def test_empty_text_paint_fails(self):
        with tempfile.TemporaryDirectory() as d:
            self.fixture(Path(d),ink=False);r=analyze(Path(d))[0]
            self.assertFalse(r['paint_check_pass']);self.assertEqual(r['runs_with_ink'],0)

    def test_every_visible_run_with_ink_passes_paint_only(self):
        with tempfile.TemporaryDirectory() as d:
            self.fixture(Path(d));r=analyze(Path(d))[0]
            self.assertTrue(r['paint_check_pass']);self.assertEqual(r['runs_with_ink'],5)
            self.assertFalse(r['legibility_qualified']);self.assertTrue(r['visual_review_required'])

    def test_one_missing_run_cannot_hide_in_majority(self):
        with tempfile.TemporaryDirectory() as d:
            self.fixture(Path(d),missing_last=True);r=analyze(Path(d))[0]
            self.assertFalse(r['paint_check_pass']);self.assertEqual(r['runs_with_ink'],4)

    def test_missing_han_character_cannot_hide_in_a_painted_run(self):
        for middle_painted in [False, True]:
            with self.subTest(middle_painted=middle_painted), tempfile.TemporaryDirectory() as d:
                root=Path(d);self.fixture(root)
                pixels=bytearray([255])*(80*40*3)
                for index in [0, 1, 2] if middle_painted else [0, 2]:
                    for y in range(11,17):
                        for x in range(7+index*14,11+index*14):
                            at=(y*80+x)*3;pixels[at:at+3]=b'\0\0\0'
                (root/'visible.ppm').write_bytes(b'P6\n80 40\n255\n'+pixels)
                rows=json.loads((root/'legibility.json').read_text())
                rows[0]['han_sampling']=True
                rows[0]['runs']=[dict(text='日本語',font='synthetic',
                    boxes=[dict(x=4,y=8,width=42,height=12)],
                    han_samples=[dict(text=c,boxes=[dict(x=4+i*14,y=8,width=14,height=12)])
                                 for i,c in enumerate('日本語')])]
                (root/'legibility.json').write_text(json.dumps(rows))
                result=analyze(root)[0]
                self.assertTrue(result['ink_check_pass'])
                self.assertEqual(result['han_measured_characters'],3)
                self.assertEqual(result['han_characters_with_ink'],3 if middle_painted else 2)
                self.assertEqual(result['han_check_pass'],middle_painted)
                self.assertEqual(result['paint_check_pass'],middle_painted)

    def test_restored_text_must_recover_original_pixels(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);self.fixture(root)
            rows=json.loads((root/'legibility.json').read_text())
            rows[0]['renderer_images']['restored']='restored.ppm'
            (root/'legibility.json').write_text(json.dumps(rows))
            (root/'restored.source.json').write_text(json.dumps(dict(source='renderer')))
            visible=(root/'visible.ppm').read_bytes()
            (root/'restored.ppm').write_bytes(visible)
            self.assertTrue(analyze(root)[0]['restore_check_pass'])
            header,pixels=visible.split(b'255\n',1)
            (root/'restored.ppm').write_bytes(header+b'255\n'+bytes(128 if p==0 else p for p in pixels))
            result=analyze(root)[0]
            self.assertTrue(result['ink_check_pass'])
            self.assertFalse(result['restore_check_pass'])
            self.assertFalse(result['paint_check_pass'])

    def test_content_screenshot_cannot_qualify_renderer(self):
        with tempfile.TemporaryDirectory() as d:
            self.fixture(Path(d),source='content-readback')
            with self.assertRaisesRegex(ValueError,'Unverified'):analyze(Path(d))


if __name__=='__main__':unittest.main()
