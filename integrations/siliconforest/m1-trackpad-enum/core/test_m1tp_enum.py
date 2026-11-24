#!/usr/bin/env python3
"""Host build, seeded checks are in test_m1tp_enum.c. This runs mutants."""

import ctypes
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[3]
LINUX = LAB / "sources/linux-aurora-keyboard-reference/drivers/hid/spi-hid/spi-hid-apple-core.c"
KBD = LAB / "integrations/puredarwin/m1-keyboard"
LINUX_SHA = "9c18211ef16e1c4ff5bf2c8025d24de649092148e3e181b91948cd98a0c3a595"
KBD_SHA = {
    "m1kbd_hid.c": "c4b9248c1b740d66bc6e39b2106ec3c67cb7e2d0e034d03386b6e920d34dddee",
    "m1kbd_hid.h": "0badc44929aee8ed18949684bc17cc51f6e81ebc06650f06d2f64ccd0dc36c85",
    "native_protocol/dl_spi_hid.h": "c58d84b1965812b65210d0f0c794b577a6dcfecef1a5dc449e2ae1fc6fe5d5ec",
    "native_protocol/dl_spi_hid.c": "88f2ae23fc527affa440fe5ee7309ac9b566de6c842cdb3e0d03dd054bf53103",
}
CC = ["cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O1", "-I" + str(HERE)]
FREE = ["-fno-stack-protector", "-fno-builtin", "-ffreestanding",
        "-fno-tree-loop-distribute-patterns"]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Info(ctypes.Structure):
    _fields_ = [
        ("max_input", ctypes.c_uint16),
        ("max_output", ctypes.c_uint16),
        ("max_control", ctypes.c_uint16),
        ("interface", ctypes.c_uint8),
        ("country", ctypes.c_uint8),
    ]


def bind(lib):
    lib.m1tp_claim_request.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint8, ctypes.c_uint8]
    lib.m1tp_claim_request.restype = ctypes.c_int
    lib.m1tp_interface_info.argtypes = [
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(Info)]
    lib.m1tp_interface_info.restype = ctypes.c_int
    lib.m1tp_copy_descriptor.argtypes = [
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint32),
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
    lib.m1tp_copy_descriptor.restype = ctypes.c_int
    return lib


def build(source, output):
    subprocess.check_call(CC + FREE + ["-fPIC", "-shared", str(source), "-o", str(output)])
    return bind(ctypes.CDLL(str(output)))


def header(iface, country, input_len, output, control):
    raw = bytearray(16)
    raw[1] = iface
    raw[5] = country
    raw[6] = input_len & 0xFF
    raw[7] = (input_len >> 8) & 0xFF
    raw[8] = output & 0xFF
    raw[9] = (output >> 8) & 0xFF
    raw[10] = control & 0xFF
    raw[11] = (control >> 8) & 0xFF
    raw[12] = 16
    raw[14] = 8
    return raw


def call_info(lib, raw, n):
    buf = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
    info = Info()
    info.max_input = 0x5A5A
    info.max_output = 0x5A5A
    info.max_control = 0x5A5A
    info.interface = 0x5A
    info.country = 0x5A
    rc = lib.m1tp_interface_info(buf, n, ctypes.byref(info))
    return rc, info


