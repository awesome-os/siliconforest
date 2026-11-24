#!/usr/bin/env python3
"""Source checks for platform patch revision 2 (Apple reboot path).

Rev2 = rev1 plus one change: the Apple platform expert installs no
PE_halt_restart handler (PSCI does not exist on Apple SoCs, and the PSCI probe
takes IORegistry locks on the panic path). These checks read the pinned
baseline checkout and temporary copies; they do not compile, load or boot.
"""

import hashlib
import importlib.util
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PLATFORM_DIR = HERE.parents[1]
REPO = HERE.parents[4]
BASELINE = REPO / "sources" / "puredarwin-baseline"
ISOLATED = REPO / "sources" / "puredarwin-m1-bringup"
PIN = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"
KEXT = "src/Kernel/Extensions/PDArmPlatformExpert"
XNU = "src/Kernel/xnu"
REV1 = PLATFORM_DIR / "platform.patch"
REV1_SHA256 = "c4a4e83a202177c7e91e5a0d9b187d657fa0f11fa8f0b554827fb8a2501a8c12"
CUMULATIVE = HERE / "platform-rev2.patch"
INCREMENTAL = HERE / "rev1-to-rev2.patch"
FILES = ["CMakeLists.txt", "PDAppleAIC.cpp", "PDAppleBootCPUPMGR.cpp",
         "PDAppleBootCPUPMGR.h", "PDArmPlatformExpert.cpp"]
EXISTING = ["CMakeLists.txt", "PDAppleAIC.cpp", "PDArmPlatformExpert.cpp"]
PE = f"{KEXT}/PDArmPlatformExpert.cpp"

# Reuse the frozen rev1 checks (generic loop, Apple CPU handoff, PMGR, AIC,
# exports) without modifying that file.
_spec = importlib.util.spec_from_file_location("rev1_checks", PLATFORM_DIR / "test_platform_patch.py")
rev1 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rev1)
function_body = rev1.function_body


