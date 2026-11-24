#!/usr/bin/env python3
"""Host-only opaque frozen-fixture validation, never a firmware msgbuf ABI."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class FakeItemTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host C compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-fake-item-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}"]
        cls.fake = ["-DBFI_FAKE_DEVICE", "-DBCR_FAKE_DEVICE", "-DBCT_FAKE_BAR_FILE",
                    "-DAPB_HOST_TEST_ACCESSOR"]

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
        sources = [source or HERE/"bcm4378_fake_item.c", HERE/"bcm4378_fake_ring.c",
                   HERE/"bcm4378_transport.c", HERE/"bringup.c", HERE/"linkup.c",
                   HERE/"perst_gpio.c", HERE/"radio_enable.c", HERE/"test_bcm4378_fake_item.c"]
        if "-DBFI_FAKE_DEVICE" in flags:
            sources.append(HERE/"register_accessor.c")
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                r = self.command([self.build(name, flags)])
                self.assertIn("HOST ONLY PASS", r.stdout)
                if name == "fake":
                    self.assertRegex(r.stdout, r"HOST item-submit callback boundaries: [1-9][0-9]*")
                    print(r.stdout.strip())

    def test_negative_builds(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_fake_item.c", "host-test only"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_fake_item.c", "shared PHY and PMGR writes compiled out"),
            ("missing-ring", ["-DBFI_FAKE_DEVICE"], "bcm4378_fake_item.c", "explicit fake ring build"),
            ("missing-accessor", ["-DBFI_FAKE_DEVICE", "-DBCR_FAKE_DEVICE"], "bcm4378_fake_item.c", "host-test-only"),
            ("ndebug", ["-DNDEBUG"], "test_bcm4378_fake_item.c", "requires assertions"),
        ]:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c", HERE/file,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(diagnostic, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("baseline", self.fake)])
        src = (HERE/"bcm4378_fake_item.c").read_text()
        mutations = {
            "wrong-fixture-byte": ("0x77, 0x88", "0x77, 0x89"),
            "ignore-last-byte": ("memcmp(item, fixture, BFI_SYNTHETIC_BYTES)", "memcmp(item, fixture, BFI_SYNTHETIC_BYTES-1)"),
            "accept-short": ("bytes != BFI_SYNTHETIC_BYTES", "bytes > BFI_SYNTHETIC_BYTES"),
            "accept-oversized": ("bytes != BFI_SYNTHETIC_BYTES", "bytes < BFI_SYNTHETIC_BYTES"),
            "short-encode": ("memcpy(out, fixture, BFI_SYNTHETIC_BYTES)", "memcpy(out, fixture, BFI_SYNTHETIC_BYTES-1)"),
            "skip-initial-phy": ("if ((phy & 0x8fu) != 0x0fu)", "if (0)"),
            "ignore-link": ("if ((link & 0x45u) != 1)", "if (0)"),
            "skip-late-phy": ("return (phy & 0x8fu) == 0x0fu ? BFI_FAKE_OK : BCR_PHY_NOT_READY;", "return BFI_FAKE_OK;"),
            "accept-mismatch": ("? BFI_MISMATCH : BFI_FAKE_OK", "? BFI_FAKE_OK : BFI_FAKE_OK"),
            "skip-submit-validation": ("if (e) return e;", "(void)e;"),
            "overwrite-owned-buffer": ("if (overlap(out, bytes, r->plan.buffer, r->plan.capacity) || overlap(out, bytes, r, sizeof *r) ||",
                                        "if (overlap(out, bytes, r->plan.buffer, r->plan.capacity) && overlap(out, bytes, r, sizeof *r) &&"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertIn(before, src)
                p = self.out/(name+".c"); p.write_text(src.replace(before, after))
                r = self.command([self.build(name, self.fake, p)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)
        p = self.out/"default-success.c"; p.write_text(src.replace("return BFI_GATED;", "return BFI_FAKE_OK;"))
        r = self.command([self.build("default-success", [], p)], success=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Assertion", r.stderr)

    def test_default_no_io_or_allocation(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-E", "-P", HERE/"bcm4378_fake_item.c"]).stdout
            body = pre.split("int bfi_fake_item_compiled(void) { return 0; }", 1)[1]
            for forbidden in ("io.", "read32(", "write32(", "r->", "memcpy(", "memcmp(", "bcr_submit("):
                self.assertNotIn(forbidden, body)
            self.assertEqual(body.count("return BFI_GATED;"), 3)
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_fake_item.c").read_text(), flags=re.S)
        for forbidden in ("malloc", "alloc_coherent", "IOKit", "volatile", "mmap", "APB_PMGR", "apg_", "ars_", "pcie_init", "write32("):
            self.assertNotIn(forbidden, code)
        self.assertIn("return bcr_submit(r, item, bytes);", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
