#!/usr/bin/env python3
"""Host-only composition: actual lifecycle core plus unchanged fake models."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
BRIDGE = HERE.parent
DART = BRIDGE.parent / "m1-dart"
SOURCES = [BRIDGE / (name + ".c") for name in (
    "bcm4378_transport", "bcm4378_identity_fw", "bcm4378_fake_dart",
    "bcm4378_fake_irq", "bcm4378_fake_ring", "bcm4378_fake_item", "bringup", "linkup",
    "perst_gpio", "radio_enable")]
SOURCES += [BRIDGE / "private_lifecycle/lifecycle.c", DART / "dart_codec.c", HERE / "test_owned.c"]
FAKE = ["-DBCO_HOST", "-DAPB_HOST_TEST_ACCESSOR", "-DBCT_FAKE_BAR_FILE",
        "-DBCI_FAKE_DEVICE", "-DBFD_FAKE_DEVICE", "-DBMI_FAKE_DEVICE", "-DBCR_FAKE_DEVICE", "-DBFI_FAKE_DEVICE"]


class OwnedCompositionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which("clang") or shutil.which("cc")
        if not cls.cc:
            raise RuntimeError("host compiler required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="bcm4378-composition-")
        cls.out = Path(cls.tmp.name)
        cls.flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                     f"-I{BRIDGE}", f"-I{DART}"]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def invoke(self, argv, success=True):
        p = subprocess.run([str(x) for x in argv], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
        if success:
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        return p

    def build(self, name, flags, replacement=None):
        sources = [replacement if p == HERE / "test_owned.c" and replacement else p for p in SOURCES]
        if "-DBCO_HOST" in flags:
            sources += [BRIDGE / "register_accessor.c"]
        out = self.out / name
        self.invoke([self.cc, *self.flags, *flags, *sources, "-o", out])
        return out

    def test_real_lease_effect_composition(self):
        p = self.invoke([self.build("fake", FAKE)])
        self.assertIn("HOST ONLY PASS", p.stdout)
        self.assertRegex(p.stdout, r"callback positions=[1-9][0-9]* before_after_cases=[1-9][0-9]*")
        print(p.stdout.strip())

    def test_default_and_kernel_no_io(self):
        for name, flags in (("default", []), ("kernel", ["-DKERNEL"])):
            with self.subTest(mode=name):
                p = self.invoke([self.build(name, flags)])
                self.assertIn("HOST ONLY PASS", p.stdout)

    def test_compile_refusals(self):
        for flags, diagnostic in ((["-DNDEBUG"], "requires assertions"),
                                  ([*FAKE, "-DKERNEL"], "host-only")):
            p = self.invoke([self.cc, *self.flags, *flags, "-c", HERE / "test_owned.c",
                             "-o", self.out / "refused.o"], success=False)
            self.assertNotEqual(p.returncode, 0)
            self.assertIn(diagnostic, p.stderr)

    def test_composition_mutations(self):
        source = (HERE / "test_owned.c").read_text()
        # Change the TEST orchestrator, not predecessor source or its receipts.
        mutations = {
            "skip-begin": ("STEP(begin_effect((kind),(mask)));int raw_=(expr);", "(void)(kind);(void)(mask);int raw_=(expr);"),
            "late-begin": ("STEP(begin_effect((kind),(mask)));int raw_=(expr);", "int raw_=(expr);STEP(begin_effect((kind),(mask)));"),
            "omit-mapping-resource": ("EFFECT(BPL_MAPPING,7,owned_mapping", "EFFECT(BPL_MAPPING,3,owned_mapping"),
            "wrong-descriptor-resource": ("EFFECT(BPL_DESCRIPTOR,12,bcr_configure", "EFFECT(BPL_DESCRIPTOR,8,bcr_configure"),
            "skip-preflight": ("if((phy&0x8f)!=0xf)return -901;", "if(0)return -901;"),
            "wrong-data-span": ("BPL_PAGE,DLD_APCIE_BASE}};", "32,DLD_APCIE_BASE}};"),
            "wrong-ring-label": ("4,8,DLD_APCIE_BASE};", "4,8,DLD_APCIE_BASE+4};"),
            "invent-consumer": ("RI)=0xbeef0001;", "RI)=0xbeef0000;"),
            "wrong-submit-kind": ("EFFECT(BPL_DOORBELL,12,bfi_submit", "EFFECT(BPL_CONFIG,12,bfi_submit"),
            "wrong-submit-resources": ("EFFECT(BPL_DOORBELL,12,bfi_submit", "EFFECT(BPL_DOORBELL,8,bfi_submit"),
            "failure-journaled-success": ("uint32_t outcome=raw?BPL_REPORTED_FAILURE:BPL_REPORTED_SUCCESS;", "uint32_t outcome=BPL_REPORTED_SUCCESS;"),
            "unknown-promoted-success": ("outcome=BPL_UNKNOWN;detail=-777;", "outcome=BPL_REPORTED_SUCCESS;detail=-777;"),
            "claim-clean-on-error": ("if(!raw)raw=finish();", "if(!raw)raw=finish();if(raw)F.logical_complete=1;"),
        }
        for name, (before, after) in mutations.items():
            with self.subTest(mutation=name):
                self.assertEqual(source.count(before), 1)
                path = self.out / (name + ".c")
                path.write_text(source.replace(before, after))
                p = self.invoke([self.build(name, FAKE, path)], success=False)
                self.assertNotEqual(p.returncode, 0, "mutation survived")
                self.assertIn("Assertion", p.stderr)
                print(f"MUTANT {name}: compiled, assertion killed", flush=True)

    def test_each_memory_state_operation_late_begin(self):
        source = (HERE / "test_owned.c").read_text()
        calls = {
            "mapping": ("BPL_MAPPING", "7", "owned_mapping()"),
            "irq-open": ("BPL_INTERRUPT", "16", "owned_irq_open()"),
            "irq-arm": ("BPL_INTERRUPT", "16", "owned_irq_arm()"),
            "irq-unmask": ("BPL_INTERRUPT", "16", "owned_irq_mask(0,0)"),
            "irq-receive": ("BPL_INTERRUPT", "16", "owned_irq_receive()"),
            "irq-dispatch": ("BPL_INTERRUPT", "16", "owned_irq_dispatch(&vector,&aic)"),
            "irq-mask": ("BPL_INTERRUPT", "16", "owned_irq_mask(0,1)"),
            "irq-close": ("BPL_INTERRUPT", "16", "owned_irq_close()"),
        }
        for name, (kind, mask, call) in calls.items():
            with self.subTest(operation=name):
                before = f"EFFECT({kind},{mask},{call});"
                after = f"do{{int raw_={call};STEP(begin_effect({kind},{mask}));STEP(end_effect(raw_));}}while(0);"
                self.assertEqual(source.count(before), 1)
                path = self.out / ("targeted-late-" + name + ".c")
                path.write_text(source.replace(before, after))
                p = self.invoke([self.build("targeted-late-" + name, FAKE, path)], success=False)
                self.assertNotEqual(p.returncode, 0, "targeted late begin survived")
                self.assertIn("Assertion", p.stderr)
                print(f"TARGETED MUTANT {name}: compiled, assertion killed", flush=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
