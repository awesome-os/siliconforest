#!/usr/bin/env python3
"""Host-only tests; synthetic values, no captured device data or target writes."""
import copy
import dataclasses
import hashlib
import importlib.util
import json
import os
import random
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
SOURCE = Path(os.environ.get("BOOTARGS_TEST_MODULE", HERE / "native_bootargs.py"))
spec = importlib.util.spec_from_file_location("original_native_bootargs", SOURCE)
m = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = m
spec.loader.exec_module(m)


def values():
    return {"revision": 2, "version": 2, "virt_base": 0xfffffe0010000000,
            "phys_base": 0x880000000, "mem_size": 0x10000000,
            "top_of_kernel_data": 0x880800000,
            "video": {"base": 0x890000000, "display": 2, "stride": 10240,
                      "width": 2560, "height": 1600, "depth": 0x1001e},
            "machine_type": 0x12345678, "devtree": 0xfffffe0010040000,
            "devtree_size": 0x50000, "cmdline": "-v -s debug=0x14 cpus=1",
            "boot_flags": 0x0123456789abcdef, "mem_size_actual": 0x400000000}


def wire(v=None):
    return m.encode(values() if v is None else v, wire_profile=m.WIRE_PROFILE)


def source_r2(v):
    # Independent synthetic representation of the pinned writer facts, not
    # a production legacy codec/decoder or a relabeled historical capture.
    b = bytearray(736)
    struct.pack_into("<HH4x4Q6QI4xQI", b, 0, v["revision"], v["version"],
                     v["virt_base"], v["phys_base"], v["mem_size"], v["top_of_kernel_data"],
                     *[v["video"][k] for k in ("base", "display", "stride", "width", "height", "depth")],
                     v["machine_type"], v["devtree"], v["devtree_size"])
    cmd = v["cmdline"].encode("ascii")
    assert len(cmd) < 608 and b"\0" not in cmd
    b[108:108 + len(cmd)] = cmd
    struct.pack_into("<QQ", b, 720, v["boot_flags"], v["mem_size_actual"])
    return bytes(b)


