# SPDX-License-Identifier: MIT
"""Real plan/main export controls in a temporary tree; no target or install."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

HERE=Path(__file__).resolve().parent
RELATIVE=Path('integrations/siliconforest/firefox/daily-launch.py')
SOURCE=HERE.parents[3]/RELATIVE
ANCHOR="if k.startswith(('DYLD_', 'SFFX_')) or k=='MOZ_CRASHREPORTER_DISABLE'"


class ExportTests(unittest.TestCase):
    def capture(self,environment=None,extra_manifest=None,arguments=None,patched=True):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);path=root/RELATIVE;path.parent.mkdir(parents=True)
            original=SOURCE.read_text();path.write_text(original)
            # FX integrated the clock flag independently. Apply the remaining
            # diagnostic-only hand-over to that current source in this private
            # tree; never assume every proposed hunk was merged together.
            if patched and 'SF_FIREFOX_MEDIA_TRACE_SECONDS' not in original:
                subprocess.run(['patch','--batch','-p1','-i',str(HERE/'audio-plan-export.patch')],cwd=root,
                               capture_output=True,text=True,check=True)
            m=types.ModuleType('private_daily_export');m.__file__=str(path)
            exec(compile(path.read_text(),str(path),'exec'),m.__dict__)
            manifest=dict(target=str(root),system_layout=True,private_fonts=True,gpu_presentation=True)
            manifest.update(extra_manifest or {})
            if manifest.get('audio_output_clock') is True:
                manifest['audio_override']=str(root/'overrides/audio-clock-v1/Frameworks')
            (root/'release.json').write_text(json.dumps(manifest))
            out=io.StringIO()
            with patch.dict(os.environ,environment or {},clear=True),patch.object(sys,'argv',['daily-launch.py','--sf-plan',*(arguments or [])]),contextlib.redirect_stdout(out):
                m.main()
            return json.loads(out.getvalue())['environment']

    def test_clock_enable_survives_actual_main_json_export(self):
        env=self.capture(extra_manifest=dict(audio_output_clock=True))
        self.assertEqual(env['SF_AU_NORMALIZE_HAL_CLOCK'],'1')
        self.assertEqual(env['MOZ_CRASHREPORTER_DISABLE'],'1')

    def test_false_clock_removes_inherited_enable_before_export(self):
        env=self.capture(dict(SF_AU_NORMALIZE_HAL_CLOCK='1'),dict(audio_output_clock=False))
        self.assertNotIn('SF_AU_NORMALIZE_HAL_CLOCK',env)

    def test_unobserved_diagnostic_flags_are_not_exported(self):
        flags=dict(SF_FIREFOX_MEDIA_TRACE='1',SF_HAL_PCM_TRACE='1',SF_HAL_PCM_AUDIT='1')
        self.assertFalse(flags.keys() & self.capture(flags).keys())

    def test_admitted_private_observer_keeps_enable_and_duration(self):
        flags=dict(SF_FIREFOX_MEDIA_TRACE='1',SF_FIREFOX_MEDIA_TRACE_SECONDS='50',
                   SF_HAL_PCM_TRACE='1',SF_HAL_PCM_AUDIT='1',SF_MEDIA_CADENCE_TRACE='1')
        env=self.capture(dict(flags,SFFX_DIAGNOSTIC_OBSERVER='/private/var/db/sffx/audio-test/meter.dylib'),
                         arguments=['--no-remote','--profile','/private/var/db/sffx/audio-test/profile'])
        for key,value in flags.items():self.assertEqual(env[key],value)

    def test_nonprivate_profile_cannot_export_observer(self):
        with self.assertRaises(ValueError):
            self.capture(dict(SFFX_DIAGNOSTIC_OBSERVER='/private/var/db/sffx/audio-test/meter.dylib'),
                         arguments=['--profile','/tmp/foreign-profile'])

    def test_old_main_reproduces_clock_export_gap(self):
        if ANCHOR not in SOURCE.read_text():self.skipTest('Owner already changed export; candidate controls use actual source')
        env=self.capture(extra_manifest=dict(audio_output_clock=True),patched=False)
        self.assertNotIn('SF_AU_NORMALIZE_HAL_CLOCK',env)


if __name__=='__main__':unittest.main()