class Mutants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="m1tp-enum-")
        cls.lib = build(HERE / "m1tp_enum.c", Path(cls.tmp.name) / "m1tp.so")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_semantic_mutants(self):
        text = (HERE / "m1tp_enum.c").read_text(encoding="utf-8")
        self.assertEqual(text.count("~31u"), 1)
        self.assertEqual(text.count("payload[1] != 2u"), 1)
        self.assertEqual(text.count("input < 1u"), 1)
        self.assertNotIn("input > 2038u", text)
        self.assertEqual(text.count("n > 512u"), 1)
        self.assertEqual(text.count("n > 2038u"), 1)
        mask = build(self.write("mask.c", text.replace("~31u", "~7u", 1)), Path(self.tmp.name) / "mask.so")
        iface = build(self.write("iface.c", text.replace("payload[1] != 2u", "payload[1] != 1u", 1)),
                      Path(self.tmp.name) / "iface.so")
        capped = build(self.write("input.c", text.replace(
            "input < 1u", "input < 1u || input > 2038u", 1)), Path(self.tmp.name) / "input.so")
        opened = build(self.write("zero.c", text.replace(
            "if (input < 1u) return M1TP_ERR_INPUT;",
            "if (input < 1u && input > 1u) return M1TP_ERR_INPUT;", 1)),
                       Path(self.tmp.name) / "zero.so")
        desc = build(self.write("desc.c", text.replace("n > 512u", "n > 513u", 1)),
                     Path(self.tmp.name) / "desc.so")

        used = ctypes.c_uint8(8)
        self.assertEqual(self.lib.m1tp_claim_request(ctypes.byref(used), 0x10, 0x02), 1)
        self.assertEqual(used.value, 24)
        used = ctypes.c_uint8(8)
        self.assertEqual(mask.m1tp_claim_request(ctypes.byref(used), 0x10, 0x02), 0)
        self.assertEqual(used.value, 8)
        used = ctypes.c_uint8(32)
        self.assertEqual(self.lib.m1tp_claim_request(ctypes.byref(used), 0x01, 0xD0), 0)
        self.assertEqual(mask.m1tp_claim_request(ctypes.byref(used), 0x01, 0xD0), 0)
        self.assertEqual(used.value, 32)

        good = header(2, 33, 1, 0, 4086)
        wrong = header(1, 33, 1, 0, 4086)
        other = header(3, 33, 1, 0, 4086)
        rc, info = call_info(self.lib, good, 16)
        self.assertEqual(rc, 0)
        self.assertEqual(info.interface, 2)
        self.assertEqual(info.max_control, 4086)
        rc, info = call_info(self.lib, wrong, 16)
        self.assertEqual(rc, -3)
        self.assertEqual(info.interface, 0x5A)
        rc, info = call_info(iface, good, 16)
        self.assertEqual(rc, -3)
        self.assertEqual(info.max_input, 0x5A5A)
        rc, info = call_info(iface, wrong, 16)
        self.assertEqual(rc, 0)
        self.assertEqual(info.interface, 2)
        self.assertEqual(info.country, 33)
        rc, info = call_info(iface, other, 16)
        self.assertEqual(rc, -3)
        self.assertEqual(info.country, 0x5A)

        zero = header(2, 0, 0, 65535, 0)
        advertised = header(2, 0, 2048, 0, 4086)
        ceiling = header(2, 255, 65535, 0, 0)
        high = header(2, 0, 2039, 0, 0)
        rc, info = call_info(self.lib, zero, 16)
        self.assertEqual(rc, -4)
        self.assertEqual(info.max_output, 0x5A5A)
        rc, info = call_info(opened, zero, 16)
        self.assertEqual(rc, 0)
        self.assertEqual(info.max_input, 0)
        self.assertEqual(info.max_output, 65535)
        rc, info = call_info(self.lib, advertised, 31)
        self.assertEqual(rc, 0)
        self.assertEqual(info.max_input, 2048)
        self.assertEqual(info.max_output, 0)
        self.assertEqual(info.max_control, 4086)
        self.assertEqual(info.interface, 2)
        rc, info = call_info(capped, advertised, 31)
        self.assertEqual(rc, -4)
        self.assertEqual(info.max_input, 0x5A5A)
        rc, info = call_info(self.lib, ceiling, 16)
        self.assertEqual(rc, 0)
        self.assertEqual(info.max_input, 65535)
        self.assertEqual(info.country, 255)
        rc, info = call_info(capped, ceiling, 16)
        self.assertEqual(rc, -4)
        self.assertEqual(info.max_input, 0x5A5A)
        rc, info = call_info(self.lib, high, 16)
        self.assertEqual(rc, 0)
        self.assertEqual(info.max_input, 2039)
        rc, info = call_info(capped, high, 16)
        self.assertEqual(rc, -4)
        self.assertEqual(info.max_control, 0x5A5A)

        src = (ctypes.c_uint8 * 514)(*[i & 0xFF for i in range(514)])
        dst = (ctypes.c_uint8 * 514)(*([0xA5] * 514))
        length = ctypes.c_uint32(0xA5A5A5A5)
        self.assertEqual(self.lib.m1tp_copy_descriptor(dst, 514, ctypes.byref(length), src, 513), -2)
        self.assertEqual(length.value, 0xA5A5A5A5)
        self.assertEqual(dst[0], 0xA5)
        self.assertEqual(desc.m1tp_copy_descriptor(dst, 514, ctypes.byref(length), src, 513), 0)
        self.assertEqual(length.value, 513)
        self.assertEqual(dst[512], 512 & 0xFF)
        self.assertEqual(dst[513], 0xA5)
        length = ctypes.c_uint32(9)
        dst2 = (ctypes.c_uint8 * 8)(*([0x11] * 8))
        self.assertEqual(desc.m1tp_copy_descriptor(dst2, 8, ctypes.byref(length), src, 514), -2)
        self.assertEqual(desc.m1tp_copy_descriptor(dst2, 8, ctypes.byref(length), src, 0), -2)
        self.assertEqual(length.value, 9)
        self.assertEqual(dst2[0], 0x11)

    def test_frozen_matcher_and_citation(self):
        self.assertEqual(sha256(LINUX), LINUX_SHA)
        lines = LINUX.read_text(encoding="utf-8", errors="replace").splitlines()
        cited = "\n".join(lines[1119:1142])
        self.assertIn("spihid_apple_request(spihid, 0xd0, 0x20, 0x02, i,", cited)
        self.assertIn("spihid_apple_request(spihid, 0xd0, 0x20, 0x10, i,", cited)
        self.assertIn("SPIHID_DESC_MAX", cited)
        layout = "\n".join(lines[613:626])
        self.assertIn("u8 interface_num;", layout)
        self.assertIn("u8 country_code;", layout)
        self.assertIn("__le16 max_input_report_len;", layout)
        self.assertIn("__le16 max_output_report_len;", layout)
        self.assertIn("__le16 max_control_report_len;", layout)
        self.assertEqual(lines[52].strip(), "#define SPIHID_DESC_MAX 512")
        self.assertEqual(lines[37].strip(), "#define SPIHID_MAX_INPUT_REPORT_SIZE 0x800")
        copied = "\n".join(lines[627:650])
        self.assertIn("le16_to_cpu(info->max_input_report_len)", copied)
        self.assertNotIn("2038", copied)
        self.assertNotIn("interface_num", copied.split("info =", 1)[1])
        for rel, digest in KBD_SHA.items():
            self.assertEqual(sha256(KBD / rel), digest, rel)
        text = (KBD / "native_protocol/dl_spi_hid.h").read_text(encoding="utf-8")
        self.assertIn("#define DLSP_MESSAGE_BYTES 2048u", text)
        self.assertIn("#define DLSP_DESCRIPTOR_BYTES 512u", text)
        code = r'''
#include "m1kbd_hid.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t used = 0;
    struct dlsp_message m;
    if (m1kbd_claim_request(&used, 0x02, 0x02) != 0 || used != 0) return 1;
    if (m1kbd_claim_request(&used, 0x10, 0x02) != 0 || used != 0) return 2;
    if (m1kbd_claim_request(&used, 0x01, 0xd0) != 1 || used != 1) return 3;
    memset(&m, 0, sizeof m);
    m.device = 0xd0; m.flags = 0x40; m.command = 0x20;
    m.report = 0x02; m.subdevice = 0x02;
    if (m1kbd_response_matches(&m, 0x02, 0x02) != 1) return 4;
    m.report = 0x10; m.subdevice = 0x02;
    if (m1kbd_response_matches(&m, 0x10, 0x02) != 1) return 5;
    return 0;
}
'''
        src = Path(self.tmp.name) / "frozen_match.c"
        src.write_text(code, encoding="utf-8")
        bin_path = Path(self.tmp.name) / "frozen_match"
        subprocess.check_call([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            "-I" + str(KBD), str(src), str(KBD / "m1kbd_hid.c"), "-o", str(bin_path)])
        subprocess.check_call([str(bin_path)])
        body = (HERE / "m1tp_enum.c").read_text(encoding="utf-8")
        for banned in ("dlsp_", "m1kbd_", "memcpy", "malloc", "mmap", "ioctl"):
            self.assertNotIn(banned, body)

    def test_provenance(self):
        provenance = json.loads((HERE / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertFalse(provenance["hardware_operation"])
        self.assertEqual(provenance["revision"], 2)
        self.assertEqual(
            provenance["api_delta_from_root_request"],
            "advertised max_input is any nonzero uint16; payload, descriptor, and claim limits unchanged")
        archive = HERE / "revisions" / "001"
        for name, meta in provenance["revision_001"]["files"].items():
            path = archive / name
            self.assertEqual(sha256(path), meta["sha256"], name)
            self.assertEqual(path.stat().st_size, meta["bytes"], name)
        required = {"m1tp_enum.h", "m1tp_enum.c", "test_m1tp_enum.c", "test_m1tp_enum.py", "README.md"}
        self.assertEqual(set(provenance["original_files"]), required)
        for name, meta in provenance["original_files"].items():
            path = HERE / name
            self.assertEqual(sha256(path), meta["sha256"], name)
            self.assertEqual(path.stat().st_size, meta["bytes"], name)

    def write(self, name, text):
        path = Path(self.tmp.name) / name
        path.write_text(text, encoding="utf-8")
        return path


def compile_checks(tmp):
    obj = tmp / "m1tp_enum.o"
    subprocess.check_call(CC + FREE + ["-c", str(HERE / "m1tp_enum.c"), "-o", str(obj)])
    undefined = subprocess.check_output(["nm", "-u", str(obj)], text=True)
    if undefined.strip():
        raise SystemExit("undefined symbols:\n" + undefined)
    defined = subprocess.check_output(["nm", str(obj)], text=True)
    for banned in ("malloc", "memcpy", "memset", "mmap", "dlsp_", "m1kbd_"):
        if banned in defined:
            raise SystemExit(banned + " in codec object")
    binary = tmp / "test_m1tp_enum"
    subprocess.check_call(CC + [
        "-g", "-fsanitize=address,undefined",
        str(HERE / "m1tp_enum.c"), str(HERE / "test_m1tp_enum.c"), "-o", str(binary)])
    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.check_call([str(binary)], env=env)
    ndebug = subprocess.run(
        CC + ["-DNDEBUG", "-c", str(HERE / "test_m1tp_enum.c"), "-o", str(tmp / "ndebug.o")],
        check=False, capture_output=True, text=True)
    if ndebug.returncode == 0 or "M1TP_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN" not in ndebug.stderr:
        raise SystemExit("NDEBUG guard did not fire:\n" + ndebug.stderr)
    abi = tmp / "abi.cc"
    abi.write_text(
        '#include "m1tp_enum.h"\n'
        "int main() {\n"
        "  uint8_t used = 0;\n"
        "  if (m1tp_claim_request(&used, 0x02, 0x02) != 1 || used != 8) return 1;\n"
        "  return sizeof(struct m1tp_interface_info) == 8 ? 0 : 2;\n"
        "}\n", encoding="utf-8")
    subprocess.check_call([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
        "-I" + str(HERE), str(HERE / "m1tp_enum.c"), str(abi), "-o", str(tmp / "abi")])
    subprocess.check_call([str(tmp / "abi")])


def main():
    with tempfile.TemporaryDirectory(prefix="m1tp-enum-host-") as raw:
        compile_checks(Path(raw))
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Mutants)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
