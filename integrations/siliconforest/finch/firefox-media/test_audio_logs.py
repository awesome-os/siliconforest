# SPDX-License-Identifier: MIT
"""Collector scope/bounds controls on temporary files, no target evidence."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import audio_logs


class OwnLogs(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.run=Path(self.temp.name);self.home=self.run/'home'
        self.logs=self.home/'Library/Logs/SiliconForest/Firefox';self.logs.mkdir(parents=True)

    def test_collects_owned_stdout_bytes_and_receipt(self):
        (self.logs/'firefox-100-7.log').write_bytes(b'own native stdout\n')
        rows=audio_logs.collect(self.run,self.home)
        self.assertEqual(len(rows),1);self.assertEqual(rows[0]['bytes'],18)
        self.assertEqual((self.run/rows[0]['path']).read_bytes(),b'own native stdout\n')

    def test_other_home_is_refused(self):
        with self.assertRaises(ValueError):audio_logs.collect(self.run,self.run/'other-home')

    def test_symlinked_file_is_refused(self):
        (self.run/'outside').write_text('not a log')
        (self.logs/'firefox-100-7.log').symlink_to(self.run/'outside')
        with self.assertRaises(ValueError):audio_logs.collect(self.run,self.home)

    def test_symlinked_directory_escape_is_refused(self):
        self.logs.rmdir();self.logs.symlink_to(self.run)
        with self.assertRaises(ValueError):audio_logs.collect(self.run,self.home)

    def test_oversize_read_is_bounded_and_refused(self):
        (self.logs/'firefox-100-7.log').write_bytes(b'123456')
        with patch.object(audio_logs,'MAX_BYTES',5),self.assertRaises(ValueError):audio_logs.collect(self.run,self.home)


if __name__=='__main__':unittest.main()
