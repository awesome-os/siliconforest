#!/usr/bin/env python3
"""Host checks for the m1-wayland patches (SiliconForest e7aa00a).

sfgop-fail-closed.patch: the real patched SFGOP.c is compiled on the host
against hosttest/fake headers that mirror SiliconForest's SFIOKitLib.h signatures
and driven through success and short/invalid driver replies. The pinned
original is built the same way to show the defects it fixes.

iogop-vram-write-combine.patch: source checks of the opt-in gate, plus
source anchors for the cache-attribute rationale. Kernel code is not compiled
here; the target SDK build is root's.
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
SFGOP_PATCH = HERE / "sfgop-fail-closed.patch"
IOGOP_PATCH = HERE / "iogop-vram-write-combine.patch"
FB30_PATCH = REPO / "integrations" / "siliconforest" / "m1-display" / "framebuffer-30bit.patch"
HARNESS = HERE / "hosttest" / "sfgop_harness.c"
FAKE = HERE / "hosttest" / "fake"
SFGOP_C = "src/Libraries/SFGOP/SFGOP.c"
SFGOP_H = "src/Libraries/SFGOP/include/SFGOP.h"
IOGOP = "src/Kernel/Extensions/IOGOPFramebuffer/IOGOPFramebuffer.cpp"
XNU = "src/Kernel/xnu"
IOG = "src/Kernel/Extensions/IOGraphicsFamily"
KERN_NOT_SUPPORTED = 46


def git(*args, cwd=BASELINE):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def pinned(path):
    return git("show", f"{PIN}:{path}")


def patched_tree(paths, patches):
    tmp = Path(tempfile.mkdtemp(prefix="m1-wayland-"))
    git("init", "-q", cwd=tmp)
    for p in paths:
        (tmp / p).parent.mkdir(parents=True, exist_ok=True)
        (tmp / p).write_text(pinned(p))
    for patch in patches:
        git("apply", str(patch), cwd=tmp)
    return tmp


def build_and_run(sfgop_c_text, sfgop_h_text, pinned_mode, workdir):
    src = workdir / ("pinned" if pinned_mode else "patched")
    (src / "include").mkdir(parents=True, exist_ok=True)
    (src / "SFGOP.c").write_text(sfgop_c_text)
    (src / "include" / "SFGOP.h").write_text(sfgop_h_text)
    exe = src / "harness"
    cmd = ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-I", str(FAKE), "-I", str(src / "include"),
           str(src / "SFGOP.c"), str(HARNESS), "-o", str(exe)]
    if pinned_mode:
        cmd.insert(1, "-DPINNED")
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode != 0:
        raise AssertionError("harness build failed:\n" + build.stderr)
    out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    rows = {}
    for line in out.splitlines():
        name, *kv = line.split()
        rows[name] = dict(item.split("=", 1) for item in kv)
    return rows, build.stderr


FAILURES = {
    "short_scalar": "GetCurrentDisplayMode:short",
    "zero_scalar": "GetCurrentDisplayMode:short",
    "short_pixelinfo": "GetPixelInformation:short",
    "zero_width": "PixelInformation:invalid",
    "zero_bpp": "PixelInformation:invalid",
    "odd_bpp": "PixelInformation:invalid",
    "short_stride": "PixelInformation:invalid",
    "short_map": "MapVRAM:short",
    "null_map": "MapVRAM:short",
    "conforms_error": "ServiceKind",
    "no_service": "IOServiceGetMatchingService",
}


def sfgop_problems(rows):
    """Contract checks on harness output; returns a list of violations."""
    p = []
    ok = rows.get("ok_iogop_2101010", {})
    if ok.get("open_rc") != "0" or ok.get("kind") != "1" or ok.get("fmt") != "2":
        p.append(f"IOGOP 2101010 open/kind/fmt wrong: {ok}")
    if ok.get("present_rc") != str(KERN_NOT_SUPPORTED) or ok.get("sel11") != "0" or ok.get("other") != "0":
        p.append(f"direct-memory present must be KERN_NOT_SUPPORTED with no driver call: {ok}")
    if ok.get("after_close_kind") != "0":
        p.append("service kind must be unknown after close")
    vio = rows.get("ok_virtio_8888", {})
    if vio.get("open_rc") != "0" or vio.get("kind") != "2" or vio.get("fmt") != "1" or \
            vio.get("present_rc") != "0" or vio.get("sel11") != "1":
        p.append(f"virtio present path wrong: {vio}")
    unk = rows.get("ok_unknown_format", {})
    if unk.get("open_rc") != "0" or unk.get("fmt") != "0" or unk.get("present_rc") != str(KERN_NOT_SUPPORTED):
        p.append(f"16-bit framebuffer should open with unknown format: {unk}")
    for name, stage in FAILURES.items():
        r = rows.get(name, {})
        if r.get("open_rc") in (None, "0"):
            p.append(f"{name}: returned success")
            continue
        if r.get("stage") != stage or r.get("zeroed") != "1":
            p.append(f"{name}: stage {r.get('stage')} zeroed {r.get('zeroed')}")
        opened = name not in ("conforms_error", "no_service")
        if opened and (r.get("close") != "1" or r.get("release") != "1" or r.get("dealloc") != "1"):
            p.append(f"{name}: cleanup incomplete {r}")
        if r.get("unmap") != ("1" if name == "short_map" else "0"):
            p.append(f"{name}: unmap count {r.get('unmap')}")
    return p


@unittest.skipUnless((BASELINE / ".git").exists(), "pinned SiliconForest baseline checkout absent")
@unittest.skipUnless(shutil.which("cc"), "host C compiler absent")
class SFGOPContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = patched_tree([SFGOP_C, SFGOP_H], [SFGOP_PATCH])
        cls.new_c = (cls.tmp / SFGOP_C).read_text()
        cls.new_h = (cls.tmp / SFGOP_H).read_text()
        cls.rows, cls.warnings = build_and_run(cls.new_c, cls.new_h, False, cls.tmp)
        cls.old_rows, _ = build_and_run(pinned(SFGOP_C), pinned(SFGOP_H), True, cls.tmp)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_pin(self):
        self.assertEqual(git("rev-parse", "HEAD").strip(), PIN)

    def test_patch_touches_only_sfgop(self):
        self.assertEqual(set(re.findall(r"^\+\+\+ b/(\S+)", SFGOP_PATCH.read_text(), re.M)), {SFGOP_C, SFGOP_H})

    def test_struct_definition_byte_identical(self):
        def struct(text):
            return re.search(r"typedef struct SFGOPFramebuffer \{.*?\} SFGOPFramebuffer;", text, re.S).group(0)
        self.assertEqual(struct(self.new_h), struct(pinned(SFGOP_H)))

    def test_abi_64_bytes_unchanged(self):
        expect = {"sizeof": "64", "align": "8", "off_address": "16", "off_size": "24",
                  "off_width": "32", "off_bpp": "44", "off_masks": "52"}
        self.assertEqual(self.rows["abi"], expect)
        self.assertEqual(self.old_rows["abi"], expect)

    def test_uses_sf_three_argument_conforms(self):
        self.assertIn('IOObjectConformsTo(service, "IOVirtIOGPU", &virtio) != KERN_SUCCESS', self.new_c)
        self.assertIn("#ifdef _SF_IOKITLIB_H", self.new_c)

    def test_patched_build_is_warning_free(self):
        self.assertEqual(self.warnings.strip(), "")

    def test_contract(self):
        self.assertEqual(sfgop_problems(self.rows), [])

    def test_pinned_defects_are_real(self):
        o = self.old_rows
        self.assertEqual(o["short_scalar"]["open_rc"], "0", "pinned returns success on a short scalar reply")
        self.assertEqual(o["short_scalar"]["zeroed"], "1", "...with a zeroed framebuffer")
        self.assertEqual(o["short_pixelinfo"]["open_rc"], "0", "pinned accepts a short IOPixelInformation")
        self.assertEqual(o["ok_iogop_2101010"]["sel11"], "1", "pinned sends selector 11 to an IOFramebuffer")
        self.assertTrue(sfgop_problems(o))

    def test_mutations_are_caught(self):
        c = self.new_c
        mutants = {
            "short scalar accepted": c.replace("    if (scalarOutCnt < 2) {", "    if (0) {"),
            "short pixel info accepted": c.replace("if (pixelInfoSize != sizeof(pixelInfo)) {", "if (0) {"),
            "geometry unchecked": c.replace("if (!sfgop_geometry_valid(&pixelInfo)) {", "if (0) {"),
            "map size unchecked": c.replace("    if (fb->address == 0 ||\n", "    if (0 &&\n"),
            "present to any service": c.replace("if (SFGOPGetServiceKind(fb) != kSFGOPServiceVirtioTransfer)",
                                                "if (0)"),
            "kind error tolerated": c.replace("if (SFGOPGetServiceKind(fb) == kSFGOPServiceUnknown) {", "if (0) {"),
            "cache not dropped": c.replace("            gSFGOPKindService = MACH_PORT_NULL;\n", ""),
        }
        for label, text in mutants.items():
            with self.subTest(label):
                self.assertNotEqual(text, c, f"mutation did not apply: {label}")
                work = self.tmp / ("mut_" + re.sub(r"\W+", "_", label))
                rows, _ = build_and_run(text, self.new_h, False, work)
                self.assertTrue(sfgop_problems(rows), f"mutant not caught: {label}")


def iogop_problems(text):
    p = []
    gate = re.search(r"static bool\s*\ngop_apple_soc_firmware_fb\(IOService \*provider\)\s*\{(.*?)\n\}", text, re.S)
    if gate is None:
        return ["gate function missing"]
    g = gate.group(1)
    if "OSDynamicCast(IOPCIDevice, provider) != NULL" not in g or "return false;" not in g:
        p.append("gate does not exclude PCI framebuffers")
    if 'fromPath("/", gIODTPlane)' not in g or '"AppleARM"' not in g:
        p.append("gate does not require an AppleARM device tree root")
    if "root->release();" not in g:
        p.append("gate leaks the root entry")
    start = re.search(r"^IOGOPFramebuffer::start\(IOService \*provider\)\s*\{(.*?)^\}", text, re.S | re.M)
    if start is None:
        return p + ["start missing"]
    s = start.group(1)
    block = re.search(r"if \(gop_apple_soc_firmware_fb\(provider\)\) \{(.*?)\n    \}", s, re.S)
    if block is None:
        p.append("opt-in not gated")
    else:
        b = block.group(1)
        if 'setProperty("sf-vram-write-combine", kOSBooleanTrue);' not in b:
            p.append("property not set exactly")
        if "gFb0CacheMode = kIOMapWriteCombineCache;" not in b:
            p.append("/dev/fb0 mapping not write-combined under the gate")
    if 'setProperty("sf-vram-write-combine"' in s.replace(block.group(0) if block else "", ""):
        p.append("property set outside the gate")
    order = [s.find(x) for x in ("gop_apple_soc_firmware_fb(provider)", "fb0_publish(", "registerService();")]
    if -1 in order or order != sorted(order):
        p.append("opt-in must precede fb0_publish and registerService")
    if "static IOOptionBits gFb0CacheMode = kIOGOPFBMapCacheMode;" not in text:
        p.append("non-Apple /dev/fb0 default changed")
    if "gFb0Memory->map(kIOMapAnywhere | gFb0CacheMode)" not in text:
        p.append("/dev/fb0 map ignores gFb0CacheMode")
    return p


@unittest.skipUnless((BASELINE / ".git").exists(), "pinned SiliconForest baseline checkout absent")
class IOGOPWriteCombine(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = patched_tree([IOGOP], [IOGOP_PATCH])
        cls.new = (cls.tmp / IOGOP).read_text()
        cls.stacked = patched_tree([IOGOP], [FB30_PATCH, IOGOP_PATCH])

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)
        shutil.rmtree(cls.stacked, ignore_errors=True)

    def test_touches_only_iogop(self):
        self.assertEqual(set(re.findall(r"^\+\+\+ b/(\S+)", IOGOP_PATCH.read_text(), re.M)), {IOGOP})

    def test_stacks_on_frozen_30bit_patch(self):
        text = (self.stacked / IOGOP).read_text()
        self.assertIn("ret = kIO30BitDirectPixels;", text)
        self.assertEqual(iogop_problems(text), [])

    def test_gate_and_placement(self):
        self.assertEqual(iogop_problems(self.new), [])

    def test_property_name_matches_the_reader(self):
        uc = pinned(f"{IOG}/IOFramebufferUserClient.cpp")
        self.assertEqual(uc.count('fOwner->getProperty("sf-vram-write-combine")'), 2)
        self.assertRegex(uc, r'#else\s*\n\s*//[^\n]*\n\s*if \(fOwner->getProperty\("sf-vram-write-combine"\)\) \*flags \|= kIOMapWriteCombineCache;')

    def test_rationale_source_anchors(self):
        """Facts the cache-attribute rationale depends on, at the pin."""
        md = pinned(f"{XNU}/iokit/Kernel/IOMemoryDescriptor.cpp")
        self.assertIn("[kIOInhibitCache] = {VM_WIMG_IO, MAP_MEM_IO}", md)
        self.assertIn("[kIOWriteCombineCache] = {VM_WIMG_WCOMB, MAP_MEM_WCOMB}", md)
        self.assertIn("cacheModeForPagerFlags(IODefaultCacheBits(nextAddr))", md)
        rpc = pinned(f"{XNU}/osfmk/device/iokit_rpc.c")
        self.assertRegex(rpc, r"IODefaultCacheBits\(addr64_t pa\)\s*\{\s*return pmap_cache_attributes")
        pm = pinned(f"{XNU}/osfmk/arm/pmap/pmap.c")
        self.assertRegex(pm, r"return \(io_rgn == NULL\) \? VM_WIMG_IO : io_rgn->wimg;")
        self.assertRegex(pm, r"(?s)case VM_WIMG_IO:.*?if \(is_dram_addr\(pa\)\) \{\s*pte = ARM_PTE_ATTRINDX\(CACHE_ATTRINDX_POSTED_COMBINED_REORDERED\);", )
        self.assertRegex(pm, r"case VM_WIMG_WCOMB:\s*if \(is_dram_addr\(pa\)\) \{\s*pte = ARM_PTE_ATTRINDX\(CACHE_ATTRINDX_WRITECOMB\);")
        prc = pinned(f"{XNU}/osfmk/arm64/proc_reg.h")
        self.assertIn("#define MAIR_WRITECOMB                    0x40 /* Normal Memory, Non-Cacheable", prc)
        self.assertRegex(prc, r"MAIR_POSTED_COMBINED_REORDERED\s+0x0D /\* Device Memory, GRE")
        vc = pinned(f"{XNU}/osfmk/console/video_console.c")
        self.assertRegex(vc, r"(?s)#if HAS_UCNORMAL_MEM \|\| APPLEVIRTUALPLATFORM.*?unsigned int flags = VM_WIMG_WCOMB;")
        fb = pinned(f"{IOG}/IOFramebuffer.cpp")
        self.assertRegex(fb, r"#else\s*\nenum \{ kIOFBMapCacheMode = kIOMapInhibitCache \};")
        self.assertIn("#define is_dram_addr(addr)      (((uint64_t)(addr) - gDramBase) < gDramSize)",
                      pinned(f"{XNU}/osfmk/mach/arm/vm_param.h"))

    def test_mutations_are_caught(self):
        n = self.new
        mutants = {
            "PCI included": n.replace("    if (OSDynamicCast(IOPCIDevice, provider) != NULL)\n        return false;\n", ""),
            "misspelled property": n.replace('setProperty("sf-vram-write-combine"', 'setProperty("sf-vram-writecombine"'),
            "ungated": n.replace("if (gop_apple_soc_firmware_fb(provider)) {", "if (true) {"),
            "after registerService": n.replace("    fb0_publish((uint8_t *)fbBase, (size_t)pitch * height);\n",
                                               "").replace("    registerService();\n    waitForDrm(provider);",
                                               "    registerService();\n    fb0_publish((uint8_t *)fbBase, (size_t)pitch * height);\n    waitForDrm(provider);"),
            "global default flipped": n.replace("static IOOptionBits gFb0CacheMode = kIOGOPFBMapCacheMode;",
                                                "static IOOptionBits gFb0CacheMode = kIOMapWriteCombineCache;"),
        }
        for label, text in mutants.items():
            with self.subTest(label):
                self.assertNotEqual(text, n, f"mutation did not apply: {label}")
                self.assertTrue(iogop_problems(text), f"mutant not caught: {label}")


if __name__ == "__main__":
    unittest.main()
