#!/usr/bin/env python3
"""Original host-only intent-before-callback and retained-lease checks."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
CODEC = HERE.parent/"m1-dart"


class FakeLedgerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host C compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-fake-ledger-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all", f"-I{HERE}", f"-I{CODEC}"]
        cls.fake = ["-DBFL_FAKE_DEVICE", "-DBFO_FAKE_DEVICE", "-DBFD_FAKE_DEVICE", "-DBMI_FAKE_DEVICE", "-DAPB_HOST_TEST_ACCESSOR"]

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
        sources = [source or HERE/"bcm4378_fake_effect_ledger.c", HERE/"bcm4378_fake_ownership.c",
                   HERE/"bcm4378_fake_dart.c", HERE/"bcm4378_fake_irq.c", HERE/"bringup.c", HERE/"linkup.c",
                   HERE/"perst_gpio.c", HERE/"radio_enable.c", HERE/"test_bcm4378_fake_effect_ledger.c"]
        if "-DBFL_FAKE_DEVICE" in flags:
            sources.extend([HERE/"register_accessor.c", CODEC/"dart_codec.c"])
        self.command([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_fake_default_kernel(self):
        for name, flags in [("fake", self.fake), ("default", []), ("kernel", ["-DKERNEL"])]:
            with self.subTest(mode=name):
                r = self.command([self.build(name, flags)])
                self.assertIn("HOST ONLY PASS", r.stdout)
                if name == "fake":
                    self.assertRegex(r.stdout, r"HOST ledger read-fault positions: [1-9][0-9]*; recorded/quarantined: [1-9][0-9]*")
                    print(r.stdout.strip())

    def test_compile_refusals(self):
        for name, flags, file, diagnostic in [
            ("kernel-fake", [*self.fake, "-DKERNEL"], "bcm4378_fake_effect_ledger.c", "host-test only"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "bcm4378_fake_effect_ledger.c", "shared PHY and PMGR writes compiled out"),
            ("missing-registry", ["-DBFL_FAKE_DEVICE"], "bcm4378_fake_effect_ledger.c", "explicit registry DART and interrupt fake builds"),
            ("missing-accessor", ["-DBFL_FAKE_DEVICE", "-DBFO_FAKE_DEVICE", "-DBFD_FAKE_DEVICE", "-DBMI_FAKE_DEVICE"], "bcm4378_fake_effect_ledger.c", "host-test-only"),
            ("ndebug", ["-DNDEBUG"], "test_bcm4378_fake_effect_ledger.c", "requires assertions"),
        ]:
            r = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", f"-I{CODEC}", "-c", HERE/file,
                              "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(diagnostic, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.command([self.build("baseline", self.fake)])
        src = (HERE/"bcm4378_fake_effect_ledger.c").read_text()
        mutations = {
            "ignore-generation": ("s->generation != t.generation", "0"),
            "reuse-generation": ("s->generation = ++l->last_generation;", "s->generation = 1; l->last_generation++;"),
            "allow-quarantined-retry": ("if (s->quarantined) return BFL_QUARANTINED;", "if (0) return BFL_QUARANTINED;"),
            "exposed-can-retire": ("if (s->exposed) return BFL_EXPOSED;", "if (0) return BFL_EXPOSED;"),
            "missing-private-half": ("if (s->private_started != s->private_complete) return BFL_STATE;", "if (0) return BFL_STATE;"),
            "intent-already-complete": ("kind, BFL_PENDING, 0", "kind, BFL_COMPLETE, 0"),
            "exposure-not-recorded": ("else s->exposed = 1;", "else s->exposed = 0;"),
            "allow-reentrant-operation": ("if (!l) return BFL_STATE;\n    if (l->busy) return BFL_BUSY;", "if (!l) return BFL_STATE;\n    if (0) return BFL_BUSY;"),
            "no-pair-begin": ("if (kind != BFL_EXPOSE) s->private_started++;", "if (kind != BFL_EXPOSE) s->private_started += 0;"),
            "failure-counted-complete": ("if (e) { r->phase = BFL_FAILED; s->quarantined = 1; }", "if (e) { r->phase = BFL_COMPLETE; s->quarantined = 1; }"),
            "callback-failure-not-quarantined": ("s->quarantined = 1; l->busy = 0;", "s->quarantined = 0; l->busy = 0;"),
            "postcheck-falsely-complete": ("r->result = e; r->phase = BFL_UNKNOWN;", "r->result = e; r->phase = BFL_COMPLETE;"),
            "generation-wrap": (" || l->last_generation == UINT64_MAX", ""),
            "history-overwrite": ("&l->records[l->count]", "&l->records[0]"),
            "history-budget-too-small": ("l->count >= BFL_RECORDS", "l->count >= BFL_RECORDS-1"),
            "premature-claim-release": ("e = bfo_irq_close(&q);", "l->registry.vectors[q.vector] = NULL; e = bfo_irq_close(&q);"),
            "skip-first-phy": ("if ((phy & 0x8fu) != 0x0fu)", "if (0)"),
            "ignore-link": ("if ((link & 0x45u) != 1)", "if (0)"),
            "skip-final-phy": ("return (phy & 0x8fu) == 0x0fu ? BFL_FAKE_OK : BFO_PHY_NOT_READY;", "return BFL_FAKE_OK;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(name=name):
                self.assertIn(before, src)
                p = self.out/(name+".c"); p.write_text(src.replace(before, after))
                r = self.command([self.build(name, self.fake, p)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)
        p = self.out/"default-success.c"; p.write_text(src.replace("return BFL_GATED;", "return BFL_FAKE_OK;"))
        r = self.command([self.build("default-success", [], p)], success=False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("Assertion", r.stderr)

    def test_no_default_callbacks_or_hardware_path(self):
        for flags in ([], ["-DKERNEL"]):
            pre = self.command([self.cc, "-std=c11", *flags, f"-I{HERE}", f"-I{CODEC}", "-E", "-P", HERE/"bcm4378_fake_effect_ledger.c"]).stdout
            body = pre.split("int bfl_fake_ledger_compiled(void) { return 0; }", 1)[1]
            for forbidden in ("read32(", "write32(", "memcpy(", "memset(", "->", "callback("):
                self.assertNotIn(forbidden, body)
            self.assertNotRegex(body, r"\bbfo_[a-z_]+\s*\(")
            self.assertEqual(body.count("return BFL_GATED;"), 5)
        code = re.sub(r"/\*.*?\*/", "", (HERE/"bcm4378_fake_effect_ledger.c").read_text(), flags=re.S)
        for forbidden in ("write32", "APB_PMGR", "volatile", "mmap", "IOKit", "apg_", "ars_", "pcie_init", "malloc", "invalidate("):
            self.assertNotIn(forbidden, code)
        header = (HERE/"bcm4378_fake_effect_ledger.h").read_text()
        self.assertRegex(header, r"int bfl_retire\(struct bfl_ledger \*, struct bfl_token, struct bfo_workspace \*\);")
        for forbidden in ("drain_ack", "force_reset", "mark_success", "clear_fault", "to_real"):
            self.assertNotIn(forbidden, code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
