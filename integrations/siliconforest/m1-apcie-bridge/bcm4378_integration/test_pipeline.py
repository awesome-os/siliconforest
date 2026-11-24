#!/usr/bin/env python3
"""Run real original model implementations against one ordinary-RAM fixture."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
BRIDGE = HERE.parent
DART = BRIDGE.parent / "m1-dart"
SOURCES = [BRIDGE / (name + ".c") for name in (
    "bcm4378_transport", "bcm4378_identity_fw", "bcm4378_fake_dart",
    "bcm4378_fake_irq", "bcm4378_fake_ring", "bringup", "linkup",
    "perst_gpio", "radio_enable")]
SOURCES += [DART / "dart_codec.c", HERE / "test_pipeline.c"]
FAKE = ["-DBCL_TEST_HOST", "-DAPB_HOST_TEST_ACCESSOR", "-DBCT_FAKE_BAR_FILE",
        "-DBCI_FAKE_DEVICE", "-DBFD_FAKE_DEVICE", "-DBMI_FAKE_DEVICE", "-DBCR_FAKE_DEVICE"]


class PipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-composition-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                     f"-I{BRIDGE}", f"-I{DART}"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def invoke(self, argv, success=True):
        p = subprocess.run([str(x) for x in argv], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
        if success:
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        return p

    def build(self, name, flags, replacement=None):
        sources = [replacement if p == HERE / "test_pipeline.c" and replacement else p for p in SOURCES]
        if "-DBCL_TEST_HOST" in flags:
            sources += [BRIDGE / "register_accessor.c"]
        out = self.out / name
        self.invoke([self.cc, *self.flags, *flags, *sources, "-o", out])
        return out

    def test_real_five_model_composition(self):
        p = self.invoke([self.build("fake", FAKE)])
        self.assertIn("HOST ONLY PASS", p.stdout)
        self.assertRegex(p.stdout, r"callback boundaries=[1-9][0-9]* failure_cases=[1-9][0-9]*")
        print(p.stdout.strip())

    def test_default_and_kernel_no_io(self):
        for name, flags in (("default", []), ("kernel", ["-DKERNEL"])):
            with self.subTest(mode=name):
                p = self.invoke([self.build(name, flags)])
                self.assertIn("HOST ONLY PASS", p.stdout)

    def test_compile_refusals(self):
        for flags, diagnostic in ((["-DNDEBUG"], "require assertions"),
                                  ([*FAKE, "-DKERNEL"], "host-only")):
            p = self.invoke([self.cc, *self.flags, *flags, "-c", HERE / "test_pipeline.c",
                             "-o", self.out / "refused.o"], success=False)
            self.assertNotEqual(p.returncode, 0)
            self.assertIn(diagnostic, p.stderr)

    def test_composition_mutations(self):
        source = (HERE / "test_pipeline.c").read_text()
        # Change the TEST orchestrator, not predecessor source or its receipts.
        mutations = {
            "skip-preflight": ("STEP(preflight());", "if (0) STEP(preflight());"),
            "duplicate-owner": ("if (F.owner) return -902;", "if (0) return -902;"),
            "wrong-ring-label": ("4,8,DLD_APCIE_BASE};", "4,8,DLD_APCIE_BASE+4};"),
            "claim-clean-on-error": ("int e = prepare(); if (!e) e = finish();", "int e = prepare(); if (!e) e = finish(); if (e) F.complete = 1;"),
            "invent-consumer": ("*word(APR_BCM_BAR2, RI) = 0xbeef0001;", "*word(APR_BCM_BAR2, RI) = 0xbeef0000;"),
            "lost-owner": ("F.complete = 1; /* Logical close", "F.owner = 0; F.complete = 1; /* Logical close"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(mutation=name):
                self.assertEqual(source.count(before), 1)
                path = self.out / (name + ".c")
                path.write_text(source.replace(before, after))
                p = self.invoke([self.build(name, FAKE, path)], success=False)
                self.assertNotEqual(p.returncode, 0, "mutation survived")
                self.assertIn("Assertion", p.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
