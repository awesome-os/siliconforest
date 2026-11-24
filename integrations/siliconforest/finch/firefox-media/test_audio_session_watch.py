# SPDX-License-Identifier: MIT
"""Admission refusal controls; these synthetic logs do not prove target removal."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import audio_session_watch as watch


class OwnRing(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.run=Path(self.temp.name);self.home=self.run/'home'
        self.logs=self.home/'Library/Logs/SiliconForest/Firefox';self.logs.mkdir(parents=True)
        self.path=self.logs/'firefox-100-7.log'
        self.line='SFMEDIA_RING pid=100 read=1000 write=2048 running=1 rate=48000 period=512 capacity=2048 protocol=2 ring-address=0x123400000\n'

    def test_only_admitted_pid(self):
        self.path.write_text(self.line)
        self.assertEqual(watch.own_ring(self.run,self.home,100)['address'],0x123400000)
        self.assertIsNone(watch.own_ring(self.run,self.home,101))

    def test_missing_pointer_and_inactive_not_admitted(self):
        for row in [self.line.replace('ring-address=0x123400000',''),self.line.replace('running=1','running=0'),
                    self.line.replace('rate=48000','rate=44100'),self.line.replace('read=1000','read=0')]:
            self.path.write_text(row)
            self.assertIsNone(watch.own_ring(self.run,self.home,100))

    def test_ambiguous_active_objects_refused(self):
        self.path.write_text(self.line+self.line.replace('0x123400000','0x123500000'))
        with self.assertRaises(ValueError):watch.own_ring(self.run,self.home,100)

    def test_other_home_refused(self):
        with self.assertRaises(ValueError):watch.own_ring(self.run,self.run/'other',100)

    def test_symlink_escape_refused(self):
        self.path.symlink_to(self.run/'outside')
        with self.assertRaises(ValueError):watch.own_ring(self.run,self.home,100)

    def test_read_is_bounded(self):
        self.path.write_text(self.line)
        with patch.object(watch,'MAX_BYTES',5),self.assertRaises(ValueError):watch.own_ring(self.run,self.home,100)


if __name__=='__main__':unittest.main()
