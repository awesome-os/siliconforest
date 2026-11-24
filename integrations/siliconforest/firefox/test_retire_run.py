# SPDX-License-Identifier: MIT
"""Host-only archive/path policy checks. No native cleanup or SSH proof."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest


class RetireRunPolicy(unittest.TestCase):
    def setUp(self):
        self.script=Path(__file__).with_name('retire-owned-run.py').resolve()
        parent=self.script.parents[3]/'runs/firefox';parent.mkdir(parents=True,exist_ok=True)
        self.temporary=tempfile.TemporaryDirectory(prefix='retire-policy-',dir=parent)
        self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name)
        self.marker=self.root/'ssh-called'
        fake=self.root/'bin';fake.mkdir()
        ssh=fake/'ssh';ssh.write_text('#!/bin/sh\necho called > "$RETIRE_SSH_MARKER"\nexit 97\n');ssh.chmod(0o755)
        self.environment=dict(os.environ,PATH=str(fake)+':'+os.environ['PATH'],RETIRE_SSH_MARKER=str(self.marker))
        self.remote='/private/var/db/sffx/'+self.root.name
        self.metadata(self.remote)
        with tarfile.open(self.root/'target-evidence.tar.gz','w:gz')as archive:
            data=b'Synthetic host policy fixture; no native browser.\n'
            item=tarfile.TarInfo('./firefox.log');item.size=len(data)
            archive.addfile(item,io.BytesIO(data))

    def metadata(self,remote):
        (self.root/'launch-metadata.json').write_text(json.dumps({'command':['python3','remote-run.py','--run',remote]}))

    def plan(self):
        result=subprocess.run([sys.executable,str(self.script),'--run',str(self.root)],
                              capture_output=True,text=True,env=self.environment)
        self.assertFalse(self.marker.exists(),'A host plan must never invoke SSH')
        return result

    def test_complete_archive_plans_only_exact_owned_run(self):
        result=self.plan();self.assertEqual(result.returncode,0,result.stderr)
        plan=json.loads(result.stdout)
        self.assertEqual(plan['remote'],self.remote)
        self.assertTrue(plan['archive_fully_read']);self.assertFalse(plan['apply'])
        self.assertTrue(self.root.exists())

    def test_owner_profile_target_is_rejected_before_ssh(self):
        self.metadata('/var/root/Library/Application Support/Firefox/Profiles/siliconforest')
        result=self.plan();self.assertNotEqual(result.returncode,0)
        self.assertIn('do not match',result.stderr)

    def test_different_private_directory_is_rejected(self):
        self.metadata('/private/var/db/sffx/a-different-run')
        self.assertNotEqual(self.plan().returncode,0)

    def test_truncated_archive_cannot_authorize_removal(self):
        archive=self.root/'target-evidence.tar.gz'
        data=archive.read_bytes();archive.write_bytes(data[:len(data)//2])
        self.assertNotEqual(self.plan().returncode,0)


if __name__=='__main__':
    unittest.main()
