#!/usr/bin/env python3
"""Host-only fake BAR tests; never a target or a kernel collection."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("a host compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-transport-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}"]
        cls.fake = ["-DBCT_FAKE_BAR_FILE", "-DAPB_HOST_TEST_ACCESSOR"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def command(self, args, success=True):
        r = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1",
                                    PYTHONDONTWRITEBYTECODE="1"))
        if success:
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        return r

    def build(self, name, flags=(), source=None):
        exe = self.out / name
        sources = [source or HERE / "bcm4378_transport.c", HERE / "bringup.c", HERE / "linkup.c",
                   HERE / "perst_gpio.c", HERE / "radio_enable.c", HERE / "test_bcm4378_transport.c"]
        if "-DBCT_FAKE_BAR_FILE" in flags:
            sources.append(HERE / "register_accessor.c")
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                r = self.command([self.build(name, flags)])
                self.assertIn("HOST ONLY PASS", r.stdout)
                if name == "fake":
                    self.assertRegex(r.stdout, r"HOST fault boundaries: [1-9][0-9]*")
                    print(r.stdout.strip())

    def test_negative_compilation_and_default_no_io(self):
        cases = [("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_transport.c", "host-test only"),
                 ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_transport.c", "shared PHY and PMGR writes compiled out"),
                 ("unbound-host", ["-DBCT_FAKE_BAR_FILE"], "bcm4378_transport.c", "host-test-only"),
                 ("ndebug", ["-DNDEBUG"], "test_bcm4378_transport.c", "requires assertions")]
        for name, flags, source, msg in cases:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-c", HERE/source,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(msg, r.stderr)
        for flags in ([], ["-DKERNEL"]):
            p = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", "-E", "-P", HERE/"bcm4378_transport.c"]).stdout
            body = p.split("int bct_fake_transport_compiled(void) { return 0; }", 1)[1]
            for forbidden in ("read32(", "write32(", "apg_", "ars_", "apr_", "io->"):
                self.assertNotIn(forbidden, body)
            self.assertIn("return BCT_GATED;", body)

    def test_compiled_mutants(self):
        self.command([self.build("mutant-baseline", self.fake)])
        original = (HERE/"bcm4378_transport.c").read_text()
        mutations = {
            "link-down": ("return (link & 0x45u) == 1u ? BCT_FAKE_OK : BCT_LINK_DOWN;", "return BCT_FAKE_OK;"),
            "identity": ("if (word != 0x442514e4u)", "if (0)"),
            "window-readback": ("if (word != s->plan.window_page)", "if (0)"),
            "bad-shared-range": ("if (!ram_span(s, address, 52) || overlaps(address, 52, tail, 4))", "if (0)"),
            "alias-mailboxes": ("if (overlaps(words[i], bytes, words[j], j == 3 ? 20u : 4u))", "if (0)"),
            "busy-mailbox": ("if (pending) return finish(s, BCT_BUSY);", "if (0) return finish(s, BCT_BUSY);"),
            "wrong-layout": ("s->plan.pcie_core_revision >= 64", "s->plan.pcie_core_revision >= 256"),
            "single-old-kick": ("s->plan.pcie_core_revision <= 13 ? 2u : 1u", "s->plan.pcie_core_revision <= 13 ? 1u : 1u"),
            "skip-payload": ("if ((e = write_word(s, APR_BCM_BAR2, s->h2d_address, word)))", "if ((e = BCT_FAKE_OK))"),
            "missing-fault": ("if (result == BCT_IO || result == BCT_VERIFY || (result && s->pending_writes)) s->faulted = 1;", "/* lost fault latch */"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertIn(before, original)
                source = self.out/(name+".c")
                source.write_text(original.replace(before, after))
                r = self.command([self.build(name, self.fake, source)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)

    def test_previous_accessor_and_observer_regressions(self):
        for file in ("test_register_accessor.py", "test_readonly_accessor.py"):
            self.command([sys.executable, HERE/file])

    def test_no_hardware_or_gpio_implementation(self):
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_transport.c").read_text(), flags=re.S)
        for word in ("volatile", "ioremap", "mmap", "IOKit", "pci_set_master", "apg_", "ars_", "pmgr", "phy"):
            self.assertNotIn(word, code)
        self.assertIn("apr_bcm_io_bound(io)", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