def git(*args, cwd=BASELINE):
    return subprocess.run(["git", *args], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout


def pinned(path):
    return git("show", f"{PIN}:{path}")


def tree(patches):
    """Pinned blobs with the given patches applied, as {relative path: text}."""
    tmp = Path(tempfile.mkdtemp(prefix="m1-platform-rev2-"))
    try:
        git("init", "-q", cwd=tmp)
        for name in EXISTING:
            dest = tmp / KEXT / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(pinned(f"{KEXT}/{name}"))
        for patch in patches:
            git("apply", str(patch), cwd=tmp)
        return {f"{KEXT}/{n}": (tmp / KEXT / n).read_text() for n in FILES if (tmp / KEXT / n).exists()}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def code_lines(lines):
    """Drop comment-only lines, for 'no new reset mechanism' scanning."""
    out = []
    for ln in lines:
        s = ln.strip()
        if s.startswith(("//", "/*", "*", "*/")) or not s:
            continue
        out.append(s)
    return out


def check_halt_restart(pe):
    problems = []
    assigns = re.findall(r"PE_halt_restart\s*=[^=][^;]*;", pe)
    if assigns != ["PE_halt_restart = pdArmHaltRestart;"]:
        problems.append(f"unexpected PE_halt_restart assignments: {assigns}")
    start = function_body(pe, "PDArmPlatformExpert::start") or ""
    if not re.search(r"if \(usesPSCIHaltRestart\(\)\) \{\s*PE_halt_restart = pdArmHaltRestart;\s*\}", start):
        problems.append("start() does not gate the PSCI handler on usesPSCIHaltRestart()")
    base = function_body(pe, "PDArmPlatformExpert::usesPSCIHaltRestart")
    if base is None or base.strip() != "return true;":
        problems.append("generic default is not 'return true'")
    apple = function_body(pe, "PDAppleARMPlatformExpert::usesPSCIHaltRestart")
    if apple is None or apple.strip() != "return false;":
        problems.append("Apple override is not 'return false'")
    decl = re.search(r"class PDAppleARMPlatformExpert.*?\};", pe, re.S)
    if decl is None or "bool usesPSCIHaltRestart(void) APPLE_KEXT_OVERRIDE;" not in decl.group(0):
        problems.append("Apple subclass does not declare the override")
    base_decl = re.search(r"class PDArmPlatformExpert : public IODTPlatformExpert.*?\};", pe, re.S)
    if base_decl is None or "virtual bool usesPSCIHaltRestart(void);" not in base_decl.group(0):
        problems.append("base class does not declare the virtual")
    apple_part = pe[pe.find("class PDAppleARMPlatformExpert"):]
    if "PE_halt_restart" in "\n".join(code_lines(apple_part.splitlines())):
        problems.append("Apple code references PE_halt_restart")
    return problems


def check_incremental_scope(patch_text):
    problems = []
    removed = [l[1:] for l in patch_text.splitlines() if l.startswith("-") and not l.startswith("---")]
    added = [l[1:] for l in patch_text.splitlines() if l.startswith("+") and not l.startswith("+++")]
    if removed != ["\tPE_halt_restart = pdArmHaltRestart;"]:
        problems.append(f"rev1->rev2 removes more than the unconditional assignment: {removed}")
    files = set(re.findall(r"^\+\+\+ b/(\S+)", patch_text, re.M))
    if files != {PE}:
        problems.append(f"rev1->rev2 touches {files}")
    banned = re.compile(r"wdt|watchdog|ml_io_map|IOMemoryDescriptor|withPhysicalAddress|hvc|smc|systemReset|"
                        r"PE_halt_restart\s*=(?!\s*pdArmHaltRestart\b)", re.I)
    for ln in code_lines(added):
        if banned.search(ln):
            problems.append(f"new reset mechanism in added code: {ln}")
    return problems


@unittest.skipUnless((BASELINE / ".git").exists(), "pinned PureDarwin baseline checkout absent")
class PlatformRev2(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.old = {f"{KEXT}/{n}": pinned(f"{KEXT}/{n}") for n in EXISTING}
        cls.rev1 = tree([REV1])
        cls.cum = tree([CUMULATIVE])
        cls.inc = tree([REV1, INCREMENTAL])
        cls.iopmgr_h = pinned(f"{XNU}/iokit/IOKit/IOPMGR.h")
        cls.exports = pinned(f"{XNU}/config/IOKit.exports")

    def test_pin(self):
        self.assertEqual(git("rev-parse", "HEAD").strip(), PIN)

    def test_frozen_rev1_untouched(self):
        self.assertEqual(hashlib.sha256(REV1.read_bytes()).hexdigest(), REV1_SHA256)

    def test_cumulative_equals_rev1_plus_incremental(self):
        self.assertEqual(self.cum, self.inc)

    def test_cumulative_touches_only_the_kext(self):
        paths = set(re.findall(r"^\+\+\+ b/(\S+)", CUMULATIVE.read_text(), re.M))
        self.assertEqual(paths, {f"{KEXT}/{n}" for n in FILES})

    def test_incremental_scope(self):
        self.assertEqual(check_incremental_scope(INCREMENTAL.read_text()), [])

    def test_apple_installs_no_halt_restart(self):
        self.assertEqual(check_halt_restart(self.cum[PE]), [])

    def test_rev1_behaviour_preserved(self):
        self.assertEqual(rev1.check_generic_path(self.old[PE], self.cum[PE]), [])
        self.assertEqual(rev1.check_apple_override(self.cum[PE]), [])
        self.assertEqual(rev1.check_pmgr(self.cum[f"{KEXT}/PDAppleBootCPUPMGR.cpp"], self.iopmgr_h), [])
        self.assertEqual(rev1.check_aic(self.old[f"{KEXT}/PDAppleAIC.cpp"], self.cum[f"{KEXT}/PDAppleAIC.cpp"]), [])
        self.assertEqual(rev1.check_exports(self.exports), [])
        for name in FILES:
            if name != "PDArmPlatformExpert.cpp":
                self.assertEqual(self.cum[f"{KEXT}/{name}"], self.rev1[f"{KEXT}/{name}"], name)

    def test_generic_path_still_installs_psci(self):
        # Apart from the new hook, every line of rev1's PE survives in rev2.
        rev1_lines = [l for l in self.rev1[PE].splitlines() if l != "\tPE_halt_restart = pdArmHaltRestart;"]
        rev2_lines = self.cum[PE].splitlines()
        it = iter(rev2_lines)
        self.assertTrue(all(any(l == r for r in it) for l in rev1_lines), "rev1 line order not preserved")

    @unittest.skipUnless((ISOLATED / KEXT).exists(), "isolated bringup tree absent")
    def test_incremental_applies_to_isolated_tree_copy(self):
        tmp = Path(tempfile.mkdtemp(prefix="m1-platform-iso-"))
        try:
            shutil.copytree(ISOLATED / KEXT, tmp / KEXT)
            git("init", "-q", cwd=tmp)
            r = subprocess.run(["git", "apply", "--check", str(INCREMENTAL)], cwd=tmp,
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_mutations_are_caught(self):
        pe = self.cum[PE]
        inc = INCREMENTAL.read_text()
        mutants = [
            ("Apple keeps PSCI", check_halt_restart,
             pe.replace("PDAppleARMPlatformExpert::usesPSCIHaltRestart(void)\n{\n\treturn false;",
                        "PDAppleARMPlatformExpert::usesPSCIHaltRestart(void)\n{\n\treturn true;")),
            ("unconditional install", check_halt_restart,
             pe.replace("if (usesPSCIHaltRestart()) {\n\t\tPE_halt_restart = pdArmHaltRestart;\n\t}",
                        "PE_halt_restart = pdArmHaltRestart;")),
            ("generic default flipped", check_halt_restart,
             pe.replace("PDArmPlatformExpert::usesPSCIHaltRestart(void)\n{\n\treturn true;",
                        "PDArmPlatformExpert::usesPSCIHaltRestart(void)\n{\n\treturn false;")),
            ("Apple fake reset handler", check_halt_restart,
             pe.replace("\treturn \"Apple ARM\";", "\tPE_halt_restart = pdAppleFakeReset;\n\treturn \"Apple ARM\";")),
            ("override undeclared", check_halt_restart,
             pe.replace("\tbool usesPSCIHaltRestart(void) APPLE_KEXT_OVERRIDE;\n", "")),
            ("invented WDT reset", check_incremental_scope,
             inc.replace("+\treturn false;\n", "+\tpd_apple_wdt_reset();\n+\treturn false;\n")),
            ("extra removal", check_incremental_scope,
             inc.replace(" \tPDSun60iDVFS_start();\n", "-\tPDSun60iDVFS_start();\n", 1)),
        ]
        for label, check, text in mutants:
            with self.subTest(label):
                original = pe if check is check_halt_restart else inc
                self.assertNotEqual(text, original, f"mutation did not apply: {label}")
                self.assertTrue(check(text), f"mutant not caught: {label}")


if __name__ == "__main__":
    unittest.main()
