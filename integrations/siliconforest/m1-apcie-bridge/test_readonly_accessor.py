#!/usr/bin/env python3
"""Host fake-file qualification of read-only observer/accessor wiring."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SURVEY = HERE.parent / "m1-dart-survey"


class ReadonlyAccessorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("a host compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="apcie-readonly-wire-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-DAPB_HOST_TEST_ACCESSOR", "-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all", f"-I{HERE}", f"-I{SURVEY}"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def command(self, args, success=True):
        result = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def build(self, name, wire=None):
        binary = self.out / name
        self.command([self.cc, *self.flags, wire or HERE / "readonly_accessor.c",
                      HERE / "register_accessor.c", HERE / "observe.c", HERE / "bringup.c", HERE / "linkup.c",
                      HERE / "test_readonly_accessor.c", "-o", binary])
        return binary

    def test_fake_register_wiring_and_all_read_faults(self):
        result = self.command([self.build("wire")])
        self.assertIn("HOST ONLY PASS", result.stdout)

    def test_original_observer_suite_unchanged(self):
        binary = self.out / "original-observer"
        self.command([self.cc, *self.flags, HERE / "observe.c", HERE / "test_observe.c", "-o", binary])
        self.assertIn("HOST ONLY PASS", self.command([binary]).stdout)

    def test_negative_compile_guards(self):
        for name, flags, source, diagnostic in [
            ("default", [], "readonly_accessor.c", "host-test-only"),
            ("kernel", ["-DAPB_HOST_TEST_ACCESSOR", "-DKERNEL"], "readonly_accessor.c", "host-test-only"),
            ("shared", ["-DAPB_HOST_TEST_ACCESSOR", "-DAPB_FAKE_REGISTER_FILE"], "readonly_accessor.c", "host-test-only"),
            ("ndebug", ["-DAPB_HOST_TEST_ACCESSOR", "-DNDEBUG"], "test_readonly_accessor.c", "test_readonly_accessor requires assertions"),
        ]:
            with self.subTest(name=name):
                result = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", f"-I{SURVEY}", "-c",
                                       HERE / source, "-o", self.out / (name + ".o")], success=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)

    def test_compiled_wiring_mutants(self):
        self.command([self.build("mutation-baseline")])
        original = (HERE / "readonly_accessor.c").read_text()
        call = "return wire->port.read32(wire->port.context, port_region, offset, value);"
        mutations = {
            "wrong-port-id": ("case DLS_PORT: port_region = APB_PORT0;", "case DLS_PORT: port_region = APB_PHYIP;"),
            "missing-whitelist": ("!wire || !value || !apo_read_allowed(region, offset)", "!wire || !value"),
            "bypass-accessor": (call, "return wire->checked.fake.read32(wire->checked.fake.context, port_region, offset, value);"),
            "swallowed-read-failure": (call, "wire->port.read32(wire->port.context, port_region, offset, value); return APB_OK;"),
            "invented-observation": ("return apo_observe(board, &io, out);", "return APO_OBSERVED;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1)
                source = self.out / (name + ".c")
                source.write_text(original.replace(before, after))
                result = self.command([self.build(name, source)], success=False)
                self.assertNotEqual(result.returncode, 0, "mutant survived")
                self.assertIn("Assertion", result.stderr)

    def test_read_only_source_shape(self):
        code = re.sub(r"/\*.*?\*/", "", (HERE / "readonly_accessor.c").read_text(), flags=re.S)
        for forbidden in ("write32", "write16", "delay_us", "volatile", "IOKit", "ioremap", "mmap", "malloc"):
            self.assertNotIn(forbidden, code)
        self.assertEqual(code.count("wire->port.read32(wire->port.context, port_region, offset, value)"), 1)
        self.assertIn("return apo_observe(board, &io, out);", code)
        self.assertIn("struct dls_io io;", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
