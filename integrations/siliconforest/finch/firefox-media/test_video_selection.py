# SPDX-License-Identifier: MIT
"""Selection/plan refusal controls, not native media or presentation proof."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import types
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('video_selection', HERE/'video_selection.py')
selection = importlib.util.module_from_spec(spec); spec.loader.exec_module(selection)
RELATIVE = Path('integrations/siliconforest/firefox/daily-launch.py')
SOURCE = HERE.parents[3]/RELATIVE


def manifest(root=selection.RUNTIME):
    return dict(target=str(root), system_layout=True, system_ui=True, system_gpu_providers=True,
                video_override=str(root/'overrides/video-shared-v1/Frameworks'), video_zero_copy=True,
                audio_override=str(root/'overrides/audio-clock-v1/Frameworks'), audio_output_clock=True,
                network_override=str(root/'overrides/network-v1/Frameworks'))


class SelectionTests(unittest.TestCase):
    def test_installed_family_requires_exact_cohort(self):
        self.assertEqual(selection.installed_family(manifest()),selection.FAMILY/'Frameworks')
        for key in manifest():
            row=manifest();row.pop(key)
            with self.subTest(key=key),self.assertRaises(ValueError):selection.installed_family(row)

    def test_only_explicit_clock_candidate_is_admitted(self):
        row=manifest();row['video_override']=str(selection.CLOCK_FAMILY/'Frameworks')
        self.assertEqual(selection.installed_family(row),selection.CLOCK_FAMILY/'Frameworks')
        for value in [str(selection.CLOCK_FAMILY/'Frameworks')+'/',
                      str(selection.RUNTIME/'overrides/video-clock-other/Frameworks'),
                      str(selection.CLOCK_FAMILY/'../video-shared-v1/Frameworks')]:
            row['video_override']=value
            with self.subTest(value=value),self.assertRaises(ValueError):selection.installed_family(row)
        for key,value in [('video_zero_copy',1),('audio_output_clock',1),
                          ('graphics_override','/private/another'),('diagnostics',{'args':['--marionette']})]:
            row=manifest();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):selection.installed_family(row)

    def candidate(self, directory):
        root=Path(directory);path=root/RELATIVE;path.parent.mkdir(parents=True)
        original=SOURCE.read_text();path.write_text(original)
        if "manifest.get('video_override')" not in original:
            subprocess.run(['patch','--batch','-p1','-i',str(HERE/'video-runtime-plan.patch')],
                           cwd=root,capture_output=True,text=True,check=True)
        module=types.ModuleType('candidate');module.__file__=str(path)
        exec(compile(path.read_text(),str(path),'exec'),module.__dict__)
        return module,root

    def test_plan_retains_graphics_network_audio_and_enables_best_effort(self):
        with tempfile.TemporaryDirectory() as directory,patch.dict(os.environ,{},clear=True):
            daily,root=self.candidate(directory);command,env=daily.plan(root,[],manifest(root))
            frames=env['DYLD_FRAMEWORK_PATH'].split(':')
            self.assertEqual(frames[:3],[str(root/'overrides'/name/'Frameworks')
                                       for name in ['video-shared-v1','audio-clock-v1','network-v1']])
            self.assertIn('/System/Library/Frameworks',frames)
            self.assertEqual(env['DYLD_LIBRARY_PATH'].split(':')[0],'/usr/local/lib/siliconforest/opengl/v1/lib')
            self.assertEqual(command[1],'/usr/libexec/iosurfaced')
            self.assertEqual(env['SF_AU_NORMALIZE_HAL_CLOCK'],'1')
            self.assertEqual(env['SF_VT_ZERO_COPY'],'1')

    def test_path_escape_and_mixed_graphics_refused(self):
        with tempfile.TemporaryDirectory() as directory,patch.dict(os.environ,{},clear=True):
            daily,root=self.candidate(directory)
            for value in ['/System/Library/Frameworks',str(root/'overrides/Frameworks'),
                          str(root/'overrides/video/../Frameworks'),str(root/'overrides/video')]:
                row=manifest(root);row['video_override']=value
                with self.subTest(value=value),self.assertRaises(ValueError):daily.plan(root,[],row)
            row=manifest(root);row['system_gpu_providers']=False
            with self.assertRaises(ValueError):daily.plan(root,[],row)

    def test_clock_flag_and_frame_modes_are_truthful(self):
        with tempfile.TemporaryDirectory() as directory,patch.dict(os.environ,{},clear=True):
            daily,root=self.candidate(directory)
            for value in [1,'true']:
                row=manifest(root);row['video_zero_copy']=value
                with self.subTest(value=value),self.assertRaises(ValueError):daily.plan(root,[],row)
            row=manifest(root);row.pop('video_override')
            with self.assertRaises(ValueError):daily.plan(root,[],row)
            for mode in ['0','1','require']:
                with patch.dict(os.environ,SF_VT_ZERO_COPY=mode):
                    self.assertEqual(daily.plan(root,[],manifest(root))[1]['SF_VT_ZERO_COPY'],mode)
            with patch.dict(os.environ,SF_VT_ZERO_COPY='unknown'),self.assertRaises(ValueError):
                daily.plan(root,[],manifest(root))
            row=manifest(root);row['video_zero_copy']=False
            with patch.dict(os.environ,SF_VT_ZERO_COPY='require'):
                self.assertNotIn('SF_VT_ZERO_COPY',daily.plan(root,[],row)[1])


if __name__=='__main__':unittest.main()
