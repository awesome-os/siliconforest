#!/usr/bin/env python3
"""Source checks for the M1 platform patch (PDArmPlatformExpert, pinned e7aa00a).

These checks read the pinned baseline checkout and apply the patch to a
temporary copy of the pinned blobs. They do not compile, link, load or boot
anything. Compilation is left to the real PureDarwin/Nix build.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASELINE = REPO / "sources" / "puredarwin-baseline"
PIN = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"
PATCH = HERE / "platform.patch"
KEXT = "src/Kernel/Extensions/PDArmPlatformExpert"
XNU = "src/Kernel/xnu"
TOUCHED = {
    f"{KEXT}/CMakeLists.txt",
    f"{KEXT}/PDAppleAIC.cpp",
    f"{KEXT}/PDAppleBootCPUPMGR.cpp",
    f"{KEXT}/PDAppleBootCPUPMGR.h",
    f"{KEXT}/PDArmPlatformExpert.cpp",
}
EXISTING = sorted(p for p in TOUCHED if "BootCPUPMGR" not in p)


def git(*args, cwd=BASELINE):
    return subprocess.run(["git", *args], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout


def pinned(path):
    return git("show", f"{PIN}:{path}")


def function_body(text, qualified_name):
    """Body of the first definition of qualified_name, braces balanced."""
    m = re.search(r"^" + re.escape(qualified_name) + r"\([^)]*\)\s*\n\{", text, re.M)
    if m is None:
        return None
    depth, i = 0, m.end() - 1
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
    return None


def norm(lines):
    return [ln.strip() for ln in lines if ln.strip()]


# Each check returns a list of problems, so the mutation tests can reuse it.

def check_generic_path(old_pe, new_pe):
    problems = []
    start = "PDArmCPU::setCPUCount(ncpu);"
    end = 'IOLog("PDArmPlatformExpert: %u processor(s)\\n", ncpu);'

    def block(text):
        a, b = text.find(start), text.find(end)
        return norm(text[a:b + len(end)].splitlines()) if a >= 0 and b > a else None

    old_block = block(old_pe)
    body = function_body(new_pe, "PDArmPlatformExpert::startProcessors")
    if body is None:
        return ["base startProcessors() not defined"]
    if old_block is None or block(body) != old_block:
        problems.append("generic processor loop is not the original block")
    if "return bootCPU != NULL;" not in body:
        problems.append("generic startProcessors() drops the boot CPU check")
    start_body = function_body(new_pe, "PDArmPlatformExpert::start") or ""
    order = [start_body.find(s) for s in ("initPlatformInterrupts()",
                                          "startProcessors()",
                                          "initPlatformInterruptsLate()")]
    if -1 in order or order != sorted(order):
        problems.append("start() does not run interrupts, processors, late interrupts in order")
    if "new PDArmCPU" in start_body:
        problems.append("start() still builds PDArmCPU directly")
    return problems


def check_apple_override(new_pe):
    problems = []
    body = function_body(new_pe, "PDAppleARMPlatformExpert::startProcessors")
    if body is None:
        return ["Apple startProcessors() override missing"]
    decl = re.search(r"class PDAppleARMPlatformExpert.*?\};", new_pe, re.S)
    if decl is None or "bool startProcessors(void) APPLE_KEXT_OVERRIDE;" not in decl.group(0):
        problems.append("Apple subclass does not declare the override")
    for needle in ('serviceMatching("PassthruInterruptController")',
                   "gPlatformInterruptControllerName",
                   "PD_APPLE_SMP_WAIT_NS",
                   "PDAppleBootCPUPMGR_publish(this)"):
        if needle not in body:
            problems.append(f"Apple override lacks {needle}")
    if re.search(r"waitForMatchingService\(\s*matching\s*\)", body):
        problems.append("Apple override waits without a timeout")
    for banned in ("PDArmCPU", "PDArmGIC", "ml_processor_register"):
        if banned in body:
            problems.append(f"Apple override references {banned}")
    if body.count("return false;") < 3:
        problems.append("Apple override is missing a failure return")
    return problems


def pmgr_methods(iopmgr_h):
    """(return type, name, parameter types) of every IOPMGR virtual."""
    out = []
    for m in re.finditer(r"virtual\s+(\w+)\s+(\w+)\(([^)]*)\)", iopmgr_h):
        params = tuple(re.match(r"^(.*?)\s*\w+$", p.strip()).group(1)
                       for p in m.group(3).split(",") if p.strip())
        out.append((m.group(1), m.group(2), params))
    return out


def check_pmgr(pmgr_cpp, iopmgr_h):
    problems = []
    decl = re.search(r"class PDAppleBootCPUPMGR : public IOPMGR\s*\{(.*?)\};", pmgr_cpp, re.S)
    if decl is None:
        return ["PDAppleBootCPUPMGR class not found"]
    for ret, name, params in pmgr_methods(iopmgr_h):
        pattern = (r"\b" + ret + r"\s+" + name + r"\(" +
                   r",\s*".join(re.escape(p).replace(r"\ ", r"\s*") + r"\s*\w+" for p in params) +
                   r"\)\s+APPLE_KEXT_OVERRIDE;")
        if not re.search(pattern, decl.group(1)):
            problems.append(f"IOPMGR::{name}{params} not overridden exactly")
    for name in ("enableCPUCore", "disableCPUCore", "enableCPUCluster", "disableCPUCluster"):
        bodies = [m for m in re.finditer(r"^PDAppleBootCPUPMGR::" + name + r"\([^)]*\)\s*\n\{(.*?)^\}",
                                         pmgr_cpp, re.S | re.M)]
        if not bodies:
            problems.append(f"{name} has no definition")
        for b in bodies:
            if "panic(" not in b.group(1) or "return" in b.group(1):
                problems.append(f"{name} does not fail stop")
    for name in ("enterCPUIdle", "exitCPUIdle", "updateCPUIdle"):
        body = function_body(pmgr_cpp, "PDAppleBootCPUPMGR::" + name) or ""
        if "*newIdleTimeoutTicks = 0;" not in body or "!= NULL" not in body:
            problems.append(f"{name} does not disable the idle timer safely")
    init = function_body(pmgr_cpp, "PDAppleBootCPUPMGR::initCPUIdle") or ""
    if "supports_nap = FALSE" not in init or "powergate_latency = 0" not in init:
        problems.append("initCPUIdle advertises idle states")
    if re.search(r"ml_io_map|IOMemoryDescriptor|withPhysicalAddress", pmgr_cpp):
        problems.append("PMGR touches MMIO")
    pub = function_body(pmgr_cpp, "PDAppleBootCPUPMGR_publish") or ""
    for needle in ("attach(provider)", "start(provider)", "registerService()", "release()"):
        if needle not in pub:
            problems.append(f"publish lacks {needle}")
    return problems


def check_aic(old_aic, new_aic):
    problems = []
    init = function_body(new_aic, "PDAppleAIC::initAIC") or ""
    if "registerInterruptController(\n\t\t(OSSymbol *)dtControllerName, this);" not in init:
        problems.append("AIC is not registered under its phandle name")
    if "(OSSymbol *)gIODTDefaultInterruptController, this);" not in init:
        problems.append("AIC default-controller registration lost")
    finder = new_aic[new_aic.find("aic_find_registers("):]
    ph, name = finder.find('"AAPL,phandle"'), finder.find("IODTInterruptControllerName(aicNode)")
    if ph < 0 or name < 0 or ph > name:
        problems.append("phandle not checked before building the controller name")
    handler = function_body(new_aic, "PDAppleAIC::handleInterrupt") or ""
    call = handler.find("vector->handler(")
    tail = handler[call:handler.find("vector->interruptActive = 0;", call)]
    if "enableVector(irq, vector);" not in tail or "interruptDisabledHard = 1;" not in tail:
        problems.append("handled source is not unmasked or marked hard-disabled")
    if old_aic.count("write32(") != new_aic.count("write32("):
        problems.append("AIC register writes changed beyond the unmask path")
    return problems


def check_exports(exports):
    needed = ["__ZTV6IOPMGR", "__ZN6IOPMGR10gMetaClassE", "__ZN6IOPMGR10superClassE",
              "__ZN6IOPMGRC2EPK11OSMetaClass", "__ZN6IOPMGRD2Ev",
              "__Z27IODTInterruptControllerNameP15IORegistryEntry",
              "_gPlatformInterruptControllerName", "_gIODTDefaultInterruptController"]
    return [f"{s} not exported" for s in needed if s not in exports]


@unittest.skipUnless((BASELINE / ".git").exists(), "pinned PureDarwin baseline checkout absent")
class PlatformPatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="m1-platform-"))
        git("init", "-q", cwd=cls.tmp)
        for path in EXISTING:
            dest = cls.tmp / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(pinned(path))
        cls.check = subprocess.run(["git", "apply", "--check", str(PATCH)], cwd=cls.tmp,
                                   capture_output=True, text=True)
        if cls.check.returncode == 0:
            git("apply", str(PATCH), cwd=cls.tmp)
        cls.old = {p: pinned(p) for p in EXISTING}
        cls.new = {p: (cls.tmp / p).read_text() for p in TOUCHED if (cls.tmp / p).exists()}
        cls.iopmgr_h = pinned(f"{XNU}/iokit/IOKit/IOPMGR.h")
        cls.exports = pinned(f"{XNU}/config/IOKit.exports")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_pin(self):
        self.assertEqual(git("rev-parse", "HEAD").strip(), PIN)

    def test_applies_to_pinned_blobs(self):
        self.assertEqual(self.check.returncode, 0, self.check.stderr)

    def test_touches_only_the_kext(self):
        paths = set(re.findall(r"^\+\+\+ b/(\S+)", PATCH.read_text(), re.M))
        self.assertEqual(paths, TOUCHED)

    def test_generic_path_unchanged(self):
        pe = f"{KEXT}/PDArmPlatformExpert.cpp"
        self.assertEqual(check_generic_path(self.old[pe], self.new[pe]), [])

    def test_apple_hands_processors_to_xnu(self):
        self.assertEqual(check_apple_override(self.new[f"{KEXT}/PDArmPlatformExpert.cpp"]), [])

    def test_pmgr_implements_iopmgr(self):
        self.assertGreaterEqual(len(pmgr_methods(self.iopmgr_h)), 9)
        self.assertEqual(check_pmgr(self.new[f"{KEXT}/PDAppleBootCPUPMGR.cpp"], self.iopmgr_h), [])

    def test_aic(self):
        aic = f"{KEXT}/PDAppleAIC.cpp"
        self.assertEqual(check_aic(self.old[aic], self.new[aic]), [])

    def test_exports(self):
        self.assertEqual(check_exports(self.exports), [])

    def test_cmake_builds_pmgr(self):
        self.assertIn("    PDAppleBootCPUPMGR.cpp\n", self.new[f"{KEXT}/CMakeLists.txt"])

    def test_mutations_are_caught(self):
        pe = self.new[f"{KEXT}/PDArmPlatformExpert.cpp"]
        pmgr = self.new[f"{KEXT}/PDAppleBootCPUPMGR.cpp"]
        aic_old, aic = self.old[f"{KEXT}/PDAppleAIC.cpp"], self.new[f"{KEXT}/PDAppleAIC.cpp"]
        mutants = [
            ("success stub for a secondary start", check_pmgr,
             (pmgr.replace('panic("PDAppleBootCPUPMGR: start of cpu %u requested; "\n'
                           '\t    "secondary CPU bring-up is not implemented, boot with cpus=1", cpu_id);',
                           "(void)cpu_id;"), self.iopmgr_h)),
            ("idle timer left armed", check_pmgr,
             (pmgr.replace("*newIdleTimeoutTicks = 0;", "(void)newIdleTimeoutTicks;", 1), self.iopmgr_h)),
            ("missing override", check_pmgr,
             (pmgr.replace("\tvoid updateCPUIdle(UInt64 *newIdleTimeoutTicks) APPLE_KEXT_OVERRIDE;\n", ""),
              self.iopmgr_h)),
            ("Apple builds PDArmCPU", check_apple_override,
             (pe.replace("passthru->release();", "passthru->release();\n\tnew PDArmCPU;"),)),
            ("untimed wait", check_apple_override,
             (pe.replace("waitForMatchingService(matching, PD_APPLE_SMP_WAIT_NS)",
                         "waitForMatchingService(matching)"),)),
            ("generic loop changed", check_generic_path,
             (self.old[f"{KEXT}/PDArmPlatformExpert.cpp"], pe.replace("if (i == 0) bootCPU = cpu;", ""))),
            ("no unmask after dispatch", check_aic,
             (aic_old, aic.replace("\t\t\t\tenableVector(irq, vector);\n", ""))),
            ("phandle name not registered", check_aic,
             (aic_old, aic.replace("(OSSymbol *)dtControllerName, this);", "NULL, this);"))),
        ]
        originals = {check_pmgr: pmgr, check_apple_override: pe, check_generic_path: pe,
                     check_aic: aic}
        for label, check, args in mutants:
            with self.subTest(label):
                mutated = args[0] if check in (check_pmgr, check_apple_override) else args[1]
                self.assertNotEqual(mutated, originals[check], f"mutation did not apply: {label}")
                self.assertTrue(check(*args), f"mutant not caught: {label}")


if __name__ == "__main__":
    unittest.main()
