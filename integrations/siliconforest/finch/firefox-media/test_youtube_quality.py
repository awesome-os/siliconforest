# SPDX-License-Identifier: MIT
"""Quality admission refusals only; not a YouTube/network/native playback test."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('quality',Path(__file__).with_name('youtube_quality.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class QualityTests(unittest.TestCase):
    def test_label_does_not_substitute_for_real_dimensions(self):
        state=dict(videoId='own',ad=False,width=1280,height=720,readyState=4,error=None,quality='hd1080')
        self.assertFalse(m.admissible(state,'own'))
        state.update(width=1920,height=1080)
        self.assertTrue(m.admissible(state,'own'))
        for key,value in [('videoId','other'),('ad',True),('readyState',1),('error',3)]:
            with self.subTest(key=key):self.assertFalse(m.admissible(dict(state,**{key:value}),'own'))

    def test_ad_or_identity_change_is_retained_as_refusal(self):
        import json
        class Client:
            def __init__(self):self.calls=0
            def command(self,name,parameters):
                self.calls+=1
                return {'videoId':'own'} if self.calls==1 else {'videoId':'other','ad':False}
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'quality.json'
            with self.assertRaises(RuntimeError):m.prepare(Client(),'own',output)
            record=json.loads(output.read_text());self.assertFalse(record['complete'])
            self.assertIn('identity',record['error']);self.assertFalse(record['input_injected'])


if __name__=='__main__':unittest.main()
