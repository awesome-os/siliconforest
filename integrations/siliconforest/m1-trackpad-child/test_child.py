#!/usr/bin/env python3
"""Source-aware host tests. Fake IOKit is NOT ABI, SDK, device or input proof."""
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
LAB = HERE.parents[2]
ADAPTER = HERE / "adapter"
KEYBOARD = LAB / "integrations/siliconforest/m1-keyboard"
ENUM = LAB / "integrations/siliconforest/m1-trackpad-enum"


def body(source, name):
    """Extract a definition verbatim, including its return type."""
    at = source.index(name + "(")
    start = source.rfind("\n", 0, at) + 1
    opening = source.index("{", at)
    # Replace comments/literals with equal-length spaces before balancing.
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m[0]), source)
    depth = 0
    for end in range(opening, len(masked)):
        depth += (masked[end] == "{") - (masked[end] == "}")
        if not depth:
            return source[start:end + 1] + "\n"
    raise AssertionError(name)


def command(args, *, expect=0):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                            timeout=45, env=env)
    if expect is not None and result.returncode != expect:
        raise AssertionError(f"{args}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


def stage(tmp, mutant=None):
    tmp = Path(tmp)
    for path in ADAPTER.iterdir():
        if path.is_file(): shutil.copyfile(path, tmp / path.name)
    shutil.copyfile(HERE / "child_state.h", tmp / "child_state.h")
    for name in ("observation_layout.h",): shutil.copyfile(ENUM / name, tmp / name)
    shutil.copyfile(ENUM / "core/m1tp_enum.h", tmp / "m1tp_enum.h")
    shutil.copyfile(HERE / "tests/fake_iokit.h", tmp / "fake_iokit.h")
    for name in ("IOHIDDevice.h", "SFM1KeyboardHW.h", "IOKit/IOCommandGate.h",
                 "IOKit/IOTimerEventSource.h", "IOKit/IOWorkLoop.h",
                 "IOKit/IOBufferMemoryDescriptor.h", "IOKit/IOLib.h",
                 "libkern/libkern.h", "libkern/c++/OSData.h"):
        path = tmp / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "fake_iokit.h"\n')
    reference = json.loads((HERE / "descriptor-reference.json").read_text())
    path = LAB / reference["generated_header"]
    if path.exists():
        raw = path.read_bytes()
        assert hashlib.sha256(raw).hexdigest() == reference["generated_sha256"]
        (tmp / "trackpad_reference.h").write_bytes(raw)
    else:
        # Explicit software-only equality fixture; never an SDK input.
        (tmp / "trackpad_reference.h").write_text(
            "static const uint8_t dl_m1_tp_descriptor_reference[110]={1,2,3};\n")
    source = (tmp / "SFM1SPIHID.cpp").read_text()
    inc = (tmp / "SFM1SPIHIDChild.inc").read_text()
    policy = (tmp / "m1tp_child_policy.c").read_text()
    child = (tmp / "SFM1SPITrackpad.cpp").read_text()
    if mutant == "parent_staging":
        old = '!setProperty(kIOHIDRegisterServiceKey,kOSBooleanFalse) ||\n            getProperty(kIOHIDRegisterServiceKey)!=kOSBooleanFalse || '
        assert old in source; source = source.replace(old, "", 1)
    elif mutant == "parent_stop":
        original = body(source, "SFM1SPIHID::stop")
        source = source.replace(original.rstrip(), original.replace("if (t2Committed())", "if (false)", 1).rstrip(), 1)
    elif mutant in ("parent_dispose", "parent_free"):
        method = "disposeTransport" if mutant == "parent_dispose" else "free"
        original = body(source, "SFM1SPIHID::" + method)
        source = source.replace(original.rstrip(), original.replace("if (t2Committed())", "if (false)", 1).rstrip(), 1)
    elif mutant == "child_stop":
        original = body(child, "SFM1SPITrackpad::stop")
        child = child.replace(original.rstrip(), original.replace("if (committed())", "if (false)", 1).rstrip(), 1)
    elif mutant == "legacy_open":
        inc = inc.replace("if (t2Enabled && (", "if ((", 1)
    elif mutant == "length_minimum":
        policy = policy.replace("payload_length==expected", "payload_length>=expected")
    (tmp / "SFM1SPIHIDChild.inc").write_text(inc)
    (tmp / "m1tp_child_policy.c").write_text(policy)
    (tmp / "SFM1SPITrackpad.cpp").write_text(child)
    names = ["init", "probe", "getWorkLoop", "start", "poll", "publishCounters", "fail",
             "stopGated", "quiesce", "handleStop", "stop", "disposeTransport", "free",
             "terminate", "newReportDescriptor", "newTransportString", "newProductString",
             "newVendorIDNumber", "newProductIDNumber", "newVersionNumber",
             "newLocationIDNumber", "newCountryCodeNumber"]
    unit = '''#include "fake_iokit.h"
#define private public
#include "SFM1SPIHID.h"
#include "SFM1SPITrackpad.h"
#undef private
#include "m1tp_child_policy.h"
#include "trackpad_reference.h"
#define super IOHIDDevice
static volatile UInt32 gSFM1KeyboardProbeClaimed=0;
'''
    unit += body(source, "recordBytes")
    unit += "\n".join(body(source, "SFM1SPIHID::" + n) for n in names)
    unit += '\n#include "SFM1SPIHIDChild.inc"\n#include "SFM1SPITrackpad.cpp"\n'
    unit += (HERE / "tests/lifecycle_main.cpp").read_text()
    (tmp / "lifecycle.cpp").write_text(unit)
    return tmp


def compile_objects(tmp):
    includes = ["-I" + str(tmp), "-I" + str(KEYBOARD)]
    flags = ["-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g", "-fsanitize=undefined"]
    objects = []
    for src in (tmp / "m1tp_child_policy.c", KEYBOARD / "native_protocol/dl_spi_hid.c",
                KEYBOARD / "m1kbd_hid.c"):
        out = tmp / (src.stem + ".o")
        command(["cc", "-std=c11", *flags, *includes, "-c", src, "-o", out]);objects.append(out)
    return flags, includes, objects


class ChildTests(unittest.TestCase):
    def test_real_lifecycle_bodies(self):
        with tempfile.TemporaryDirectory(prefix="t2-host-") as d:
            t = stage(d); flags, includes, objects = compile_objects(t)
            command(["c++", "-std=c++14", *flags, *includes, t / "lifecycle.cpp", *objects, "-o", t / "lifecycle"])
            result = command([t / "lifecycle"])
            self.assertIn("HOST ONLY PASS", result.stdout)
            print(result.stdout.strip())

    def test_policy_and_real_fragment_engine(self):
        with tempfile.TemporaryDirectory(prefix="t2-wire-") as d:
            t = stage(d);flags, includes, objects = compile_objects(t)
            command(["cc", "-std=c11", *flags, *includes, HERE / "tests/policy_main.c", *objects, "-o", t / "policy"])
            self.assertIn("HOST ONLY PASS", command([t / "policy"]).stdout)

    def test_assertions_cannot_be_compiled_out(self):
        with tempfile.TemporaryDirectory(prefix="t2-ndebug-") as d:
            t = stage(d)
            r = command(["cc", "-std=c11", "-DNDEBUG", "-I" + str(t), "-I" + str(KEYBOARD),
                         "-c", HERE / "tests/policy_main.c", "-o", t / "bad.o"], expect=None)
            self.assertNotEqual(r.returncode, 0);self.assertIn("NDEBUG forbidden", r.stderr)
            r = command(["c++", "-std=c++14", "-DNDEBUG", "-I" + str(t), "-I" + str(KEYBOARD),
                         "-c", t / "lifecycle.cpp", "-o", t / "bad-cpp.o"], expect=None)
            self.assertNotEqual(r.returncode, 0);self.assertIn("NDEBUG forbidden", r.stderr)

    def test_lifecycle_and_report_mutants(self):
        for mutant in ("parent_staging", "parent_stop", "parent_dispose", "parent_free",
                       "child_stop", "legacy_open", "length_minimum"):
            with self.subTest(mutant=mutant), tempfile.TemporaryDirectory(prefix="t2-mutant-") as d:
                t = stage(d, mutant);flags, includes, objects = compile_objects(t)
                policy = mutant == "length_minimum"
                command(["cc" if policy else "c++", "-std=c11" if policy else "-std=c++14",
                         *flags, *includes, HERE / "tests/policy_main.c" if policy else t / "lifecycle.cpp",
                         *objects, "-o", t / "mutant"])
                r = command([t / "mutant"], expect=None)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn("Assertion", r.stderr)

    def test_passive_source_and_frozen_transport_fidelity(self):
        source = (ADAPTER / "SFM1SPIHID.cpp").read_text()
        child = (ADAPTER / "SFM1SPITrackpad.cpp").read_text()
        inc = (ADAPTER / "SFM1SPIHIDChild.inc").read_text()
        old = (ENUM / "adapter/SFM1SPIHID.cpp").read_text()
        for name in ("startHardware", "enumerate", "enumerateTrackpad", "waitResponse", "receive",
                     "preserveTrackpadEnumeration"):
            self.assertEqual(body(source, "SFM1SPIHID::" + name), body(old, "SFM1SPIHID::" + name))
        # Ignore prose: these are forbidden executable call/override paths.
        code = re.sub(r"/\*[\s\S]*?\*/|//[^\n]*", "", child + inc)
        self.assertNotRegex(code, r"\b(?:waitResponse|dlsp_request|handleReportWithTimeAsync|commandSleep|IOSleep|IOLockSleep)\s*\(")
        self.assertNotIn("SFM1SPITrackpad::getWorkLoop", child)
        self.assertNotRegex(code, r"(?:memcpy|bcopy)\([^;]*dl_m1_tp_descriptor_reference")
        self.assertIn("m1tp_child_descriptor_equal(trackpadEnumeration.descriptor", inc)
        self.assertIn("initWithSnapshot(trackpadEnumeration.descriptor", inc)
        for name in ("stop", "handleStop", "free"):
            self.assertIn("committed()", body(child, "SFM1SPITrackpad::" + name))


if __name__ == "__main__": unittest.main(verbosity=2)
