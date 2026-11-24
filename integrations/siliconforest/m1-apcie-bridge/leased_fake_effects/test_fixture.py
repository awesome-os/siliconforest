#!/usr/bin/env python3
"""New host-only fixture: actual frozen c585 plus original fake models."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
BRIDGE = HERE.parent
DART = BRIDGE.parent / "m1-dart"
CORE = BRIDGE / "private_lifecycle"
SOURCES = [BRIDGE / (n + ".c") for n in (
    "bcm4378_transport", "bcm4378_identity_fw", "bcm4378_fake_dart",
    "bcm4378_fake_irq", "bcm4378_fake_ring", "bcm4378_fake_item",
    "bringup", "linkup", "perst_gpio", "radio_enable")]
SOURCES += [DART / "dart_codec.c", CORE / "lifecycle.c", HERE / "fixture.c"]
FAKE = ["-DLEASED_FAKE_EFFECTS", "-DAPB_HOST_TEST_ACCESSOR", "-DBCT_FAKE_BAR_FILE",
        "-DBCI_FAKE_DEVICE", "-DBFD_FAKE_DEVICE", "-DBMI_FAKE_DEVICE",
        "-DBCR_FAKE_DEVICE", "-DBFI_FAKE_DEVICE"]


class LeasedFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-leased-fake-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                     f"-I{BRIDGE}", f"-I{DART}", f"-I{CORE}"]
        pins = {
            "source-validation-001.json": "c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb",
            "lifecycle.c": "00ae4b0f7edf917f2282c95cb329a28e5bfbe7560c7e6333757ddeae7de0bded",
            "lifecycle.h": "87d47f866a13bdc1d935e4f497ed23ca1782345dd2ca89c0b3bd6d59a00e0fea",
        }
        for name, wanted in pins.items():
            if hashlib.sha256((CORE/name).read_bytes()).hexdigest() != wanted:
                raise RuntimeError("frozen c585 mismatch: " + name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def invoke(self, argv, success=True):
        r = subprocess.run([str(x) for x in argv], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
        if success:
            self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        return r

    def build(self, name, flags, replacement=None):
        sources = [replacement if p == HERE/"fixture.c" and replacement else p for p in SOURCES]
        if "-DLEASED_FAKE_EFFECTS" in flags:
            sources += [BRIDGE/"register_accessor.c"]
        exe = self.out/name
        self.invoke([self.cc, *self.flags, *flags, *sources, "-o", exe])
        return exe

    def test_leased_fake_effects(self):
        r = self.invoke([self.build("fake", FAKE)])
        self.assertIn("HOST ONLY PASS", r.stdout)
        self.assertRegex(r.stdout, r"callbacks=[1-9][0-9]* fault_cases=[1-9][0-9]* quarantined=[1-9][0-9]*")
        print(r.stdout.strip())

    def test_default_kernel_and_negative_builds(self):
        for name, flags in (("default", []), ("kernel", ["-DKERNEL"])):
            with self.subTest(mode=name):
                self.assertIn("HOST ONLY PASS", self.invoke([self.build(name, flags)]).stdout)
        for name, flags, text in (
            ("kernel-fake", [*FAKE, "-DKERNEL"], "host-only"),
            ("ndebug", ["-DNDEBUG"], "requires assertions"),
            ("shared", ["-DAPB_FAKE_REGISTER_FILE"], "shared PHY and PMGR writes compiled out"),
            ("missing-models", ["-DLEASED_FAKE_EFFECTS"], "all explicit fake models"),
        ):
            r = self.invoke([self.cc, *self.flags, *flags, "-c", HERE/"fixture.c", "-o", self.out/(name+".o")], success=False)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn(text, r.stderr)

    def test_compiled_assertion_mutants(self):
        self.invoke([self.build("baseline", FAKE)])
        src = (HERE/"fixture.c").read_text()
        mutations = {
            "skip-ticket-begin": ("int e_=begin_effect((K),(M));", "int e_=(F.kind=(K),F.resources=(M),0);"),
            "journal-after-call": ("int e_=begin_effect((K),(M)); if(e_) return e_; e_=(EXPR);", "int e_=(EXPR); int b_=begin_effect((K),(M)); if(b_) return b_;"),
            "missing-data-resource": ("MUTATE(BPL_DOORBELL,12,bfi_submit", "MUTATE(BPL_DOORBELL,8,bfi_submit"),
            "stale-ticket": ("int rc=bpl_effect_begin(&F.registry,&F.lease,kind,resources,&F.ticket);", "struct bpl_ticket old=F.ticket; int rc=bpl_effect_begin(&F.registry,&F.lease,kind,resources,&F.ticket); F.ticket=old;"),
            "skip-result": ("bpl_effect_result(&F.registry,&F.ticket,outcome,rc)", "((void)outcome,rc?BPL_RETAINED:BPL_OK)"),
            "failed-callback-blessed": ("bpl_effect_result(&F.registry,&F.ticket,outcome,rc)", "((void)outcome,bpl_effect_result(&F.registry,&F.ticket,BPL_REPORTED_SUCCESS,0))"),
            "unknown-blessed": ("outcome=BPL_UNKNOWN;", "outcome=BPL_REPORTED_SUCCESS;"),
            "swallow-effect-error": ("e_=end_effect(e_); if(e_) return e_;", "e_=end_effect(e_); if(e_) return 0;"),
            "clear-history": ("F.result_count++; F.kind=F.resources=0;", "F.result_count++; F.kind=F.resources=0; F.registry.entries[F.lease.slot].effect_count=0; F.registry.entries[F.lease.slot].ever_resources=0; F.registry.entries[F.lease.slot].state=BPL_PRIVATE_METADATA;"),
            "skip-read-error-quarantine": ("if (inspect().effect_count) assert(bpl_quarantine(&F.registry,&F.lease,rc)==BPL_RETAINED);", "if (0) assert(bpl_quarantine(&F.registry,&F.lease,rc)==BPL_RETAINED);"),
            "retry-after-history": ("if (s.state!=BPL_PRIVATE_METADATA || s.pending || s.effect_count) return BPL_STATE;", "if (0) return BPL_STATE;"),
            "skip-preflight": ("READ(preflight());", "if (0) READ(preflight());"),
            "wrong-ring-label": ("plan={data_page,32,4,8,DLD_APCIE_BASE}", "plan={data_page,32,4,8,DLD_APCIE_BASE+4}"),
            "wrong-ring-backing": ("plan={data_page,32,4,8,DLD_APCIE_BASE}", "plan={root_page,32,4,8,DLD_APCIE_BASE}"),
            "wrong-mapped-label": ("&F.workspace,DLD_APCIE_BASE,DATA_PA,DLD_PAGE,DLD_READ", "&F.workspace,DLD_APCIE_BASE,DATA_PA+DLD_PAGE,DLD_PAGE,DLD_READ"),
            "pretend-32B-backing": ("data_page,DATA_PA,BPL_PAGE,DLD_APCIE_BASE", "data_page,DATA_PA,32,DLD_APCIE_BASE"),
            "wrong-span-generation": ("BPL_DATA,BPL_READ,3,F.lease.generation,", "BPL_DATA,BPL_READ,3,F.lease.generation+1,"),
            "invent-consumer-progress": ("*word(APR_BCM_BAR2,RI)=0xbeef0001;", "*word(APR_BCM_BAR2,RI)=0xbeef0000;"),
            "lose-first-error": ("F.first_model_error=rc;", "F.first_model_error=0;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(mutation=name):
                self.assertIn(before, src)
                p = self.out/(name+".c"); p.write_text(src.replace(before, after))
                r = self.invoke([self.build(name, FAKE, p)], success=False)
                self.assertNotEqual(r.returncode, 0, "mutant survived")
                self.assertIn("Assertion", r.stderr)

    def test_scope_shape(self):
        src = (HERE/"fixture.c").read_text()
        code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        for forbidden in ("F.owner", "struct bfo_registry", "struct bfl_ledger", "native_iokit", "backing.h", "pmap", "mmap", "malloc", "pcie_init"):
            self.assertNotIn(forbidden, code)
        self.assertEqual(code.count("struct bpl_registry registry;"), 1)
        self.assertEqual(code.count("_Alignas(16384) static uint8_t"), 3)
        self.assertNotRegex(code, r"\([^)]*\*\)\s*(?:F\.lease|F\.ticket|ROOT_PA|LEAF_PA|DATA_PA)")
        self.assertIn("MUTATE(BPL_DOORBELL,12,bfi_submit", src)
        self.assertIn("MUTATE(BPL_INTERRUPT,16,bmi_close", src)
        self.assertEqual(src.count("MUTATE("), 19)  # one definition + exact18-call chain
        for flags in ([], ["-DKERNEL"]):
            pre = self.invoke([self.cc, *self.flags, *flags, "-E", "-P", HERE/"fixture.c"]).stdout
            body = pre.split("enum { LEASED_GATED=100 };", 1)[1]
            for forbidden in ("read_fake", "write_fake", "bpl_init(", "bpl_claim(", "bfi_submit("):
                self.assertNotIn(forbidden, body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
