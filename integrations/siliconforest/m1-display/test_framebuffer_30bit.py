#!/usr/bin/env python3
"""Source checks for framebuffer-30bit.patch (IOGOPFramebuffer, pinned e7aa00a).

The Apple firmware boot framebuffer on M1 reports boot-args depth 30: 10-bit
RGB in a 32-bit native (little-endian) word. Unpatched IOGOPFramebuffer falls
into its 8-bit indexed default, so IOFramebuffer hands the kernel console
depth 8 at the console handoff. These checks read pinned sources and a
temporary patched copy; they do not compile, load or boot anything.
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
BRINGUP = REPO / "sources" / "puredarwin-m1-bringup"
M1N1 = REPO / "sources" / "m1n1-baseline"
PIN = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"
M1N1_PIN = "121fcbf35d4501814b58ca5347926a4173b6a512"
PATCH = HERE / "framebuffer-30bit.patch"
IOGOP = "src/Kernel/Extensions/IOGOPFramebuffer/IOGOPFramebuffer.cpp"
TYPES = "src/Kernel/Extensions/IOGraphicsFamily/IOKit/graphics/IOGraphicsTypes.h"
IOFB = "src/Kernel/Extensions/IOGraphicsFamily/IOFramebuffer.cpp"
VC = "src/Kernel/xnu/osfmk/console/video_console.c"
MASKS = (0x3FF00000, 0x000FFC00, 0x000003FF)


def git(*args, cwd=BASELINE):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def pinned(path):
    return git("show", f"{PIN}:{path}")


def function_body(text, qualified):
    m = re.search(r"^" + re.escape(qualified) + r"\([^)]*\)\s*\n\{", text, re.M | re.S)
    if m is None:
        m = re.search(r"^" + re.escape(qualified) + r"\([^{]*?\)\s*\n\{", text, re.M | re.S)
    if m is None:
        return None
    depth = 0
    for j in range(m.end() - 1, len(text)):
        depth += {"{": 1, "}": -1}.get(text[j], 0)
        if depth == 0:
            return text[m.end():j]
    return None


def case_block(body, value):
    """Statements under 'case <value>:' up to the next break."""
    m = re.search(r"case\s+%d\s*:(.*?)break\s*;" % value, body or "", re.S)
    return m.group(1) if m else None


def pixel_info(block):
    """Parse the field assignments of a getPixelInformation case block."""
    if block is None:
        return None
    fmt = re.search(r"strlcpy\(info->pixelFormat,\s*(\w+)", block)
    out = {"pixelFormat": fmt.group(1) if fmt else None}
    for field in ("pixelType", "bitsPerPixel", "componentCount", "bitsPerComponent"):
        m = re.search(r"info->%s\s*=\s*(\w+)\s*;" % field, block)
        out[field] = m.group(1) if m else None
    out["masks"] = tuple(int(v, 16) for _, v in sorted(
        re.findall(r"info->componentMasks\[(\d)\]\s*=\s*(0x[0-9a-fA-F]+)", block)))
    return out


def iofb_console_depth(info):
    """IOFramebuffer.cpp: componentCount*bitsPerComponent if bpc > 8, else bitsPerPixel."""
    bpc, count, bpp = int(info["bitsPerComponent"]), int(info["componentCount"]), int(info["bitsPerPixel"])
    return count * bpc if bpc > 8 else bpp


def check_patched(text):
    problems = []
    info = pixel_info(case_block(function_body(text, "IOGOPFramebuffer::getPixelInformation"), 30))
    if info is None:
        return ["getPixelInformation has no case 30"]
    expect = {"pixelFormat": "kIO30BitDirectPixels", "pixelType": "kIORGBDirectPixels",
              "bitsPerPixel": "32", "componentCount": "3", "bitsPerComponent": "10"}
    for k, v in expect.items():
        if info[k] != v:
            problems.append(f"getPixelInformation case 30 {k} = {info[k]}, expected {v}")
    if info["masks"] != MASKS:
        problems.append(f"case 30 masks {tuple(hex(m) for m in info['masks'])}")
    if info["bitsPerComponent"] and info["bitsPerPixel"] and info["componentCount"] and iofb_console_depth(info) != 30:
        problems.append("IOFramebuffer console depth would not be 30")
    fmts = case_block(function_body(text, "IOGOPFramebuffer::getPixelFormats"), 30)
    if fmts is None or not re.search(r"ret\s*=\s*kIO30BitDirectPixels\s*;", fmts):
        problems.append("getPixelFormats case 30 does not return kIO30BitDirectPixels")
    return problems


@unittest.skipUnless((BASELINE / ".git").exists(), "pinned PureDarwin baseline checkout absent")
class Framebuffer30Bit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.old = pinned(IOGOP)
        tmp = Path(tempfile.mkdtemp(prefix="m1-fb30-"))
        try:
            git("init", "-q", cwd=tmp)
            (tmp / IOGOP).parent.mkdir(parents=True)
            (tmp / IOGOP).write_text(cls.old)
            cls.check = subprocess.run(["git", "apply", "--check", str(PATCH)], cwd=tmp,
                                       capture_output=True, text=True)
            if cls.check.returncode == 0:
                git("apply", str(PATCH), cwd=tmp)
            cls.new = (tmp / IOGOP).read_text()
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_pin(self):
        self.assertEqual(git("rev-parse", "HEAD").strip(), PIN)

    def test_applies_and_touches_only_iogop(self):
        self.assertEqual(self.check.returncode, 0, self.check.stderr)
        self.assertEqual(set(re.findall(r"^\+\+\+ b/(\S+)", PATCH.read_text(), re.M)), {IOGOP})
        removed = [l for l in PATCH.read_text().splitlines() if l.startswith("-") and not l.startswith("---")]
        self.assertEqual(removed, [], "patch must only add the depth-30 cases")

    def test_unpatched_bug_is_real(self):
        # Depth 30 hits the 8-bit indexed default today => console depth 8.
        body = function_body(self.old, "IOGOPFramebuffer::getPixelInformation")
        self.assertIsNone(case_block(body, 30))
        default = re.search(r"case 8:\s*default:(.*?)break;", body, re.S).group(1)
        info = pixel_info(default)
        self.assertEqual(info["pixelFormat"], "IO8BitIndexedPixels")
        self.assertEqual(iofb_console_depth(info), 8)

    def test_patched_description(self):
        self.assertEqual(check_patched(self.new), [])

    def test_other_depths_unchanged(self):
        for fn in ("IOGOPFramebuffer::getPixelInformation", "IOGOPFramebuffer::getPixelFormats"):
            old_b, new_b = function_body(self.old, fn), function_body(self.new, fn)
            for depth in (8, 15, 16, 24, 32):
                self.assertEqual(case_block(old_b, depth), case_block(new_b, depth), f"{fn} case {depth}")

    def test_iographics_defines_30bit_encoding(self):
        types = pinned(TYPES)
        m = re.search(r'#define kIO30BitDirectPixels\s+"([^"]+)"', types)
        self.assertIsNotNone(m)
        enc = m.group(1)
        self.assertEqual(len(enc), 32)
        # MSB-first encoding of the native 32-bit word => masks
        for ch, mask in zip("RGB", MASKS):
            bits = sum(1 << (31 - i) for i, c in enumerate(enc) if c == ch)
            self.assertEqual(bits, mask, ch)

    def test_iofb_console_depth_rule_matches_source(self):
        src = pinned(IOFB)
        self.assertRegex(src, r"if \(info->bitsPerComponent > 8\)\s*__private->consoleDepth = info->componentCount \* info->bitsPerComponent;")

    def test_xnu_video_console_paints_this_layout(self):
        vc = pinned(VC)
        table = re.search(r"vc_color_index_table\[33\]\s*=\s*\{([^}]*)\}", vc).group(1)
        idx = [int(x) for x in re.findall(r"\d+", table)]
        self.assertEqual(idx[30], 3)
        colors = re.search(r"vc_colors\[8\]\[4\]\s*=\s*\{(.*?)\};", vc, re.S).group(1)
        rows = [[int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", r)] for r in re.findall(r"\{([^}]*)\}", colors)
                if not r.strip().startswith("//")]
        red, green, blue = rows[1][3], rows[2][3], rows[4][3]
        self.assertEqual((red, green, blue), MASKS)
        self.assertRegex(vc, r"case 30:\s*case 32:\s*vc_paint_char_32")

    @unittest.skipUnless((M1N1 / ".git").exists(), "pinned m1n1 checkout absent")
    def test_firmware_layout_from_m1n1(self):
        self.assertEqual(git("rev-parse", "HEAD", cwd=M1N1).strip(), M1N1_PIN)
        fb = git("show", f"{M1N1_PIN}:src/fb.c", cwd=M1N1)
        m = re.search(r"rgb2pixel_30\(rgb_t c\)\s*\{\s*return \(c\.b << (\d+)\) \| \(c\.g << (\d+)\) \| \(c\.r << (\d+)\);", fb)
        self.assertIsNotNone(m)
        b, g, r = (int(x) for x in m.groups())
        # 8-bit channels land in the top 8 bits of each 10-bit field
        self.assertEqual(((0xFF << r) | (0x3 << (r - 2))), MASKS[0])
        self.assertEqual(((0xFF << g) | (0x3 << (g - 2))), MASKS[1])
        self.assertEqual(((0xFF << b) | (0x3 << (b - 2))), MASKS[2])

    @unittest.skipUnless((BRINGUP / IOGOP).exists(), "isolated bringup tree absent")
    def test_bringup_tree_iogop_is_pinned(self):
        self.assertEqual((BRINGUP / IOGOP).read_text(), self.old)

    def test_mutations_are_caught(self):
        n = self.new
        mutants = {
            "8-bit components": n.replace("info->bitsPerComponent      = 10;", "info->bitsPerComponent      = 8;"),
            "30-bit storage": n.replace("info->componentMasks[2] = 0x000003ff;\n            info->bitsPerPixel  = 32;",
                                        "info->componentMasks[2] = 0x000003ff;\n            info->bitsPerPixel  = 30;"),
            "swapped masks": n.replace("componentMasks[0] = 0x3ff00000", "componentMasks[0] = 0x000003fX")
                              .replace("componentMasks[2] = 0x000003ff", "componentMasks[2] = 0x3ff00000")
                              .replace("componentMasks[0] = 0x000003fX", "componentMasks[0] = 0x000003ff"),
            "forced 32-bit format": n.replace("strlcpy(info->pixelFormat, kIO30BitDirectPixels",
                                              "strlcpy(info->pixelFormat, IO32BitDirectPixels"),
            "missing getPixelFormats case": n.replace("ret = kIO30BitDirectPixels;", "ret = IO8BitIndexedPixels;"),
            "CLUT type": n.replace("info->pixelType     = kIORGBDirectPixels;\n            info->componentMasks[0] = 0x3ff00000;",
                                   "info->pixelType     = kIOCLUTPixels;\n            info->componentMasks[0] = 0x3ff00000;"),
        }
        for label, text in mutants.items():
            with self.subTest(label):
                self.assertNotEqual(text, n, f"mutation did not apply: {label}")
                self.assertTrue(check_patched(text), f"mutant not caught: {label}")


if __name__ == "__main__":
    unittest.main()
