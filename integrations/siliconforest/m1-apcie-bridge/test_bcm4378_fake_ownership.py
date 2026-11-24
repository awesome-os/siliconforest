#!/usr/bin/env python3
"""Host-only registry lifetime/fault tests, never native DART or MSI setup."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
CODEC = HERE.parent/"m1-dart"


class FakeOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host C compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-fake-ownership-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}", f"-I{CODEC}"]
        cls.fake = ["-DBFO_FAKE_DEVICE", "-DBFD_FAKE_DEVICE", "-DBMI_FAKE_DEVICE", "-DAPB_HOST_TEST_ACCESSOR"]

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
        sources = [source or HERE/"bcm4378_fake_ownership.c", HERE/"bcm4378_fake_dart.c",
                   HERE/"bcm4378_fake_irq.c", HERE/"bringup.c", HERE/"linkup.c",
                   HERE/"perst_gpio.c", HERE/"radio_enable.c", HERE/"test_bcm4378_fake_ownership.c"]
        if "-DBFO_FAKE_DEVICE" in flags:
            sources.extend([HERE/"register_accessor.c", CODEC/"dart_codec.c"])
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                r = self.command([self.build(name, flags)])
                self.assertIn("HOST ONLY PASS", r.stdout)
                if name == "fake":
                    self.assertRegex(r.stdout, r"HOST ownership read-fault cases: [1-9][0-9]*")
                    print(r.stdout.strip())

    def test_compile_refusals(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_fake_ownership.c", "host-test only"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_fake_ownership.c", "shared PHY and PMGR writes compiled out"),
            ("missing-dependencies", ["-DBFO_FAKE_DEVICE"], "bcm4378_fake_ownership.c", "explicit fake DART and interrupt builds"),
            ("missing-accessor", ["-DBFO_FAKE_DEVICE", "-DBFD_FAKE_DEVICE", "-DBMI_FAKE_DEVICE"], "bcm4378_fake_ownership.c", "host-test-only"),
            ("ndebug", ["-DNDEBUG"], "test_bcm4378_fake_ownership.c", "requires assertions"),
        ]:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", f"-I{CODEC}", "-c", HERE/file,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(diagnostic, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("baseline", self.fake)])
        src = (HERE/"bcm4378_fake_ownership.c").read_text()
        mutations = {
            "skip-first-phy": ("if ((phy & 0x8fu) != 0x0fu)", "if (0)"),
            "ignore-link": ("if ((link & 0x45u) != 1)", "if (0)"),
            "ignore-pci": ("if (pci != 0x442514e4u)", "if (0)"),
            "skip-final-phy": ("return (phy & 0x8fu) == 0x0fu ? BFO_FAKE_OK : BFO_PHY_NOT_READY;", "return BFO_FAKE_OK;"),
            "allow-stream-theft": ("if (r->stream) return BFO_BUSY;", "if (0) return BFO_BUSY;"),
            "allow-vector-theft": ("if (r->vectors[vector]) return BFO_BUSY;", "if (0) return BFO_BUSY;"),
            "close-registry-with-vector": ("if (r->vectors[i]) return BFO_BUSY;", "if (r->vectors[i] && 0) return BFO_BUSY;"),
            "ignore-sticky-fault": ("if (m->map.faulted) return BFO_FAULTED;", "if (0) return BFO_FAULTED;"),
            "drop-fault-latch": ("m->map.faulted = 1;", "(void)m;"),
            "leave-leaf-mapped": ("memset(m->map.storage.leaf, 0, DLD_PAGE);", "(void)m->map.storage.leaf;"),
            "retain-binding-on-close": ("m->registry->stream = NULL;", "(void)m->registry->stream;"),
            "leave-stale-image": ("memset(&m->map.image, 0, sizeof m->map.image);", "(void)m->map.image;"),
            "stack-image-pointer": ("candidate.image.tables = m->map.tables;", "candidate.image.tables = candidate.tables;"),
            "close-pending-or-unmasked": ("if ((e = bmi_close(&candidate))) return e;", "candidate.closed = 1; candidate.armed = 0;"),
            "release-wrong-vector": ("s->registry->vectors[s->vector] = NULL;", "s->registry->vectors[(s->vector+1)%8] = NULL;"),
            "partial-irq-error-commit": ("if (e) return e;\n    s->source = *candidate;", "if (e) { s->source = *candidate; return e; }\n    s->source = *candidate;"),
            "wrong-vector-allocation": ("bmi_arm(&candidate, 1u << vector)", "bmi_arm(&candidate, 1u << ((vector+1)%8))"),
            "lost-copied-owner-check": ("m->registry->stream != m ||", "0 ||"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertIn(before, src)
                p = self.out/(name+".c"); p.write_text(src.replace(before, after))
                r = self.command([self.build(name, self.fake, p)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)
        p = self.out/"default-success.c"; p.write_text(src.replace("return BFO_GATED;", "return BFO_FAKE_OK;"))
        r = self.command([self.build("default-success", [], p)], success=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Assertion", r.stderr)

    def test_no_default_io_or_hardware_writer(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", f"-I{CODEC}", "-E", "-P", HERE/"bcm4378_fake_ownership.c"]).stdout
            body = pre.split("int bfo_fake_registry_compiled(void) { return 0; }", 1)[1]
            for forbidden in ("read32(", "write32(", "memcpy(", "memset(", "->"):
                self.assertNotIn(forbidden, body)
            self.assertNotRegex(body, r"\b(?:bfd|bmi)_[a-z_]+\s*\(")
            self.assertEqual(body.count("return BFO_GATED;"), 11)
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_fake_ownership.c").read_text(), flags=re.S)
        for forbidden in ("write32", "APB_PMGR", "volatile", "mmap", "IOKit", "apg_", "ars_", "pcie_init", "malloc", "invalidate("):
            self.assertNotIn(forbidden, code)
        self.assertIn("bfd_map_pages(&candidate", code)
        self.assertIn("bmi_close(&candidate)", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
