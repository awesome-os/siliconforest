# SPDX-License-Identifier: MIT
"""Prelaunch preference/override refusals; no installed browser or profile used."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('policy_guard',Path(__file__).with_name('codec_policy_guard.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.profile=Path(self.temp.name)/'profile';self.profile.mkdir()
        (self.profile/'user.js').write_text('user_pref("unrelated",true);\n')

    def test_default_and_opt_in_are_observed_without_test_assignment(self):
        self.assertTrue(m.inspect(self.profile,{},False)['passed'])
        self.assertEqual(m.inspect(self.profile,{'SFFX_AV1':'1'},True)['av1_environment'],'1')

    def test_old_prefs_or_test_key_refused(self):
        settings=self.profile/'user.js';original=settings.read_text()
        settings.write_text(original+'user_pref("media.av1.enabled",false);\n')
        with self.assertRaises(ValueError):m.inspect(self.profile,{},False)
        settings.write_text(original);(self.profile/'prefs.js').write_text('')
        with self.assertRaises(ValueError):m.inspect(self.profile,{},False)

    def test_inherited_override_cannot_masquerade_as_default(self):
        for env,override in [({'SFFX_AV1':'0'},False),({'SFFX_AV1':'1'},False),({},True)]:
            with self.subTest(env=env),self.assertRaises(ValueError):m.inspect(self.profile,env,override)

    def test_aliased_settings_refused(self):
        settings=self.profile/'user.js';settings.unlink();target=self.profile/'elsewhere';target.write_text('')
        settings.symlink_to(target)
        with self.assertRaises(ValueError):m.inspect(self.profile,{},False)


if __name__=='__main__':unittest.main()
