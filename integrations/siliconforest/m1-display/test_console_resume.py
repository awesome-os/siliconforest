#!/usr/bin/env python3
"""Execute extracted ARM console control code with host-only rendering stubs.

No SDK, kext, target, or existing checkout is built or modified. The temporary
host executable tests actual patched PE/control branches, not a second Python
implementation of their state machine. Mapping, locks, scanout and user-client
lifetime remain outside this test's coverage.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = ROOT / "sources/puredarwin-baseline"
PIN = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"
PE = "src/Kernel/xnu/pexpert/arm/pe_init.c"
VC = "src/Kernel/xnu/osfmk/console/video_console.c"
IOFB = "src/Kernel/Extensions/IOGraphicsFamily/IOFramebuffer.cpp"
PDGOP = "src/Libraries/PDGOP/PDGOP.c"
PATCH = HERE / "console-resume.patch"


def run(argv, cwd=None):
    return subprocess.run(argv, cwd=cwd, text=True, capture_output=True)


def pinned(path):
    result = run(["git", "show", f"{PIN}:{path}"], BASE)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout


def body(text, name):
    match = re.search(r"(?<![\w:])" + re.escape(name) + r"\([^;]*?\)\s*\{", text)
    if match is None:
        raise ValueError("Missing source function " + name)
    start = match.end() - 1
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for token in tokens.finditer(text, start):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if not depth:
                return text[start:token.end()]
    raise ValueError("Unbalanced source function " + name)


PREAMBLE = r'''
/* Host-only: stub pixel/progress sinks, retain actual ARM control branches. */
typedef int boolean_t;
typedef unsigned int uint32_t;
#define TRUE 1
#define FALSE 0
#define NULL ((void *)0)
#define XNU_TARGET_OS_OSX 1
#define bzero(p,n) __builtin_memset(p,0,n)
#define kprintf(...) ((void)0)
extern int printf(const char *, ...);
extern int atoi(const char *);
enum { kPEGraphicsMode=1, kPETextMode=2, kPETextScreen=3,
       kPEAcquireScreen=4, kPEReleaseScreen=5, kPEEnableScreen=6,
       kPEDisableScreen=7 };
enum { kVCAcquireImmediate=1, kVCDarkReboot=2, kVCDarkBackground=4,
       kVCLightBackground=8, kProgressReacquireDelay=5 };
enum { SERIAL=0, VIDEO=1 };
typedef struct { int v_scale; } PE_Video;
static struct { PE_Video video; } PE_state;
static struct { int dx,dy; } default_progress;
static char default_progress_data1x[1],default_progress_data2x[1],default_progress_data3x[1];
static unsigned char appleClut8[1];
static int vc_progress_initialized, vc_progress_white, vc_acquire_delay;
static int vc_progress_withmeter;
static void *vc_clut8;
static struct { unsigned options; } vc_progress_options,vc_user_options;
static int route=VIDEO, gc_enabled, disableConsoleOutput, serial_output=1;
static int enable_calls, want_text_boot=1;
static int console_is_serial(void) { return route==SERIAL; }
static int switch_to_serial_console(void) { int old=route; route=SERIAL; return old; }
static void switch_to_old_console(int value) { route=value; }
static void PE_parse_boot_argn(const char *name, void *value, unsigned size)
{ (void)name; (void)size; *(uint32_t *)value=want_text_boot; }
static void vc_progress_initialize(void *a,void *b,void *c,void *d,void *e)
{ (void)a;(void)b;(void)c;(void)d;(void)e; }
static void vc_progress_set(boolean_t on, uint32_t delay) { (void)on;(void)delay; }
static void vc_enable_progressmeter(boolean_t on) { (void)on; }
static void gc_enable(boolean_t enable)
{
    gc_enabled=enable;
    if (enable) { ++enable_calls; disableConsoleOutput=FALSE; }
    else if (!console_is_serial() && !serial_output) disableConsoleOutput=TRUE;
}
static void gc_pause(boolean_t pause,boolean_t graphics_now)
{
    disableConsoleOutput=pause && !console_is_serial() && !serial_output;
    gc_enabled=!pause && !graphics_now;
    if (gc_enabled) ++enable_calls;
}
'''

SCENARIOS = r'''
#define CHECK(expr) do { if (!(expr)) { printf("line %d: %s\n",__LINE__,#expr); return 1; } } while (0)
static PE_Video info={1};
static void op(int code, int remap) { PE_initialize_console(remap ? &info : NULL,code); }
static void start_text(void) { op(kPETextMode,1);op(kPEAcquireScreen,0); }
int main(int argc,char **argv)
{
    int scenario=argc>1 ? atoi(argv[1]) : 0;
    int before;
    if (scenario==9) want_text_boot=0;
    if (scenario==12) route=SERIAL;
    if (scenario==10) { op(kPEGraphicsMode,1);op(kPEAcquireScreen,0); }
    else if (scenario==15) op(kPETextMode,1);
    else start_text();
    switch (scenario) {
    case 1: /* The original disable/remap/enable failure. */
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(route==VIDEO && gc_acquired && gc_enabled);break;
    case 2: /* Ordinary remap of console-owned text. */
        op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(route==VIDEO && gc_acquired && gc_enabled);break;
    case 3: /* Paused, never released: normal enable remains a resume. */
        op(kPEDisableScreen,0);op(kPEEnableScreen,0);
        CHECK(route==VIDEO && gc_acquired && gc_enabled);break;
    case 4: /* Pending server-open: no fServerConnect/captured flag yet. */
        op(kPEReleaseScreen,0);before=enable_calls;
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(route==VIDEO && !gc_acquired && !gc_enabled && enable_calls==before);
        CHECK(!disableConsoleOutput); /* UART mirror survives. */
        op(kPEAcquireScreen,0);CHECK(gc_enabled && gc_acquired);break;
    case 5: /* Live owner: remap/enable/TextScreen cannot acquire ownership. */
        op(kPEReleaseScreen,0);before=enable_calls;
        op(kPEReleaseScreen,1);op(kPEEnableScreen,1);op(kPETextScreen,0);
        CHECK(!gc_acquired && !gc_enabled && enable_calls==before);
        op(kPEAcquireScreen,0);CHECK(gc_acquired && gc_enabled);break;
    case 6: /* Ownership release cancels a queued remap resume. */
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);
        op(kPEReleaseScreen,0);before=enable_calls;op(kPEEnableScreen,1);
        CHECK(!gc_acquired && !gc_enabled && enable_calls==before);break;
    case 7: /* Repeated remaps preserve one pending console resume. */
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);op(kPEReleaseScreen,1);
        op(kPEEnableScreen,1);CHECK(gc_enabled && gc_acquired && route==VIDEO);break;
    case 8: /* Repeated disables preserve one route; enable consumes it. */
        op(kPEDisableScreen,0);op(kPEDisableScreen,0);
        op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(route==VIDEO && gc_enabled);
        route=SERIAL;op(kPEEnableScreen,0);CHECK(route==SERIAL);break;
    case 9: /* pdtextboot=0 still suppresses automatic remap acquisition. */
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(!gc_enabled && !gc_acquired);
        op(kPEAcquireScreen,0);CHECK(gc_enabled);break;
    case 10: /* Graphics boot is not forced into text. */
        before=enable_calls;
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(gc_graphics_boot && !gc_enabled && enable_calls==before);break;
    case 11: /* An explicit graphics-mode change overrides an old text boot. */
        op(kPEReleaseScreen,0);op(kPEGraphicsMode,1);op(kPEAcquireScreen,0);
        before=enable_calls;op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(gc_graphics_boot && !gc_enabled && enable_calls==before);break;
    case 12: /* Deliberately serial-selected console is not forced to VC. */
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(route==SERIAL && !gc_enabled);break;
    case 13: /* Reopen after close: release remains effective without disable. */
        op(kPEReleaseScreen,0);op(kPEAcquireScreen,0);CHECK(gc_enabled);
        op(kPEReleaseScreen,0);before=enable_calls;op(kPEEnableScreen,1);
        CHECK(!gc_enabled && !gc_acquired && enable_calls==before);break;
    case 14: /* A NULL release followed by enable is not an ownership grant. */
        op(kPEReleaseScreen,0);before=enable_calls;op(kPEEnableScreen,0);
        CHECK(!gc_enabled && !gc_acquired && enable_calls==before);break;
    case 15: /* Remapping a console which was never acquired cannot claim it. */
        op(kPEReleaseScreen,1);op(kPEEnableScreen,1);
        CHECK(!gc_enabled && !gc_acquired);break;
    case 16: /* Changing mode cancels a queued text-remap resume. */
        op(kPEDisableScreen,0);op(kPEReleaseScreen,1);op(kPEGraphicsMode,1);
        before=enable_calls;op(kPEEnableScreen,1);
        CHECK(gc_graphics_boot && !gc_enabled && enable_calls==before);break;
    case 17: /* Explicit close/acquire restores the route even without enable. */
        op(kPEReleaseScreen,0);op(kPEDisableScreen,0);
        CHECK(route==SERIAL && !gc_enabled);
        op(kPEAcquireScreen,0);CHECK(route==VIDEO && gc_enabled && gc_acquired);break;
    default:return 2;
    }
    return 0;
}
'''


def harness(pe, vc):
    # Pixel mapping precedes this tail. Its behavior is outside these tests.
    control = body(vc, "initialize_screen")
    control = control[control.index("\tif (!gc_text_boot_set"):]
    declarations = vc[vc.index("static boolean_t gc_acquired"):vc.index("static vm_offset_t  lastVideoVirt")]
    return (PREAMBLE + declarations + "\nstatic void initialize_screen(PE_Video *boot_vinfo,unsigned int op)\n{\n"
            "boolean_t graphics_now;uint32_t delay;\n" + control +
            "\nint PE_initialize_console(PE_Video *info,int op)\n" + body(pe, "PE_initialize_console") + SCENARIOS)


class ConsoleResume(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="m1-console-resume-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.old = {path: pinned(path) for path in (PE, VC)}
        for path, source in cls.old.items():
            target = cls.directory / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(source)
        result = run(["git", "apply", "--no-index", str(PATCH)], cls.directory)
        if result.returncode:
            raise RuntimeError(result.stderr)
        cls.new = {path: (cls.directory / path).read_text() for path in (PE, VC)}
        cls.compiler = shutil.which("cc")
        if not cls.compiler:
            raise RuntimeError("Host C compiler required for extracted-control tests (no SDK build)")
        cls.fixed = cls.compile("fixed", cls.new)
        cls.original = cls.compile("original", cls.old)

    @classmethod
    def compile(cls, name, source):
        path = cls.directory / (name + ".c")
        path.write_text(harness(source[PE], source[VC]))
        executable = cls.directory / name
        result = run([cls.compiler, "-std=c11", "-O0", "-Wall", "-Werror",
                      "-Wno-unused-variable", "-Wno-unused-parameter",
                      "-U__x86_64__", "-U__i386__", str(path), "-o", str(executable)])
        if result.returncode:
            raise RuntimeError(result.stderr)
        return executable

    def test_exact_source_scope(self):
        self.assertEqual(set(re.findall(r"^\+\+\+ b/(.*)$", PATCH.read_text(), re.M)), {PE, VC})
        for path in (PE, VC):
            self.assertEqual((ROOT / "sources/puredarwin-m1-bringup" / path).read_text(), self.old[path],
                             "console source in isolated native candidate must remain unchanged")

    def test_seventeen_real_control_sequences(self):
        for scenario in range(1, 18):
            with self.subTest(scenario=scenario):
                result = run([str(self.fixed), str(scenario)])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_bug_and_ownership_hazards_are_reproduced(self):
        for scenario in (1, 8, 11, 14, 15, 17):
            with self.subTest(scenario=scenario):
                self.assertNotEqual(run([str(self.original), str(scenario)]).returncode, 0)

    def test_reordering_alone_would_paint_into_owned_framebuffer(self):
        naive = self.compile("naive-order-only", {PE: self.new[PE], VC: self.old[VC]})
        for scenario in (4, 5, 6, 13, 14):
            self.assertNotEqual(run([str(naive), str(scenario)]).returncode, 0)

    def test_latch_mutations_are_rejected(self):
        vc = self.new[VC]
        mutations = {
            "ignore-null-release": ("&& boot_vinfo &&", "&& TRUE &&", 6),
            "ignore-previous-ownership": ("(gc_acquired || gc_reacquire_text)", "TRUE", 5),
            "resume-graphics-as-text": ("gc_text_boot && !gc_graphics_boot &&", "gc_text_boot &&", 11),
            "unconditional-auto-acquire": ("gc_reacquire_text && !console_is_serial()", "gc_text_boot && !console_is_serial()", 14),
        }
        for name, (before, after, scenario) in mutations.items():
            with self.subTest(mutation=name):
                self.assertEqual(vc.count(before), 1)
                mutant = self.compile(name, {PE: self.new[PE], VC: vc.replace(before, after)})
                self.assertNotEqual(run([str(mutant), str(scenario)]).returncode, 0)

    def test_caller_ownership_contract_is_present_in_pin(self):
        iofb, pdgop = pinned(IOFB), pinned(PDGOP)
        opening = body(iofb, "IOFramebuffer::newUserClient")
        self.assertRegex(opening, r"case kIOFBServerConnectType:\s*if \(fServerConnect\)\s*err = kIOReturnExclusiveAccess;")
        self.assertLess(opening.index("setPlatformConsole(0, kPEReleaseScreen"), opening.index("err = open();"))
        self.assertIn("setPlatformConsole(&newConsole, kPEReleaseScreen", body(iofb, "IOFramebuffer::findConsole"))
        self.assertIn("setPlatformConsole(0, kPEAcquireScreen", body(iofb, "IOFramebuffer::close"))
        self.assertIn("IOServiceOpen(fb->service, mach_task_self(), 0, &fb->connect)", pdgop)
        closing = body(pdgop, "PDGOPClose")
        self.assertLess(closing.index("IOConnectUnmapMemory64"), closing.index("IOServiceClose"))
        # Capture policy is not the exclusive connection / console ownership token.
        self.assertNotIn("setPlatformConsole", body(iofb, "IOFramebuffer::setCaptured"))


if __name__ == "__main__":
    unittest.main()
