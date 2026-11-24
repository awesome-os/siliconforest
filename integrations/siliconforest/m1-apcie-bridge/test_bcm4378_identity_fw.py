#!/usr/bin/env python3
"""Host fake chip identification and firmware request planning only."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class IdentityFirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host C compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-identity-fw-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}"]
        cls.fake = ["-DBCI_FAKE_DEVICE", "-DAPB_HOST_TEST_ACCESSOR"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def command(self, args, success=True):
        r = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
        if success:
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        return r

    def build(self, name, flags=(), source=None):
        exe = self.out/name
        sources = [source or HERE/"bcm4378_identity_fw.c", HERE/"bringup.c", HERE/"linkup.c",
                   HERE/"perst_gpio.c", HERE/"radio_enable.c", HERE/"test_bcm4378_identity_fw.c"]
        if "-DBCI_FAKE_DEVICE" in flags:
            sources.append(HERE/"register_accessor.c")
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel_modes(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                self.assertIn("HOST ONLY PASS", self.command([self.build(name, flags)]).stdout)

    def test_negative_builds(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_identity_fw.c", "host-test only"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_identity_fw.c", "shared PHY and PMGR writes compiled out"),
            ("host-not-enabled", ["-DBCI_FAKE_DEVICE"], "bcm4378_identity_fw.c", "host-test-only"),
            ("ndebug", ["-DNDEBUG"], "test_bcm4378_identity_fw.c", "requires assertions"),
        ]:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c", HERE/file,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(diagnostic, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("baseline", self.fake)])
        src = (HERE/"bcm4378_identity_fw.c").read_text()
        mutations = {
            "revision-four": ("return revision < 32 && revision != 4;", "return revision < 32;"),
            "wrong-family": ("p->revision < 4 ?", "p->revision < 6 ?"),
            "wrong-pci": ("return pci == BCI_PCI_VENDOR_DEVICE ? BCI_FAKE_OK : BCI_IDENTITY;", "return BCI_FAKE_OK;"),
            "wrong-chip": ("if (raw == UINT32_MAX || out->chip_id != p->expected_id || out->bus_type != p->expected_type)", "if (0)"),
            "down-link": ("if ((link & 0x45) != 1)", "if (0)"),
            "optional-executable": ("out->required = item == BCI_EXECUTABLE;", "out->required = 0;"),
            "missing-success": ("out->required ? BCI_MISSING : BCI_OPTIONAL_MISSING", "BCI_FAKE_OK"),
            "allow-path": ("c == ',' || c == '-'", "c == '/' || c == ',' || c == '-'"),
            "output-clobber": ("struct bci_fw_plan next = {0};", "struct bci_fw_plan next = {0}; if (out) *out = next;"),
            "ignore-qualifiers": ("if (variant < p->qualifier_count)", "if (0)"),
            "nul-overrun": ("n + bytes >= capacity", "n + bytes > capacity"),
            "fabricate-numeric-proof": ("out->valid = 1; out->result = BCI_FAKE_OK;", "out->valid = 1; out->numeric_profile_verified = 1; out->result = BCI_FAKE_OK;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(src.count(before), 1)
                path = self.out/(name+".c"); path.write_text(src.replace(before, after))
                r = self.command([self.build(name, self.fake, path)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)

    def test_default_preprocessed_no_io_and_source_shape(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-E", "-P", HERE/"bcm4378_identity_fw.c"]).stdout
            body = pre.split("int bci_identify_fake(")[-1]
            for forbidden in ("io->", "request(", "write32(", "read32("):
                self.assertNotIn(forbidden, body)
            self.assertIn("return BCI_GATED;", body)
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_identity_fw.c").read_text(), flags=re.S)
        for forbidden in ("write32", "apg_", "ars_", "volatile", "mmap", "fopen", "open(", "download", "start_ep", "writemem"):
            self.assertNotIn(forbidden, code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
