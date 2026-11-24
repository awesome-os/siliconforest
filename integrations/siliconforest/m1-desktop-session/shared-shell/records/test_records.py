#!/usr/bin/env python3
"""Host file tests for the desktop record exporter. No Darwin or device access."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from decode_records import decode

HERE = Path(__file__).resolve().parent
BASE = HERE.parents[2] / "m1-userspace-diagnostics" / "terminal-records"
NONCE = "0123456789abcdef" * 3
SESSION = {
    "session-result.txt": b"result\r\n\x00",
    "manager-status.log": b"",
}
APPLICATION = {
    "application-stdout.log": b"out",
    "application-stderr.log": bytes(range(80)),
}


class RecordsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="desktop-records-host-")
        cls.binary = Path(cls.build.name) / "darwin-desktop-records"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                        "-O1", str(HERE / "desktop-records.c"), "-lcrypto", "-o", str(cls.binary)],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="desktop-records-fixture-")
        self.addCleanup(self.temp.cleanup)

    def make(self, files):
        directory = Path(tempfile.mkdtemp(prefix="diag-", dir=self.temp.name))
        directory.chmod(0o700)
        directory.chmod(0o700)
        for name, data in files.items():
            path = directory / name
            path.write_bytes(data)
            path.chmod(0o600)
        return directory

    def export(self, directory, kind, phase="closed"):
        return subprocess.run([str(self.binary), "--directory", str(directory), "--nonce", NONCE,
                               "--kind", kind, "--phase", phase],
                              capture_output=True, timeout=2)

    def test_provenance_base_is_unchanged(self):
        recorded = json.loads((HERE / "PROVENANCE.json").read_text())["base_sha256"]
        for name, expected in recorded.items():
            actual = hashlib.sha256((BASE / name).read_bytes()).hexdigest()
            self.assertEqual(actual, expected)

    def test_session_and_application_roundtrip_both_phases(self):
        for kind, files in (("session", SESSION), ("application", APPLICATION)):
            directory = self.make(files)
            for phase in ("live", "closed"):
                result = self.export(directory, kind, phase)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(result.stderr, b"")
                decoded = decode(result.stdout, NONCE, kind, phase)
                self.assertEqual(set(decoded), set(files))
                for name, data in files.items():
                    self.assertEqual(decoded[name]["data"], data)
                    self.assertEqual(decoded[name]["file_bytes_after"], len(data))
                self.assertNotIn(b"DL22_RECORD_MISSING", result.stdout)
                self.assertNotIn(b"DL22_RECORD_REFUSED", result.stdout)

    def test_foreign_kind_noise_and_other_prefix_are_not_stripped(self):
        raw = self.export(self.make(SESSION), "session").stdout
        with self.assertRaises(ValueError):
            decode(raw, NONCE, "application", "closed")
        swapped = raw.replace(b" session ", b" application ", 1)
        with self.assertRaises(ValueError):
            decode(swapped, NONCE, "session", "closed")
        for prefix in (b"note\n", b"DL11_RECORD_BEGIN\n", b"DL19_RECORD_BEGIN\n"):
            with self.subTest(prefix=prefix), self.assertRaises(ValueError):
                decode(prefix + raw, NONCE, "session", "closed")

    def test_corruption_duplicate_truncation_and_phase(self):
        raw = self.export(self.make(APPLICATION), "application").stdout
        self.assertEqual(decode(raw, NONCE, "application", "live")["application-stderr.log"]["data"],
                         bytes(range(80)))
        grown = raw.replace(b"file_bytes_after=80 ", b"file_bytes_after=81 ", 1)
        self.assertEqual(decode(grown, NONCE, "application", "live")["application-stderr.log"]["file_bytes_after"], 81)
        with self.assertRaises(ValueError):
            decode(grown, NONCE, "application", "closed")
        mutations = [raw.replace(b"index=0 ", b"index=1 ", 1),
                     raw.replace(b"00010203", b"00010204", 1),
                     raw.replace(b"chunks=2 ", b"chunks=1 ", 1),
                     raw.replace(b"chunk_bytes=64", b"chunk_bytes=63", 1),
                     raw.replace(b"DL22_RECORD_END", b"DL22_RECORD_LOST", 1),
                     raw.replace(b"DL22_RECORD_HEX", b"DL22_RECORD_LOST", 1),
                     raw + raw, raw[:-40], raw.replace(NONCE.encode(), b"a" * 48, 1)]
        for mutant in mutations:
            with self.subTest(mutant=mutant[:70]), self.assertRaises(ValueError):
                decode(mutant, NONCE, "application", "closed")
        with self.assertRaises(ValueError):
            decode(raw, NONCE, "widget", "closed")
        with self.assertRaises(ValueError):
            decode(raw, NONCE, "application", "later")

    def test_caps_missing_extra_and_link_modes(self):
        session = self.make({"session-result.txt": b"a" * 16384, "manager-status.log": b"a" * 65536})
        self.assertEqual(self.export(session, "session").returncode, 0)
        (session / "session-result.txt").write_bytes(b"a" * 16385)
        (session / "session-result.txt").chmod(0o600)
        refused = self.export(session, "session")
        self.assertEqual(refused.returncode, 1)
        self.assertEqual(refused.stderr, b"")
        with self.assertRaises(ValueError):
            decode(refused.stdout, NONCE, "session", "closed")
        (session / "session-result.txt").write_bytes(b"ok")
        (session / "session-result.txt").chmod(0o600)
        (session / "manager-status.log").write_bytes(b"a" * 65537)
        (session / "manager-status.log").chmod(0o600)
        self.assertEqual(self.export(session, "session").returncode, 1)
        application = self.make({"application-stdout.log": b"a" * 8192,
                                 "application-stderr.log": b"err"})
        self.assertEqual(self.export(application, "application").returncode, 0)
        (application / "application-stdout.log").write_bytes(b"a" * 8193)
        (application / "application-stdout.log").chmod(0o600)
        self.assertEqual(self.export(application, "application").returncode, 1)
        (application / "application-stdout.log").write_bytes(b"out")
        (application / "application-stdout.log").chmod(0o600)
        extra = application / "notes.txt"
        extra.write_bytes(b"ignored")
        extra.chmod(0o600)
        clean = self.export(application, "application")
        self.assertEqual(clean.returncode, 0)
        self.assertNotIn(b"notes.txt", clean.stdout)
        (application / "application-stderr.log").unlink()
        missing = self.export(application, "application")
        self.assertEqual(missing.returncode, 1)
        with self.assertRaises(ValueError):
            decode(missing.stdout, NONCE, "application", "closed")
        (application / "application-stderr.log").symlink_to(application / "application-stdout.log")
        self.assertEqual(self.export(application, "application").returncode, 1)
        (application / "application-stderr.log").unlink()
        (application / "application-stderr.log").write_bytes(b"err")
        (application / "application-stderr.log").chmod(0o600)
        os.link(application / "application-stdout.log", application / "alias")
        self.assertEqual(self.export(application, "application").returncode, 1)
        (application / "alias").unlink()
        self.assertEqual(self.export(application, "application").returncode, 0)
        for mode in (0o644, 0o400):
            (application / "application-stdout.log").chmod(mode)
            self.assertEqual(self.export(application, "application").returncode, 1, mode)
        (application / "application-stdout.log").chmod(0o600)

    def test_nonregular_directory_and_malformed_cli(self):
        directory = self.make(APPLICATION)
        target = directory / "application-stdout.log"
        target.unlink()
        os.mkfifo(target, 0o600)
        self.assertEqual(self.export(directory, "application").returncode, 1)
        target.unlink()
        target.mkdir(mode=0o700)
        self.assertEqual(self.export(directory, "application").returncode, 1)
        target.rmdir()
        target.write_bytes(b"out")
        target.chmod(0o600)
        directory.chmod(0o755)
        self.assertEqual(self.export(directory, "application").returncode, 1)
        directory.chmod(0o700)
        link = directory.parent / "link"
        link.symlink_to(directory, target_is_directory=True)
        self.assertEqual(self.export(link, "application").returncode, 1)
        relative = self.export(Path(directory.name), "application")
        self.assertEqual(relative.returncode, 2)
        self.assertEqual(relative.stdout, b"")
        dotted = self.export(Path(str(directory) + "/../" + directory.name), "application")
        self.assertEqual(dotted.returncode, 2)
        missing_dir = self.export(directory.parent / "absent-diagnostics", "application")
        self.assertEqual(missing_dir.returncode, 1)
        cases = [
            [str(self.binary), "--directory", str(directory), "--nonce", "a", "--kind", "session", "--phase", "live"],
            [str(self.binary), "--directory", str(directory), "--nonce", "A" * 48, "--kind", "session", "--phase", "live"],
            [str(self.binary), "--directory", str(directory), "--nonce", NONCE, "--kind", "widget", "--phase", "live"],
            [str(self.binary), "--directory", str(directory), "--nonce", NONCE, "--kind", "session", "--phase", "later"],
            [str(self.binary), "--nonce", NONCE, "--directory", str(directory), "--kind", "session", "--phase", "live"],
            [str(self.binary), "--directory", str(directory), "--nonce", NONCE, "--phase", "live"],
        ]
        for argv in cases:
            result = subprocess.run(argv, capture_output=True, timeout=2)
            self.assertEqual(result.returncode, 2, argv)
            self.assertEqual(result.stdout, b"")
            self.assertEqual(result.stderr, b"")


if __name__ == "__main__":
    unittest.main()