class NativeBootArgsTests(unittest.TestCase):
    def test_constants_bound_to_native_probe(self):
        self.assertEqual(m.WIRE_SIZE, 1152)
        self.assertEqual(m.COMMAND_OFFSET, 108)
        self.assertEqual(m.COMMAND_BYTES, 1024)
        self.assertEqual(m.FLAGS_OFFSET, 1136)
        self.assertEqual(m.ACTUAL_OFFSET, 1144)
        facts = json.loads((HERE / "ABI_FACTS.json").read_text())
        self.assertEqual(facts["wire_profile"], m.WIRE_PROFILE)
        self.assertEqual(facts["genuine_header_dependency_count"], 103)
        for key, actual in (("boot_args_size", m.WIRE_SIZE), ("CommandLine_offset", m.COMMAND_OFFSET),
                            ("CommandLine_bytes", m.COMMAND_BYTES), ("bootFlags_offset", m.FLAGS_OFFSET),
                            ("memSizeActual_offset", m.ACTUAL_OFFSET)):
            self.assertEqual(facts["native_layout"][key], actual)

    def test_exact_layout_and_common_prefix(self):
        v = values(); src = source_r2(v); b = wire(v)
        self.assertEqual(len(b), 1152)
        self.assertEqual(b[:108], src[:108])
        self.assertEqual(b[108:108 + len(v["cmdline"])], src[108:108 + len(v["cmdline"])])
        self.assertEqual(struct.unpack_from("<HH", b), (2, 2))
        self.assertEqual(struct.unpack_from("<QQ", b, 1136), (v["boot_flags"], v["mem_size_actual"]))
        self.assertEqual(struct.unpack_from("<QQ", src, 720), (v["boot_flags"], v["mem_size_actual"]))
        self.assertEqual(b[720:736], bytes(16))
        for lo, hi in ((4, 8), (92, 96), (1132, 1136)):
            self.assertEqual(b[lo:hi], bytes(hi - lo))
        self.assertEqual(b[108 + len(v["cmdline"]):1132], bytes(1024 - len(v["cmdline"])))

    def test_roundtrip_preserves_input_and_video(self):
        v = values(); before = copy.deepcopy(v)
        self.assertEqual(m.decode(wire(v), wire_profile=m.WIRE_PROFILE), v)
        self.assertEqual(v, before)
        b = wire(v); self.assertEqual(wire(v), b)
        for i, key in enumerate(("base", "display", "stride", "width", "height", "depth")):
            self.assertEqual(struct.unpack_from("<Q", b, 40 + i * 8)[0], v["video"][key])

    def test_width_edges(self):
        for key in ("virt_base", "phys_base", "mem_size", "top_of_kernel_data", "devtree", "boot_flags", "mem_size_actual"):
            for value in (0, (1 << 64) - 1):
                v = values(); v[key] = value
                self.assertEqual(m.decode(wire(v), wire_profile=m.WIRE_PROFILE)[key], value)
        for key in ("machine_type", "devtree_size"):
            for value in (0, (1 << 32) - 1):
                v = values(); v[key] = value
                self.assertEqual(m.decode(wire(v), wire_profile=m.WIRE_PROFILE)[key], value)

    def test_seeded_full_width_roundtrips(self):
        rng = random.Random(0x11520202)
        for n in range(256):
            v = values()
            for k in ("virt_base", "phys_base", "mem_size", "top_of_kernel_data", "devtree", "boot_flags", "mem_size_actual"):
                v[k] = rng.getrandbits(64)
            for k in ("machine_type", "devtree_size"): v[k] = rng.getrandbits(32)
            for k in v["video"]: v["video"][k] = rng.getrandbits(64)
            v["cmdline"] = "".join(chr(rng.randrange(1, 128)) for _ in range((0, 1, 607, 608, 1023)[n % 5]))
            before = copy.deepcopy(v)
            self.assertEqual(m.decode(wire(v), wire_profile=m.WIRE_PROFILE), before)
            self.assertEqual(v, before)

    def test_bad_integer_values_and_widths(self):
        fields = [(k, 64) for k in ("virt_base", "phys_base", "mem_size", "top_of_kernel_data", "devtree", "boot_flags", "mem_size_actual")]
        fields += [("machine_type", 32), ("devtree_size", 32), ("revision", 16), ("version", 16)]
        for key, bits in fields:
            for bad in (-1, 1 << bits, True, False, 1.0, "2", None):
                with self.subTest(key=key, bad=bad):
                    v = values(); v[key] = bad
                    with self.assertRaises(m.BootArgsError): wire(v)
        for key in m.VIDEO_FIELDS:
            for bad in (-1, 1 << 64, True, 1.0):
                v = values(); v["video"][key] = bad
                with self.assertRaises(m.BootArgsError): wire(v)

    def test_exact_fields_plain_dicts(self):
        for key in values():
            v = values(); del v[key]
            with self.assertRaises(m.BootArgsError): wire(v)
        v = values(); v["extra"] = 0
        with self.assertRaises(m.BootArgsError): wire(v)
        v = values(); v["video"]["extra"] = 0
        with self.assertRaises(m.BootArgsError): wire(v)
        class Unexpected(dict): pass
        with self.assertRaises(m.BootArgsError): wire(Unexpected(values()))
        for bad in (None, [], object()):
            with self.assertRaises(m.BootArgsError): m.encode(bad, wire_profile=m.WIRE_PROFILE)

    def test_explicit_profile_and_revision(self):
        for bad in (None, True, 2, "r2", "legacy736", "", m.WIRE_PROFILE + "x"):
            with self.assertRaises(m.BootArgsError): m.encode(values(), wire_profile=bad)
            with self.assertRaises(m.BootArgsError): m.decode(wire(), wire_profile=bad)
        with self.assertRaises(TypeError): m.encode(values())
        with self.assertRaises(TypeError): m.decode(wire())
        for revision, version in ((1, 2), (3, 2), (2, 1), (2, 3)):
            v = values(); v["revision"] = revision; v["version"] = version
            with self.assertRaises(m.BootArgsError): wire(v)
            b = bytearray(wire()); struct.pack_into("<HH", b, 0, revision, version)
            with self.assertRaises(m.BootArgsError): m.decode(bytes(b), wire_profile=m.WIRE_PROFILE)

    def test_ASCII_preserved_NUL_and_bounds(self):
        for command in ("", "x" * 607, "x" * 608, "x" * 1023, " \t\n\r\x01\x1f\x7f "):
            v = values(); v["cmdline"] = command
            self.assertEqual(m.decode(wire(v), wire_profile=m.WIRE_PROFILE)["cmdline"], command)
        for command in ("x" * 1024, "x" * 1025, "x\0y", "\0", "é", b"x", None):
            v = values(); v["cmdline"] = command
            with self.assertRaises(m.BootArgsError): wire(v)

    def test_decoder_strict_padding_NUL_ascii(self):
        for offset in (4, 7, 92, 95, 1132, 1135):
            b = bytearray(wire()); b[offset] = 1
            with self.assertRaises(m.BootArgsError): m.decode(bytes(b), wire_profile=m.WIRE_PROFILE)
        for changed in (b"x" * 1024, b"\0x" + bytes(1022), b"\xff\0" + bytes(1022)):
            b = bytearray(wire()); b[108:1132] = changed
            with self.assertRaises(m.BootArgsError): m.decode(bytes(b), wire_profile=m.WIRE_PROFILE)
        for bad in (wire()[:-1], wire() + b"\0", source_r2(values()), bytearray(wire()), memoryview(wire()), None):
            with self.assertRaises(m.BootArgsError): m.decode(bad, wire_profile=m.WIRE_PROFILE)

    def test_no_legacy_tail_fallback_or_layout_inference(self):
        legacy = source_r2(values())
        with self.assertRaises(m.BootArgsError): m.decode(legacy, wire_profile=m.WIRE_PROFILE)
        with self.assertRaises(m.BootArgsError): m.decode(legacy + bytes(416), wire_profile=m.WIRE_PROFILE)
        # All-zero legacy tails plus zero padding can be byte-identical. The
        # mandatory external profile is provenance, not detectable from revision.
        v = values(); v["boot_flags"] = v["mem_size_actual"] = 0
        padded = source_r2(v) + bytes(416)
        self.assertEqual(padded, wire(v))
        self.assertEqual(m.decode(padded, wire_profile=m.WIRE_PROFILE), v)
        with self.assertRaises(m.BootArgsError): m.decode(padded, wire_profile="legacy736")

    def test_pure_plan_exact_boundaries(self):
        v = values(); a = {"base": 0x880100000, "bytes": 0x4000, "review_sha256": "ab" * 32}
        before = copy.deepcopy((v, a))
        for address in (a["base"], a["base"] + a["bytes"] - 1152):
            p = m.plan_write(v, wire_profile=m.WIRE_PROFILE, address=address, allocation=a)
            self.assertEqual(p.payload, wire(v)); self.assertEqual(p.end - p.address, 1152)
            self.assertEqual(p.payload_sha256, hashlib.sha256(p.payload).hexdigest())
            self.assertEqual(p.metadata()["allocation_review_sha256"], a["review_sha256"])
            self.assertNotIn("payload", p.metadata())
            with self.assertRaises(dataclasses.FrozenInstanceError): p.address = 0
        self.assertEqual((v, a), before)
        a = {"base": (1 << 64) - 1152, "bytes": 1152, "review_sha256": "cd" * 32}
        self.assertEqual(m.plan_write(v, wire_profile=m.WIRE_PROFILE, address=a["base"], allocation=a).end, 1 << 64)

    def test_pure_plan_refusals(self):
        a = {"base": 0x880100000, "bytes": 0x4000, "review_sha256": "ab" * 32}
        for address in (a["base"] - 8, a["base"] + 1, a["base"] + a["bytes"] - 1152 + 8, -1, 1 << 64, True, 1.0):
            with self.assertRaises(m.BootArgsError): m.plan_write(values(), wire_profile=m.WIRE_PROFILE, address=address, allocation=a)
        for key, bad in (("base", 0), ("base", -1), ("base", True), ("bytes", 0), ("bytes", 1151), ("bytes", 1 << 64), ("bytes", True), ("review_sha256", "A" * 64), ("review_sha256", "a" * 63), ("review_sha256", True)):
            b = a.copy(); b[key] = bad
            with self.assertRaises(m.BootArgsError): m.plan_write(values(), wire_profile=m.WIRE_PROFILE, address=a["base"], allocation=b)
        overflow = {"base": (1 << 64) - 8, "bytes": 16, "review_sha256": "ab" * 32}
        with self.assertRaises(m.BootArgsError): m.plan_write(values(), wire_profile=m.WIRE_PROFILE, address=overflow["base"], allocation=overflow)
        with self.assertRaises(m.BootArgsError): m.plan_write(values(), wire_profile="r2", address=a["base"], allocation=a)
        with self.assertRaises(m.BootArgsError): m.plan_write(values(), wire_profile=m.WIRE_PROFILE, address=a["base"], allocation=object())


