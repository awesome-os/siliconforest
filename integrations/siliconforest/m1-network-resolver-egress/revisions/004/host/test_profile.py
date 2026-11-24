"""Offline profile boundary checks; invalid setup creates no QEMU or records."""
from pathlib import Path
import socket
import sys
import tempfile
import unittest
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import backend

class Profile(unittest.TestCase):
    def test_exact_profile_and_finite_constants(self):
        self.assertEqual(backend.PROFILE,'native-resolver-example-http-drain-v2')
        self.assertEqual(backend.POLICY_REVISION,2)
        self.assertEqual(backend.BACKEND_POLICY_REVISION,4)
        self.assertEqual(backend.ACTIVE_IDLE_SECONDS,30)
        self.assertEqual(backend.CLOSE_EOF_SECONDS,40)
        self.assertEqual(backend.CODEC_SHA256,'698a007fb9dbc63fd5eabac46163f7a7ce6a7ff3056a37ed62185f8e9a6d2885')
    def test_lifetime_and_nonce_rejected_before_path_access(self):
        for nonce,seconds,accept in [('a'*48,121,300),('a'*48,0,300),
                ('a'*48,90,301),('a'*48,90,0),('x'*48,90,300)]:
            with self.assertRaisesRegex(ValueError,'nonce/lifetime'):
                backend.Backend('/does/not/exist',backend.CODEC_SHA256,nonce,
                    '/does/not/exist/records',seconds=seconds,accept_seconds=accept)
    def test_supplied_codec_digest_cannot_replace_frozen_library(self):
        with tempfile.TemporaryDirectory() as d:
            records=Path(d)/'records'
            with self.assertRaisesRegex(ValueError,'codec library digest'):
                backend.Backend('/does/not/exist','0'*64,'a'*48,records)
            self.assertFalse(records.exists())
    def test_owned_socket_flags_are_checked(self):
        a,b=socket.socketpair()
        try:
            self.assertIs(backend.checked_socket(a),a)
            self.assertFalse(a.getblocking()); self.assertFalse(a.get_inheritable())
        finally:a.close();b.close()

    def test_packet_policy_remains_frozen003_byte_identical(self):
        import hashlib
        self.assertEqual(hashlib.sha256(Path(__file__).with_name('policy.py').read_bytes()).hexdigest(),
            '20a0b6d5dbdff17b748f241623fc09989dd83b46957ab68862ae8799fecc6d8b')
    def test_old_profile_is_refused_by_CLI_before_backend_creation(self):
        import subprocess
        argv=[sys.executable,'-B','-I',str(Path(__file__).with_name('backend.py')),
            '--profile','native-resolver-example-http-v1','--nonce','a'*48,
            '--codec-library','/unused/library','--codec-sha256',backend.CODEC_SHA256,
            '--records','/unused/records','--listen-port','0','--seconds','120','--accept-seconds','1']
        result=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=3)
        self.assertEqual(result.returncode,2);self.assertIn(b'invalid choice',result.stderr)

if __name__=='__main__':unittest.main()
