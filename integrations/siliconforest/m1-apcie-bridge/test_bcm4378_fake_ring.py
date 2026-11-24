#!/usr/bin/env python3
"""Host-only preallocated fake ring/TCM index/BCM doorbell checks."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class FakeRingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host C compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-fake-ring-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}"]
        cls.fake = ["-DBCR_FAKE_DEVICE", "-DBCT_FAKE_BAR_FILE", "-DAPB_HOST_TEST_ACCESSOR"]

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
        sources = [source or HERE/"bcm4378_fake_ring.c", HERE/"bcm4378_transport.c",
                   HERE/"bringup.c", HERE/"linkup.c", HERE/"perst_gpio.c", HERE/"radio_enable.c",
                   HERE/"test_bcm4378_fake_ring.c"]
        if "-DBCR_FAKE_DEVICE" in flags:
            sources.append(HERE/"register_accessor.c")
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                r = self.command([self.build(name, flags)])
                self.assertIn("HOST ONLY PASS", r.stdout)
                if name == "fake":
                    self.assertRegex(r.stdout, r"HOST callback boundaries: [1-9][0-9]*")
                    print(r.stdout.strip())

    def test_negative_builds(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_fake_ring.c", "host-test only"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_fake_ring.c", "shared PHY and PMGR writes compiled out"),
            ("missing-optin", ["-DBCR_FAKE_DEVICE"], "bcm4378_fake_ring.c", "host-test-only"),
            ("ndebug", ["-DNDEBUG"], "test_bcm4378_fake_ring.c", "requires assertions"),
        ]:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c", HERE/file,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(diagnostic, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("baseline", self.fake)])
        src = (HERE/"bcm4378_fake_ring.c").read_text()
        mutations = {
            "skip-first-phy": ("if ((phy & 0x8fu) != 0x0fu)", "if (0)"),
            "ignore-link": ("if ((link & 0x45u) != 1)", "if (0)"),
            "wrong-doorbell": ("(flags & 0x80000000u) ? 0xa20u : 0x140u", "(flags & 0x80000000u) ? 0x140u : 0x140u"),
            "clobber-index-neighbor": ("(w & 0xffff0000u) | next", "next"),
            "overwrite-full": ("if (next == read)", "if (0)"),
            "no-wrap": ("(r->write_index+1) % r->plan.depth", "r->write_index+1"),
            "early-doorbell": ("memcpy(r->plan.buffer+r->write_index*r->plan.item_bytes, item, bytes);", "memcpy(r->plan.buffer+r->write_index*r->plan.item_bytes, item, bytes); if ((e = write_word(r, APR_BCM_BAR0, r->bell_offset, 1))) return e;"),
            "skip-fault-latch": ("if (e == BCR_IO || e == BCR_VERIFY || (e && r->inflight)) r->faulted = 1;", "(void)r; /* dropped fault */"),
            "alias-shared-memory": ("if (overlap(values[i], sizes[i], protected_base[j], protected_size[j]))", "if (overlap(values[i], sizes[i], protected_base[j], protected_size[j]) && 0)"),
            "accept-dma-indices": ("(flags & 0x10000u)", "(flags & 0u)"),
            "wrong-item-size": ("!item || bytes != r->plan.item_bytes ||", "!item ||"),
            "lose-address-high": ("(uint32_t)(r->plan.fake_bus_label >> 32)", "(uint32_t)(r->plan.fake_bus_label >> 63)"),
            "miss-late-phy-reset": ("return (phy & 0x8fu) == 0x0fu ? BCR_FAKE_OK : BCR_PHY_NOT_READY;", "return BCR_FAKE_OK;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertIn(before, src)
                p = self.out/(name+".c"); p.write_text(src.replace(before, after))
                r = self.command([self.build(name, self.fake, p)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)
        p = self.out/"default-success.c"; p.write_text(src.replace("return BCR_GATED;", "return BCR_FAKE_OK;"))
        r = self.command([self.build("default-success", [], p)], success=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Assertion", r.stderr)

    def test_default_no_io_or_allocation(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-E", "-P", HERE/"bcm4378_fake_ring.c"]).stdout
            body = pre.split("int bcr_fake_ring_compiled(void) { return 0; }", 1)[1]
            for forbidden in ("io.", "read32(", "write32(", "r->", "memcpy(", "apg_", "ars_"):
                self.assertNotIn(forbidden, body)
            self.assertEqual(body.count("return BCR_GATED;"), 4)
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_fake_ring.c").read_text(), flags=re.S)
        for forbidden in ("malloc", "alloc_coherent", "IOKit", "volatile", "mmap", "APB_PMGR", "apg_", "ars_", "pcie_init"):
            self.assertNotIn(forbidden, code)
        self.assertIn("write_word(r, APR_BCM_BAR0, r->bell_offset, 1)", code)
        self.assertNotIn("write_word(r, APB_PHY", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