def mutations():
    source = (HERE / "native_bootargs.py").read_text()
    changes = {
        "flags-at-legacy-offset": ("FLAGS_OFFSET = 1136", "FLAGS_OFFSET = 720"),
        "actual-at-legacy-offset": ("ACTUAL_OFFSET = 1144", "ACTUAL_OFFSET = 728"),
        "legacy-wire-size": ("WIRE_SIZE = 1152", "WIRE_SIZE = 736"),
        "legacy-command-capacity": ("COMMAND_BYTES = 1024", "COMMAND_BYTES = 608"),
        "bool-is-integer": ("type(value) is not int", "not isinstance(value, int)"),
        "profile-bypass": ("if type(profile) is not str or profile != WIRE_PROFILE:", "if False:"),
        "allow-padding": ("if any(payload[low:high]):", "if False:"),
        "ignore-command-trailing-bytes": ("stop < 0 or any(raw[stop + 1:])", "stop < 0"),
        "change-revision": ("payload, 0, revision, version", "payload, 0, 3, version"),
        "accept-out-of-bounds-plan": ("address % 8 or address < base or address + WIRE_SIZE > base + size", "address % 8"),
        "mutation-of-source": ("command = _command(values[\"cmdline\"])", "command = _command(values[\"cmdline\"]); values[\"boot_flags\"] = 0"),
    }
    target = {
        "flags-at-legacy-offset": "test_exact_layout_and_common_prefix",
        "actual-at-legacy-offset": "test_exact_layout_and_common_prefix",
        "legacy-wire-size": "test_constants_bound_to_native_probe",
        "legacy-command-capacity": "test_constants_bound_to_native_probe",
        "bool-is-integer": "test_bad_integer_values_and_widths",
        "profile-bypass": "test_explicit_profile_and_revision",
        "allow-padding": "test_decoder_strict_padding_NUL_ascii",
        "ignore-command-trailing-bytes": "test_decoder_strict_padding_NUL_ascii",
        "change-revision": "test_exact_layout_and_common_prefix",
        "accept-out-of-bounds-plan": "test_pure_plan_refusals",
        "mutation-of-source": "test_roundtrip_preserves_input_and_video",
    }
    for name, (before, after) in changes.items():
        assert source.count(before) == 1, name
        changed = source.replace(before, after)
        compile(changed, name, "exec")  # A syntax/import failure is not a kill.
        with tempfile.TemporaryDirectory(prefix="native-bootargs-mutant-") as td:
            path = Path(td) / "native_bootargs.py"; path.write_text(changed)
            env = dict(os.environ, BOOTARGS_TEST_MODULE=str(path), PYTHONDONTWRITEBYTECODE="1")
            p = subprocess.run([sys.executable, str(HERE / "test_native_bootargs.py"), "NativeBootArgsTests." + target[name]], env=env,
                               capture_output=True, text=True, timeout=30)
            assert p.returncode and "FAIL:" in p.stderr and "FAILED (" in p.stderr and "ERROR:" not in p.stderr, (name, p.stdout, p.stderr)
            print("MUTANT", name, "compiled; assertion killed")
    print("MUTATIONS_PASS", len(changes))


if __name__ == "__main__":
    if sys.argv[1:] == ["--mutations"]:
        baseline = subprocess.run([sys.executable, str(Path(__file__).resolve())],
                                  env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"), timeout=30)
        if baseline.returncode: raise SystemExit(baseline.returncode)
        mutations()
    else:
        unittest.main(verbosity=2)
