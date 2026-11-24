# SPDX-License-Identifier: MIT
"""Refuse owner/aliased/live-or-failed-history profiles; no native launch here."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('media_profile',Path(__file__).with_name('media_profile_admission.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class AdmissionTests(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory();self.addCleanup(self.directory.cleanup)
        self.root=Path(self.directory.name)
        self.profile=self.root/'firefox-media-cadence-packet-owned/youtube1080-run/browser/profile'
        self.profile.mkdir(parents=True)
        self.prior=dict(command=['/Applications/Firefox.app/Contents/MacOS/firefox-sf','--no-remote','--profile',str(self.profile)],
            automation_complete=True,streaming=dict(complete=True),exit_before_cleanup=0,exit_after_cleanup=0,
            marionette=dict(capabilities={'moz:processID':123}))
        self.quit=dict(parent=123,requested=True,family_before=[123],family_remaining=[],cleanup_signals=[])
        self.save()

    def save(self):
        (self.profile.parent/'result.json').write_text(json.dumps(self.prior))
        (self.profile.parents[1]/'natural-quit.json').write_text(json.dumps(self.quit))

    def test_only_exact_completed_own_layout(self):
        self.assertEqual(m.admit(self.profile,self.root),self.profile)
        for path in [Path('/Users/owner/Library/Firefox/profile'),self.root/'other/profile',self.root/'firefox-media-cadence-packet-owned/youtube1080-run/browser/../profile']:
            with self.subTest(path=path),self.assertRaises(ValueError):m.admit(path,self.root)

    def test_failed_cleanup_or_unobserved_family_refused(self):
        for change in [{'exit_before_cleanup':None,'exit_after_cleanup':143},{'exit_before_cleanup':False},
                       {'automation_complete':False},{'streaming':{'complete':False}}]:
            prior=dict(self.prior);self.prior.update(change);self.save()
            with self.subTest(change=change),self.assertRaises(ValueError):m.admit(self.profile,self.root)
            self.prior=prior
        self.quit['family_remaining']=[123];self.save()
        with self.assertRaises(ValueError):m.admit(self.profile,self.root)

    def test_receipt_cannot_borrow_another_profiles_admission(self):
        self.prior['command'][-1]=str(self.root/'different/profile');self.save()
        with self.assertRaises(ValueError):m.admit(self.profile,self.root)

    def test_alias_refused(self):
        target=self.root/'other-result.json';target.write_text(json.dumps(self.prior))
        receipt=self.profile.parent/'result.json';receipt.unlink();receipt.symlink_to(target)
        with self.assertRaises(ValueError):m.admit(self.profile,self.root)


if __name__=='__main__':unittest.main()
