#!/usr/bin/env python3
"""Host fake SMC GPIO13 command checks; no SMC transport or target."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class RadioEnableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("a host C compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="apcie-radio-enable-")
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

    def build(self, name, flags=(), source=None):
        binary = self.out / name
        self.command([self.cc, *self.flags, *flags, source or HERE / "radio_enable.c",
                      HERE / "bringup.c", HERE / "linkup.c", HERE / "test_radio_enable.c", "-o", binary])
        return binary

    def test_fake_default_kernel_modes(self):
        for name, flags in [("fake", ["-DARS_FAKE_GPIO_FILE"]), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                self.assertIn("HOST ONLY PASS", self.command([self.build(name, flags)]).stdout)

    def test_forbidden_builds(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", ["-DKERNEL", "-DARS_FAKE_GPIO_FILE"], "radio_enable.c", "host-test only"),
            ("shared-fake", ["-DARS_FAKE_GPIO_FILE", "-DAPB_FAKE_REGISTER_FILE"], "radio_enable.c", "shared PHY and PMGR writes compiled out"),
            ("ndebug", ["-DNDEBUG"], "test_radio_enable.c", "test_radio_enable requires assertions"),
        ]:
            with self.subTest(mode=name):
                result = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c", HERE / file,
                                       "-o", self.out / (name + ".o")], success=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)

    def test_kernel_default_has_no_callback(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-E", "-P", HERE / "radio_enable.c"]).stdout
            function = pre.split("int ars_enable_radio(")[-1]
            self.assertNotIn("io->", function)
            self.assertIn("return ARS_GATED;", function)
            drive = pre.split("int ars_fake_enable_compiled(void)")[-1].split("int ars_enable_radio(")[0]
            self.assertIn("return 0;", drive)
        for file, predicate in (("bringup.c", "apb_region_shared(region)"), ("linkup.c", "apl_region_shared(region)")):
            pre = self.command([self.cc, "-std=c11", "-DKERNEL", f"-I{HERE}", "-E", "-P", HERE / file]).stdout
            branch = pre.split("static int wr(", 1)[1].split(predicate, 1)[1].split("if (!c->io->write32) {", 1)[0]
            self.assertNotIn("write32(", branch)
            self.assertIn("return APB_GATED;", branch)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("mutation-baseline", ["-DARS_FAKE_GPIO_FILE"])])
        src = (HERE / "radio_enable.c").read_text()
        mutations = {
            "wrong-key": ("{'g', 'P', '0', 'd'}", "{'g', 'P', '0', 'c'}"),
            "wrong-endian": ("{1, 0, 0x80, 0}", "{0, 0x80, 0, 1}"),
            "disable-command": ("{1, 0, 0x80, 0}", "{0, 0, 0x80, 0}"),
            "missing-dirty": ("s->dirty = 1;", "s->dirty = 0;"),
            "fake-becomes-live-proof": ("s->command_accepted = 1;", "s->command_accepted = 1; s->state_known = 1;"),
            "swallow-failure": ("s->result = ARS_IO;\n            return ARS_IO;", "s->result = ARS_FAKE_ACCEPTED;\n            return ARS_FAKE_ACCEPTED;"),
            "duplicate-command": ("s->command_accepted = 1;", "io->write_key(io->context, key, value, 4); s->command_accepted = 1;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(src.count(before), 1)
                source = self.out / (name + ".c")
                source.write_text(src.replace(before, after))
                result = self.command([self.build(name, ["-DARS_FAKE_GPIO_FILE"], source)], success=False)
                self.assertNotEqual(result.returncode, 0, "mutant survived")
                self.assertIn("Assertion", result.stderr)

    def test_no_transport_or_hardware_implementation(self):
        code = re.sub(r"/\*.*?\*/", "", (HERE / "radio_enable.c").read_text(), flags=re.S)
        for forbidden in ("volatile", "IOKit", "mmap", "malloc", "delay_us", "read32", "write32", "start_ep", "writemem", "gP1a"):
            self.assertNotIn(forbidden, code)
        self.assertEqual(code.count("io->write_key("), 1)
        self.assertNotIn("s->state_known = 1", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
