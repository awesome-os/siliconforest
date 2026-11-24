#!/usr/bin/env python3
"""Host-only logical interrupt source tests, no target/AIC operations."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class FakeInterruptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host C compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-fake-irq-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}"]
        cls.fake = ["-DBMI_FAKE_DEVICE", "-DAPB_HOST_TEST_ACCESSOR"]

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
        sources = [source or HERE/"bcm4378_fake_irq.c", HERE/"bringup.c", HERE/"linkup.c",
                   HERE/"perst_gpio.c", HERE/"radio_enable.c", HERE/"test_bcm4378_fake_irq.c"]
        if "-DBMI_FAKE_DEVICE" in flags:
            sources.append(HERE/"register_accessor.c")
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel_modes(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                self.assertIn("HOST ONLY PASS", self.command([self.build(name, flags)]).stdout)

    def test_compile_refusals(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_fake_irq.c", "host-test only"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_fake_irq.c", "shared PHY and PMGR writes compiled out"),
            ("missing-optin", ["-DBMI_FAKE_DEVICE"], "bcm4378_fake_irq.c", "host-test-only"),
            ("ndebug", ["-DNDEBUG"], "test_bcm4378_fake_irq.c", "requires assertions"),
        ]:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c", HERE/file,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(diagnostic, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("baseline", self.fake)])
        src = (HERE/"bcm4378_fake_irq.c").read_text()
        mutations = {
            "skip-first-phy": ("if ((phy & 0x8fu) != 0x0fu)", "if (0)"),
            "ignore-link": ("if ((link & 0x45u) != 1)", "if (0)"),
            "ignore-pci": ("if (pci != 0x442514e4u)", "if (0)"),
            "wrong-route": ("s->profile.aic_base + selected", "s->profile.aic_base + selected + 1"),
            "ignore-mask": ("s->pending & s->allocated & ~s->masked", "s->pending & s->allocated"),
            "ack-all": ("s->pending &= ~(1u << selected);", "s->pending = 0;"),
            "lost-coalesced-event": ("s->pending |= bit;", "s->pending ^= bit;"),
            "truncate-msi-data": ("vector_bit(s, data, &bit)", "vector_bit(s, data & 7u, &bit)"),
            "premature-close": ("if (s->pending || s->masked != s->allocated)", "if (0)"),
            "miss-late-reset": ("return (phy & 0x8fu) == 0x0fu ? BMI_FAKE_OK : BMI_PHY_NOT_READY;", "return BMI_FAKE_OK;"),
            "bad-mask-bool": ("if (masked != 0 && masked != 1)", "if (0)"),
            "foreign-vectors": ("if (!allocated || (allocated & ~0xffu))", "if (!allocated)"),
            "empty-write": ("if (!eligible) return BMI_EMPTY;", "if (!eligible) { s->commits++; return BMI_EMPTY; }"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(src.count(before), 1)
                p = self.out/(name+".c"); p.write_text(src.replace(before, after))
                r = self.command([self.build(name, self.fake, p)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)
        p = self.out/"default-success.c"; p.write_text(src.replace("return BMI_GATED;", "return BMI_FAKE_OK;"))
        r = self.command([self.build("default-success", [], p)], success=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Assertion", r.stderr)

    def test_default_no_io_and_no_register_writer(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-E", "-P", HERE/"bcm4378_fake_irq.c"]).stdout
            body = pre.split("int bmi_fake_source_compiled(void) { return 0; }", 1)[1]
            for forbidden in ("io->", "read32(", "write32(", "s->", "apg_", "ars_"):
                self.assertNotIn(forbidden, body)
            self.assertEqual(body.count("return BMI_GATED;"), 6)
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_fake_irq.c").read_text(), flags=re.S)
        for forbidden in ("write32", "APB_PMGR", "volatile", "mmap", "IOKit", "apg_", "ars_", "pmgr_enable", "pcie_init"):
            self.assertNotIn(forbidden, code)
        self.assertEqual(code.count("io->read32("), 5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
