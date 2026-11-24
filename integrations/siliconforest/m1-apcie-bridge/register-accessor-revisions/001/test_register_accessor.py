#!/usr/bin/env python3
"""Host fake-register tests only; no device, mapping, kernel link or target."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class AccessorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("a host C compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="apcie-accessor-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-DAPB_HOST_TEST_ACCESSOR", "-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all", f"-I{HERE}"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_command(self, args, success=True):
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
        result = subprocess.run([str(x) for x in args], capture_output=True, text=True, env=env)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def build(self, name, accessor=None):
        binary = self.out / name
        self.run_command([self.cc, *self.flags, accessor or HERE / "register_accessor.c",
                          HERE / "bringup.c", HERE / "linkup.c",
                          HERE / "test_register_accessor.c", "-o", binary])
        return binary

    def test_fake_file_and_real_slice_calls(self):
        result = self.run_command([self.build("accessor")])
        self.assertIn("HOST ONLY PASS", result.stdout)

    def test_compile_refusals(self):
        for name, flags, source, diagnostic in [
            ("default", [], "register_accessor.c", "host-test-only"),
            ("kernel", ["-DAPB_HOST_TEST_ACCESSOR", "-DKERNEL"], "register_accessor.c", "host-test-only"),
            ("shared", ["-DAPB_HOST_TEST_ACCESSOR", "-DAPB_FAKE_REGISTER_FILE"],
             "register_accessor.c", "host-test-only"),
            ("assertions", ["-DAPB_HOST_TEST_ACCESSOR", "-DNDEBUG"],
             "test_register_accessor.c", "test_register_accessor requires assertions"),
        ]:
            with self.subTest(name=name):
                result = self.run_command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c",
                                           HERE / source, "-o", self.out / (name + ".o")], success=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(diagnostic, result.stderr)

    def test_slice_shared_writer_branches_stay_compiled_out(self):
        for source, predicate, following in [
            ("bringup.c", "apb_region_shared(region)", "if (!c->io->write32) {"),
            ("linkup.c", "apl_region_shared(region)", "if (!c->io->write32) {"),
        ]:
            result = self.run_command([self.cc, "-std=c11", "-DAPB_HOST_TEST_ACCESSOR", f"-I{HERE}",
                                       "-E", "-P", HERE / source])
            body = result.stdout.split("static int wr(", 1)[1]
            branch = body.split(predicate, 1)[1].split(following, 1)[0]
            self.assertNotIn("write32(", branch)
            self.assertIn("return APB_GATED", branch)
            if source == "linkup.c":
                pin = result.stdout.split("static int pin(", 1)[1].split("static int set_bits(", 1)[0]
                self.assertNotIn("fn(", pin)
                self.assertIn("return APB_GATED", pin)

    def test_assertions_kill_actual_accessor_mutants(self):
        self.run_command([self.build("mutation-baseline")])
        original = (HERE / "register_accessor.c").read_text()
        mutations = {
            "shared-write": ("if (apl_region_shared(region)) return APB_GATED;", "/* omitted */"),
            "alignment": ("!(offset & (width - 1u))", "1"),
            "end-bound": ("offset <= size - width", "offset <= size"),
            "read-output": ("if (!a->fake.read32 || a->fake.read32(a->fake.context, region, offset, &temporary))\n        return APB_IO;",
                            "if (!a->fake.read32 || a->fake.read32(a->fake.context, region, offset, &temporary)) {\n        *value = temporary; return APB_IO; }") ,
            "half-status": ("if (region == APB_ECAM0 && offset == 0xa0) return APB_ARGUMENT;", "/* omitted */"),
            "swallowed-write-error": (
                "if (!a->fake.write32 || a->fake.write32(a->fake.context, region, offset, value))\n        return APB_IO;",
                "if (!a->fake.write32 || a->fake.write32(a->fake.context, region, offset, value))\n        return APB_OK;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(original.count(before), 1)
                path = self.out / (name + ".c")
                path.write_text(original.replace(before, after))
                result = self.run_command([self.build(name, path)], success=False)
                self.assertNotEqual(result.returncode, 0, "mutant survived")
                self.assertIn("Assertion", result.stderr)

    def test_cxx_linkage(self):
        cxx = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(cxx, "a host C++ compiler is required")
        objects = []
        for file in ("register_accessor.c", "bringup.c", "linkup.c"):
            obj = self.out / (file + ".o")
            self.run_command([self.cc, *self.flags, "-c", HERE / file, "-o", obj])
            objects.append(obj)
        main = self.out / "main.cpp"
        main.write_text('#include "register_accessor.h"\n'
                        'int main() { return apr_bind_fake(nullptr, nullptr, nullptr) == APB_ARGUMENT ? 0 : 1; }\n')
        binary = self.out / "cxx"
        self.run_command([cxx, "-std=c++14", "-Wall", "-Wextra", "-Werror", "-DAPB_HOST_TEST_ACCESSOR",
                          "-fsanitize=address,undefined", f"-I{HERE}", main, *objects, "-o", binary])
        self.run_command([binary])


if __name__ == "__main__":
    unittest.main(verbosity=2)
