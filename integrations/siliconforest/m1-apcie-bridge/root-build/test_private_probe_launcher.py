"""Host-only fixed-launcher control paths; no real collector or target call."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "darwin-native-private-test"
COLLECTOR = "/usr/libexec/darwin-native-private-collector"
NONCE = "19" * 24


class Launcher(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.collector = self.root / "collector"
        self.calls = self.root / "calls.json"
        self.collector.write_text(
            "#!/usr/bin/python3\nimport sys,os,json\n"
            "from pathlib import Path\n"
            "Path(os.environ['FAKE_CALLS']).write_text(json.dumps(sys.argv[1:]))\n"
            "print('FAKE_COLLECTOR', flush=True)\n"
            "sys.exit(int(os.environ['FAKE_RC']))\n"
        )
        self.collector.chmod(0o700)
        raw = SOURCE.read_text()
        self.assertEqual(raw.count(COLLECTOR), 1)
        self.launcher = self.root / "launcher"
        self.launcher.write_text(raw.replace(COLLECTOR, str(self.collector)))

    def run_case(self, args, rc=0):
        return subprocess.run(
            ["/bin/sh", str(self.launcher), *args], capture_output=True, timeout=3,
            env=dict(os.environ, FAKE_CALLS=str(self.calls), FAKE_RC=str(rc)),
        )

    def test_status_is_from_completed_collector(self):
        for code in (0, 1, 2, 77, 125, 255):
            with self.subTest(code=code):
                result = self.run_case([NONCE], code)
                self.assertEqual(result.returncode, code)
                self.assertEqual(json.loads(self.calls.read_text()), [NONCE])
                self.assertEqual(result.stderr, b"")
                self.assertEqual(result.stdout.decode(),
                    f"DL32_PRIVATE_PROCESS_BEGIN {NONCE}\nFAKE_COLLECTOR\n"
                    f"DL32_PRIVATE_PROCESS_END {NONCE} collector_exit={code} self_report_only=true\n")

    def test_invalid_cli_does_not_invoke_collector(self):
        for args in ([], [NONCE, "extra"], [""], ["1" * 47], ["1" * 49],
                     ["A" * 48], [";" * 48], ["1" * 47 + "\n"]):
            with self.subTest(args=args):
                result = self.run_case(args)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")
                self.assertFalse(self.calls.exists())

    def test_missing_collector_reports_nonzero(self):
        self.collector.unlink()
        result = self.run_case([NONCE])
        self.assertEqual(result.returncode, 127)
        self.assertIn(f"collector_exit=127 self_report_only=true\n".encode(), result.stdout)
        self.assertFalse(self.calls.exists())

    def test_syntax(self):
        result = subprocess.run(["/bin/sh", "-n", str(SOURCE)], capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
