#!/usr/bin/env python3
"""Host fake page-map tests. No hardware survey, register init or boot."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
CODEC = HERE.parent/"m1-dart"


class FakeDARTTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host C compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-fake-dart-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}", f"-I{CODEC}"]
        cls.fake = ["-DBFD_FAKE_DEVICE", "-DAPB_HOST_TEST_ACCESSOR"]

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
        sources = [source or HERE/"bcm4378_fake_dart.c", HERE/"bringup.c", HERE/"linkup.c",
                   HERE/"perst_gpio.c", HERE/"radio_enable.c", HERE/"test_bcm4378_fake_dart.c"]
        if "-DBFD_FAKE_DEVICE" in flags:
            sources.extend([HERE/"register_accessor.c", CODEC/"dart_codec.c"])
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel_modes(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                self.assertIn("HOST ONLY PASS", self.command([self.build(name, flags)]).stdout)

    def test_compile_refusals(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_fake_dart.c", "host-test only"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_fake_dart.c", "shared PHY and PMGR writes compiled out"),
            ("missing-optin", ["-DBFD_FAKE_DEVICE"], "bcm4378_fake_dart.c", "host-test-only"),
            ("ndebug", ["-DNDEBUG"], "test_bcm4378_fake_dart.c", "requires assertions"),
        ]:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", f"-I{CODEC}", "-c", HERE/file,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(diagnostic, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("baseline", self.fake)])
        src = (HERE/"bcm4378_fake_dart.c").read_text()
        translation = "return dld_translate(&m->policy, &m->image, &w->scratch, iova, access, pa);"
        mutations = {
            "link-down": ("if ((link & 0x45u) != 1u)", "if (0)"),
            "wrong-requester-init": ("if (rid != 0x100 || dld_bindings_check(&binding, 1, 1, 1))", "if (dld_bindings_check(&binding, 1, 1, 1))"),
            "wrong-requester-lookup": ("if (rid != m->binding.rid || sid != m->binding.sid)", "if ((rid != m->binding.rid || sid != m->binding.sid) && 0)"),
            "ignore-permissions": (translation, "(void)access; return dld_translate(&m->policy, &m->image, &w->scratch, iova, DLD_READ, pa);"),
            "shift-physical-page": ("dld_leaf_encode(pa + n * DLD_PAGE, permissions, &word)", "dld_leaf_encode(pa + (n + 1) * DLD_PAGE, permissions, &word)"),
            "wrong-root-index": ("w->root + start.l1 * 8", "w->root + 0 * 8"),
            "missing-work-cap": ("if (bytes / DLD_PAGE > DLD_MAX_LEAVES)", "if (0)"),
            "skip-image-validation": ("if ((e = dld_validate(&m->policy, &image, &w->scratch, &stats))) return e;", "stats.leaf_count = (size_t)(bytes / DLD_PAGE);"),
            "miss-late-link-loss": ("if ((e = enter(m))) return e;", "if ((e = BFD_FAKE_OK)) return e;"),
            "clobber-error-pa": (translation, "e = dld_translate(&m->policy, &m->image, &w->scratch, iova, access, pa); if (e) *pa = 0; return e;"),
            "ignore-fault": ("if (m->faulted) return BFD_STATE;", "if (0) return BFD_STATE;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertEqual(src.count(before), 1)
                p = self.out/(name+".c"); p.write_text(src.replace(before, after))
                r = self.command([self.build(name, self.fake, p)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)
        p = self.out/"default-success.c"; p.write_text(src.replace("return BFD_GATED;", "return BFD_FAKE_OK;"))
        r = self.command([self.build("default-success", [], p)], success=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Assertion", r.stderr)

    def test_default_no_access_and_no_dart_register_implementation(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", f"-I{CODEC}", "-E", "-P", HERE/"bcm4378_fake_dart.c"]).stdout
            body = pre.split("int bfd_fake_map_compiled(void) { return 0; }", 1)[1]
            for forbidden in ("io->", "read32(", "write32(", "memcpy(", "memset(", "apg_", "ars_"):
                self.assertNotIn(forbidden, body)
            self.assertEqual(body.count("return BFD_GATED;"), 3)
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_fake_dart.c").read_text(), flags=re.S)
        for forbidden in ("write32", "APB_PHY", "APB_PMGR", "volatile", "mmap", "IOKit", "apg_", "ars_", "invalidate("):
            self.assertNotIn(forbidden, code)
        self.assertIn("dld_validate(&m->policy", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
