# SPDX-License-Identifier: MIT
"""Capture wrapper refusal/restoration controls only, never native pixels."""
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('own_cadence',Path(__file__).with_name('cadence-browser.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class RendererRouteTests(unittest.TestCase):
    def invoke(self,prior=False,source='renderer',error=None):
        state=[prior];calls=[]
        seat=types.SimpleNamespace(functional_qa_enabled=lambda:state[0],functional_qa=lambda x:state.__setitem__(0,x))
        def capture(path):
            self.assertTrue(state[0]);calls.append(str(path))
            if error:raise error
            return {'source':source,'path':str(path)}
        with patch.dict(sys.modules,renderer_capture=types.SimpleNamespace(capture=capture),presentation_seat=seat):
            try:return m.renderer_readback(Path('/owned/renderer.ppm')),calls
            finally:self.assertIs(state[0],prior)

    def test_capture_only_and_restore_owner_activity_scope(self):
        result,calls=self.invoke()
        self.assertEqual(calls,['/owned/renderer.ppm'])
        self.assertFalse(result['input_injected']);self.assertFalse(result['visible_playback_qualified'])
        self.invoke(prior=True)

    def test_cpu_scene_or_unverified_source_never_passes(self):
        for source in ['cpu-render','cpu-scene','unverified',None]:
            with self.subTest(source=source),self.assertRaises(RuntimeError):self.invoke(source=source)

    def test_exception_restores_previous_scope(self):
        for prior in [False,True]:
            with self.subTest(prior=prior),self.assertRaises(OSError):self.invoke(prior=prior,error=OSError('readback failed'))


if __name__=='__main__':unittest.main()
