#!/usr/bin/env python3
"""Host-only extracted IOGOP-close + real kernel003 console-control tests.

Only temporary copies and host harness executables are produced. This does not
build a kext, change a checkout, validate IOKit locking, revoke a mapping or
establish framebuffer/scanout behavior. Root owns the SDK and KC build.
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
ROOT = HERE.parents[3]
BASE = ROOT / "sources/puredarwin-baseline"
PIN = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"
CPP = "src/Kernel/Extensions/IOGOPFramebuffer/IOGOPFramebuffer.cpp"
HEADER = CPP.replace(".cpp", ".h")
FORMAT = HERE.parent / "framebuffer-30bit.patch"
WC = ROOT / "integrations/puredarwin/m1-wayland/iogop-vram-write-combine.patch"
PATCH = HERE / "console-return.patch"
CUMULATIVE = HERE / "console-return-cumulative.patch"
PREREQUISITES = {
    FORMAT: "91086c74d740991f6f98a117f9fa957b211fe071255e77a0d2abab26fbd90654",
    WC: "8337e974d6578049e689e4ad31ec1d6030ffa83900e86507d647d19d83165377",
}

spec = importlib.util.spec_from_file_location("frozen_console_resume", HERE.parent / "test_console_resume.py")
resume = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resume)


def run(argv, cwd=None):
    return subprocess.run(argv, cwd=cwd, text=True, capture_output=True, timeout=30)


def checked(argv, cwd=None):
    result = run(argv, cwd)
    if result.returncode:
        raise RuntimeError(f"{argv[0]} exited {result.returncode}:\n{result.stdout}{result.stderr}")
    return result.stdout


def pinned(path):
    return checked(["git", "show", f"{PIN}:{path}"], BASE)


def write_sources(directory, sources):
    for path, source in sources.items():
        target = directory / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(source)


CONTROL_API = r'''
static PE_Video host_info={1};
void host_console_boot(int graphics,int serial) {
    route=serial ? SERIAL : VIDEO;
    PE_initialize_console(&host_info,graphics ? kPEGraphicsMode : kPETextMode);
    PE_initialize_console(NULL,kPEAcquireScreen);
}
void host_console_open(void) {
    PE_initialize_console(NULL,kPEReleaseScreen);
    PE_initialize_console(NULL,kPEDisableScreen);
}
void host_console_remap(void) {
    PE_initialize_console(&host_info,kPEReleaseScreen);
    PE_initialize_console(&host_info,kPEEnableScreen);
    PE_initialize_console(NULL,kPETextScreen);
}
void host_console_graphics(void) { PE_initialize_console(&host_info,kPEGraphicsMode); }
void host_console_acquire(void) { PE_initialize_console(NULL,kPEAcquireScreen); }
int host_console_enabled(void) { return gc_enabled; }
int host_console_acquired(void) { return gc_acquired; }
int host_console_video(void) { return route==VIDEO; }
'''


class ConsoleReturn(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="m1-console-return-host-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.original = {p: pinned(p) for p in (CPP, HEADER, resume.PE, resume.VC)}
        write_sources(cls.directory, cls.original)
        for patch, expected in PREREQUISITES.items():
            if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
                raise RuntimeError("Frozen prerequisite changed: " + str(patch))
            checked(["git", "apply", "--no-index", str(patch)], cls.directory)
        cls.layered = {p: (cls.directory / p).read_text() for p in (CPP, HEADER)}
        for patch in (PATCH, resume.PATCH):
            checked(["git", "apply", "--no-index", str(patch)], cls.directory)
        cls.patched = {p: (cls.directory / p).read_text() for p in cls.original}
        cls.cc, cls.cxx = shutil.which("cc"), shutil.which("c++")
        if not cls.cc or not cls.cxx:
            raise RuntimeError("Host C/C++ compilers required for fake harness only")
        # Reuse the exact extracted ARM PE/VC state branches from kernel003's
        # source-aware test. Pixel sinks remain host doubles, compiled as C.
        control = resume.harness(cls.patched[resume.PE], cls.patched[resume.VC])
        control = control[:control.index("#define CHECK(expr)")] + CONTROL_API
        path = cls.directory / "console-control.c"
        path.write_text(control)
        cls.control = cls.directory / "console-control.o"
        checked([cls.cc, "-std=c11", "-O0", "-Wall", "-Werror",
                 "-Wno-unused-variable", "-Wno-unused-parameter", "-U__x86_64__", "-U__i386__",
                 "-c", str(path), "-o", str(cls.control)])
        cls.close_body = resume.body(cls.patched[CPP], "IOGOPFramebuffer::close")
        cls.fixed = cls.compile("fixed", cls.close_body)

    @classmethod
    def compile(cls, name, close_body):
        source = (HERE / "close_harness.cpp").read_text()
        predicate = "static bool gop_apple_soc_firmware_fb(IOService *provider)\n" + resume.body(
            cls.patched[CPP], "gop_apple_soc_firmware_fb")
        source = source.replace("// @FIRMWARE_PREDICATE@", predicate)
        source = source.replace("// @PATCHED_CLOSE@", "void IOGOPFramebuffer::close(void)\n" + close_body)
        path = cls.directory / (name + ".cpp")
        path.write_text(source)
        executable = cls.directory / name
        checked([cls.cxx, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
                 str(path), str(cls.control), "-o", str(executable)])
        return executable

    def test_exact_incremental_scope_and_frozen_kernel_sources(self):
        scope = set(re.findall(r"^\+\+\+ b/(.*)$", PATCH.read_text(), re.M))
        self.assertEqual(scope, {CPP, HEADER})
        self.assertEqual(self.patched[CPP].replace(
            self.patched[CPP][self.patched[CPP].index("/*\n * closeWork calls"):self.patched[CPP].index("void * IOGOPFramebuffer::getBaseAddress")],
            ""), self.layered[CPP])
        self.assertEqual(self.patched[HEADER].replace("    virtual void close(void) override;\n", ""), self.layered[HEADER])
        for path in (resume.PE, resume.VC):
            self.assertEqual(self.patched[path], (ROOT / "sources/puredarwin-m1-desktop" / path).read_text())

    def test_cumulative_and_ordered_incremental_are_identical(self):
        directory = self.directory / "cumulative"
        write_sources(directory, {p: self.original[p] for p in (CPP, HEADER)})
        checked(["git", "apply", "--no-index", str(CUMULATIVE)], directory)
        for p in (CPP, HEADER):
            self.assertEqual((directory / p).read_text(), self.patched[p])
        # This delta depends on the Apple predicate from the WC layer.
        self.assertIn("gop_apple_soc_firmware_fb(provider)", self.close_body)
        self.assertEqual(resume.body(self.layered[CPP], "gop_apple_soc_firmware_fb"),
                         resume.body(self.patched[CPP], "gop_apple_soc_firmware_fb"))

    def test_forty_one_close_and_composed_console_scenarios(self):
        for scenario in range(41):
            with self.subTest(scenario=scenario):
                result = run([str(self.fixed), str(scenario)])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_parent_only_reproduces_missing_text_return(self):
        original = self.compile("parent-only", "{ (void)gop_apple_soc_firmware_fb; "
                                "(void)IOLog; (void)PE_parse_boot_argn; super::close(); }")
        result = run([str(original), "0"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("platform.requests", result.stderr)

    def test_live_caller_and_parent_lifetime_contract(self):
        source = pinned(resume.IOFB)
        close_work = resume.body(source, "IOFramebuffer::closeWork")
        self.assertLess(close_work.index("SYSASSERTGATED"), close_work.index("close();"))
        self.assertLess(close_work.index("FBGATEGUARD"), close_work.index("close();"))
        parent = resume.body(source, "IOFramebuffer::close")
        self.assertIn("getPowerState() &&", parent)
        self.assertLess(parent.index("captured = false"), parent.index("fServerConnect = NULL"))
        uc = pinned("src/Kernel/Extensions/IOGraphicsFamily/IOFramebufferUserClient.cpp")
        self.assertNotIn("kPEAcquireScreen", resume.body(uc, "IOFramebufferUserClient::clientClose"))
        self.assertIn("fOwner->extClose()", resume.body(uc, "IOFramebufferUserClient::stop"))
        self.assertIn("OSDecrementAtomic", resume.body(uc, "IOFramebufferUserClient::didTerminate"))
        pdgop_close = resume.body(pinned(resume.PDGOP), "PDGOPClose")
        self.assertLess(pdgop_close.index("IOConnectUnmapMemory64"), pdgop_close.index("IOServiceClose"))
        self.assertEqual(self.close_body.count("setConsoleInfo("), 1)
        self.assertNotIn("registerPowerDriver", self.close_body)
        self.assertNotIn("setPowerState", self.close_body)

    def test_fault_mutants_are_rejected(self):
        mutations = {
            "no-parent": ("    super::close();", "", 0),
            "no-server-history": ("if (!hadServer)", "if (false && !hadServer)", 7),
            "ignore-shared-owner": (" || fSharedConnect != NULL", "", 6),
            "ignore-apple": ("!gop_apple_soc_firmware_fb(provider)", "(false && !gop_apple_soc_firmware_fb(provider))", 1),
            "ignore-power-model": ("if (getPowerState() != 0)", "if (false && getPowerState() != 0)", 12),
            "ignore-framebuffer-identity": ("(console.v_baseAddr & ~3UL) != (uintptr_t)fbBase", "false", 15),
            "ignore-text-mode": ("if (console.v_display != 2)", "if (false && console.v_display != 2)", 20),
            "ignore-text-optout": ("textBoot == 0 || ", "", 21),
            "ignore-route-optout": ("(mirror == 0 && gopConsole == 0)", "(false && mirror == 0 && gopConsole == 0)", 22),
            "ignore-dead": (" || dead", "", 10),
            "enable-is-not-acquire": ("setConsoleInfo(NULL, kPEAcquireScreen)", "setConsoleInfo(NULL, 6)", 0),
        }
        for name, (old, new, scenario) in mutations.items():
            with self.subTest(mutation=name):
                self.assertEqual(self.close_body.count(old), 1)
                mutant = self.compile(name, self.close_body.replace(old, new))
                self.assertNotEqual(run([str(mutant), str(scenario)]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
