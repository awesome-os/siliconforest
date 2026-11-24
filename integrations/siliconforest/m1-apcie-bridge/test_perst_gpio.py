#!/usr/bin/env python3
"""PERST152 fake GPIO tests and no-drive default/kernel compile proofs."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class PerstGPIOTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("a host compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="apcie-perst-gpio-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def command(self, args, success=True):
        result = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def build(self, name, defines=(), source=None):
        binary = self.out / name
        self.command([self.cc, *self.flags, *defines, source or HERE / "perst_gpio.c",
                      HERE / "bringup.c", HERE / "linkup.c", HERE / "test_perst_gpio.c", "-o", binary])
        return binary

    def test_fake_default_and_kernel_modes(self):
        for name, flags in [("fake", ["-DAPG_FAKE_GPIO_FILE"]), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                self.assertIn("HOST ONLY PASS", self.command([self.build(name, flags)]).stdout)

    def test_forbidden_builds(self):
        for name, flags, source, diagnostic in [
            ("kernel-fake", ["-DKERNEL", "-DAPG_FAKE_GPIO_FILE"], "perst_gpio.c", "host-test only"),
            ("shared-fake", ["-DAPB_FAKE_REGISTER_FILE"], "perst_gpio.c", "shared PHY and PMGR writes compiled out"),
            ("ndebug", ["-DNDEBUG"], "test_perst_gpio.c", "test_perst_gpio requires assertions"),
        ]:
            with self.subTest(mode=name):
                result = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c", HERE / source,
                                       "-o", self.out / (name + ".o")], success=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)

    def test_kernel_has_no_gpio_callback_or_shared_writer_branch(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-E", "-P", HERE / "perst_gpio.c"]).stdout
            function = pre.split("int apg_set_perst(")[-1]
            self.assertNotIn("io->", function)
            self.assertIn("return APG_GATED;", function)
            drive = pre.split("int apg_fake_drive_compiled(void)")[-1].split("int apg_set_perst(")[0]
            self.assertIn("return 0;", drive)
        for file, predicate in (("bringup.c", "apb_region_shared(region)"), ("linkup.c", "apl_region_shared(region)")):
            pre = self.command([self.cc, "-std=c11", "-DKERNEL", f"-I{HERE}", "-E", "-P", HERE / file]).stdout
            branch = pre.split("static int wr(", 1)[1].split(predicate, 1)[1].split("if (!c->io->write32) {", 1)[0]
            self.assertNotIn("write32(", branch)
            self.assertIn("return APB_GATED;", branch)

    def test_assertion_killed_compiled_mutants(self):
        self.command([self.build("mutant-baseline", ["-DAPG_FAKE_GPIO_FILE"])])
        src = (HERE / "perst_gpio.c").read_text()
        mutants = {
            "polarity": ("(asserted ? 0u : data)", "(asserted ? data : 0u)"),
            "lock": ("if (before & lock)", "if (0)"),
            "mux-mode": ("if ((before & periph) || (before & mode) != output_mode)", "if (((before & periph) || (before & mode) != output_mode) && 0)"),
            "unrelated-bits": ("(before & ~data) | (asserted ? 0u : data)", "(asserted ? 0u : data)"),
            "lost-dirty": ("s->dirty = 1;", "s->dirty = 0;"),
            "readback": ("if ((after ^ wanted) & (data | lock | periph | mode))", "if (((after ^ wanted) & (data | lock | periph | mode)) && 0)"),
            "write-failure-success": ("if (io->write32(io->context, APG_PERST_OFFSET, wanted)) {\n            s->result = APG_IO; return APG_IO;",
                                      "if (io->write32(io->context, APG_PERST_OFFSET, wanted)) {\n            s->result = APG_OK; return APG_OK;"),
        }
        for name, (before, after) in mutants.items():
            with self.subTest(name=name):
                self.assertEqual(src.count(before), 1)
                path = self.out / (name + ".c")
                path.write_text(src.replace(before, after))
                result = self.command([self.build(name, ["-DAPG_FAKE_GPIO_FILE"], path)], success=False)
                self.assertNotEqual(result.returncode, 0, "mutant survived")
                self.assertIn("Assertion", result.stderr)

    def test_no_io_or_power_implementation(self):
        code = re.sub(r"/\*.*?\*/", "", (HERE / "perst_gpio.c").read_text(), flags=re.S)
        for forbidden in ("volatile", "IOKit", "mmap", "malloc", "pmgr", "phy", "delay_us", "power("):
            self.assertNotIn(forbidden, code)
        self.assertEqual(code.count("io->write32("), 1)
        self.assertEqual(code.count("io->read32("), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
