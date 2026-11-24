#!/usr/bin/env python3
"""Seeded ctypes differential of the C T8103 codec against revisions/003.

The Python module is the oracle for encode, decode, and address split. This
file does not call its walker. Build products stay in a temporary directory.
"""

import ctypes
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
ORACLE_PATH = HERE.parent / "revisions" / "003" / "uat_codec.py"
ORACLE_SHA256 = "97b2b9a706ab6112e59047c4f70ba8f116d3f4e4dcaaec487207acc91973f252"
REV001_SHA256 = "a4de3c97918ba10d34ce1a7b68c96bac216f7f1574dc93078c5158896d12e333"
REV002_SHA256 = ORACLE_SHA256
SEED = 0x0A710003
MASK64 = (1 << 64) - 1
SENT64 = 0xA5A5A5A5A5A5A5A5
PAGE_LEAF = 0x00C0000018004443
CC = [
    "cc", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O1",
    "-I" + str(HERE),
]
FREE = ["-fno-stack-protector", "-fno-builtin", "-ffreestanding",
        "-fno-tree-loop-distribute-patterns"]


def sha256(path):
    digest = hashlib.sha256()
    digest.update(Path(path).read_bytes())
    return digest.hexdigest()


def load_oracle():
    spec = importlib.util.spec_from_file_location("uat_codec_rev003", ORACLE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def xorshift(state):
    state = (state ^ ((state << 13) & MASK64)) & MASK64
    state ^= state >> 7
    state = (state ^ ((state << 17) & MASK64)) & MASK64
    return state


class Flags(ctypes.Structure):
    _fields_ = [
        ("os", ctypes.c_uint8), ("uxn", ctypes.c_uint8), ("pxn", ctypes.c_uint8),
        ("ng", ctypes.c_uint8), ("af", ctypes.c_uint8), ("sh", ctypes.c_uint8),
        ("ap", ctypes.c_uint8), ("attr", ctypes.c_uint8),
    ]


class Decoded(ctypes.Structure):
    _fields_ = [
        ("word", ctypes.c_uint64), ("pa", ctypes.c_uint64),
        ("raw_output", ctypes.c_uint64), ("span", ctypes.c_uint64),
        ("asid", ctypes.c_uint32), ("kind", ctypes.c_uint8), ("level", ctypes.c_uint8),
        ("os", ctypes.c_uint8), ("uxn", ctypes.c_uint8), ("pxn", ctypes.c_uint8),
        ("ng", ctypes.c_uint8), ("af", ctypes.c_uint8), ("sh", ctypes.c_uint8),
        ("ap", ctypes.c_uint8), ("attr", ctypes.c_uint8),
        ("translated", ctypes.c_uint8), ("reserved0", ctypes.c_uint8),
    ]


class BlockFacts(ctypes.Structure):
    _fields_ = [
        ("raw_output", ctypes.c_uint64), ("span", ctypes.c_uint64),
        ("virtual_offset", ctypes.c_uint64),
    ]


class Level(ctypes.Structure):
    _fields_ = [
        ("level", ctypes.c_uint32), ("shift", ctypes.c_uint32),
        ("entries", ctypes.c_uint32), ("table_bytes", ctypes.c_uint32),
        ("span", ctypes.c_uint64),
    ]


def bind(lib):
    lib.uat_format_accepted.argtypes = [ctypes.c_char_p]
    lib.uat_format_accepted.restype = ctypes.c_int
    for name in (
        "uat_pa_bits", "uat_page_size", "uat_page_shift", "uat_l0_shift",
        "uat_descriptor_field_bits", "uat_context_count", "uat_context_stride",
    ):
        getattr(lib, name).argtypes = []
        getattr(lib, name).restype = ctypes.c_uint32
    lib.uat_evidence.argtypes = []
    lib.uat_evidence.restype = ctypes.c_char_p
    lib.uat_status_name.argtypes = [ctypes.c_int]
    lib.uat_status_name.restype = ctypes.c_char_p
    lib.uat_level.argtypes = [ctypes.c_uint32, ctypes.POINTER(Level)]
    lib.uat_level.restype = ctypes.c_int
    lib.uat_table_bytes.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    lib.uat_table_bytes.restype = ctypes.c_int
    lib.uat_fold_virtual.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64)]
    lib.uat_fold_virtual.restype = ctypes.c_int
    lib.uat_virtual_index.argtypes = [
        ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint32)]
    lib.uat_virtual_index.restype = ctypes.c_int
    lib.uat_page_offset.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint32)]
    lib.uat_page_offset.restype = ctypes.c_int
    lib.uat_context_offset.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    lib.uat_context_offset.restype = ctypes.c_int
    lib.uat_encode_empty.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint64)]
    lib.uat_encode_empty.restype = ctypes.c_int
    lib.uat_encode_ttbr.argtypes = [
        ctypes.c_uint64, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint64)]
    lib.uat_encode_ttbr.restype = ctypes.c_int
    lib.uat_encode_pte.argtypes = [
        ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(Flags),
        ctypes.POINTER(ctypes.c_uint64)]
    lib.uat_encode_pte.restype = ctypes.c_int
    lib.uat_decode.argtypes = [ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(Decoded)]
    lib.uat_decode.restype = ctypes.c_int
    lib.uat_load_word.argtypes = [
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_uint64)]
    lib.uat_load_word.restype = ctypes.c_int
    lib.uat_store_word.argtypes = [
        ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint64]
    lib.uat_store_word.restype = ctypes.c_int
    lib.uat_refuse_block.argtypes = [
        ctypes.c_uint32, ctypes.c_uint64, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(BlockFacts)]
    lib.uat_refuse_block.restype = ctypes.c_int
    return lib


def status_name(lib, code):
    return lib.uat_status_name(code).decode("ascii")


def flags_of(fields):
    out = Flags()
    for key in ("os", "uxn", "pxn", "ng", "af", "sh", "ap", "attr"):
        setattr(out, key, fields[key])
    return out


def poison_decoded():
    decoded = Decoded()
    decoded.word = SENT64
    decoded.pa = SENT64
    decoded.raw_output = SENT64
    decoded.span = SENT64
    decoded.asid = 0x5A5A5A5A
    for name in (
        "kind", "level", "os", "uxn", "pxn", "ng", "af", "sh", "ap", "attr",
        "translated", "reserved0",
    ):
        setattr(decoded, name, 0x5A)
    return decoded


def decoded_unchanged(decoded):
    return (
        decoded.word == SENT64 and decoded.pa == SENT64
        and decoded.raw_output == SENT64 and decoded.span == SENT64
        and decoded.asid == 0x5A5A5A5A and decoded.kind == 0x5A
        and decoded.translated == 0x5A and decoded.reserved0 == 0x5A
    )


KIND = {"empty": 0, "table": 1, "block": 2, "page": 3}
KIND_C = {0: "empty", 1: "table", 2: "block", 3: "page"}


class OracleDifferential(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.uat = load_oracle()
        cls.tmp = tempfile.TemporaryDirectory(prefix="uat-t8103-")
        cls.lib = bind(build_library(HERE / "uat_t8103.c", Path(cls.tmp.name) / "uat.so"))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_sizes_and_symbols(self):
        self.assertEqual(ctypes.sizeof(Decoded), 48)
        self.assertEqual(ctypes.sizeof(Flags), 8)
        self.assertEqual(ctypes.sizeof(BlockFacts), 24)
        self.assertEqual(self.lib.uat_pa_bits(), 40)
        self.assertEqual(self.lib.uat_l0_shift(), self.uat.FORMATS["t8103"]["l0_shift"])
        self.assertEqual(self.lib.uat_page_size(), self.uat.PAGE_SIZE)
        self.assertEqual(self.lib.uat_descriptor_field_bits(), self.uat.DESCRIPTOR_FIELD_BITS)
        self.assertEqual(self.lib.uat_evidence(), b"software-encoding")

    def test_geometry_and_split(self):
        levels = self.uat.levels("t8103")
        for info in levels:
            got = Level()
            self.assertEqual(self.lib.uat_level(info["level"], ctypes.byref(got)), 0)
            self.assertEqual(got.shift, info["shift"])
            self.assertEqual(got.entries, info["entries"])
            self.assertEqual(got.span, info["span"])
            self.assertEqual(got.table_bytes, self.uat.table_bytes("t8103", info["level"]))
        for name in (b"T8103", b"g15", b"g13", b"g14", b"g14x", b"", b"t8103x"):
            self.assertEqual(status_name(self.lib, self.lib.uat_format_accepted(name)), "format")
            with self.assertRaises(self.uat.UatError) as caught:
                self.uat.levels(name.decode())
            self.assertEqual(caught.exception.code, "format")
        self.assertEqual(self.lib.uat_format_accepted(b"t8103"), 0)
        self.assertEqual(self.lib.uat_format_accepted(None), 2)

        samples = boundary_values()
        state = SEED
        for _ in range(192):
            state = xorshift(state)
            samples.append(state)
        for va in samples:
            self.compare_fold(va)
        for ctx in (0, 1, 63, 64, 65535):
            got = ctypes.c_uint32(0xABCDEF)
            code = status_name(self.lib, self.lib.uat_context_offset(ctx, ctypes.byref(got)))
            try:
                expect = self.uat.context_offset(ctx)
            except self.uat.UatError as exc:
                self.assertEqual(code, exc.code)
                self.assertEqual(got.value, 0xABCDEF)
            else:
                self.assertEqual(code, "ok")
                self.assertEqual(got.value, expect)

    def test_exhaustive_flags_and_seeded_ranges(self):
        for level, kind_name, kind, pa in (
            (3, "page", 3, 0x18004000),
            (2, "table", 1, 0x10004000),
            (2, "block", 2, 0x02000000),
        ):
            for bits in range(4096):
                fields = fields_from_bits(bits)
                self.compare_pte(level, kind_name, kind, pa, fields)
        state = SEED
        for _ in range(128):
            state = xorshift(state)
            fields = fields_from_bits(state & 0xFFF)
            page = (state & ((1 << 40) - 1)) & ~0x3FFF
            self.compare_pte(3, "page", 3, page, fields)
            self.compare_pte(3, "page", 3, page | (1 << 40), fields)
            self.compare_pte(3, "page", 3, page | 0x200, fields)
            block = (state & 0x1F) << 25
            self.compare_pte(2, "block", 2, block, fields)
            self.compare_pte(2, "block", 2, block + 0x4000, fields)
            asid = state & 0x1FFFF
            self.compare_ttbr(page, asid)
        self.compare_ttbr(0, 0)
        self.compare_ttbr(0x80000000, 7)
        self.compare_ttbr((1 << 40) - 16384, 65535)
        self.compare_ttbr(1 << 40, 0)
        self.compare_ttbr((1 << 40) - 1, 0)
        for level in range(4):
            word = ctypes.c_uint64(SENT64)
            self.assertEqual(self.lib.uat_encode_empty(level, ctypes.byref(word)), 0)
            self.assertEqual(word.value, 0)
            self.compare_decode(level, 0)

    def test_reserved_and_alignment_inputs(self):
        reserved = [5, 12, 13, 48, 49, 50, 51, 52] + list(range(56, 64))
        for bit in reserved:
            decoded = poison_decoded()
            code = status_name(
                self.lib, self.lib.uat_decode(3, PAGE_LEAF | (1 << bit), ctypes.byref(decoded)))
            try:
                self.uat.decode_descriptor("t8103", 3, PAGE_LEAF | (1 << bit))
            except self.uat.UatError as exc:
                self.assertEqual(code, exc.code, bit)
                self.assertEqual(exc.code, "reserved")
            else:
                self.fail("oracle accepted reserved bit %s" % bit)
            self.assertTrue(decoded_unchanged(decoded))
        for word, code in ((1 << 10, "descriptor"), (0x18004001, "descriptor"),
                           (0x80000003, "alignment"), ((1 << 40) | 1, "address")):
            self.compare_decode(3 if word != 0x80000003 and word != ((1 << 40) | 1) else 0, word)
            self.assertEqual(code, code)

    def test_block_refusal_matches_oracle_facts(self):
        cases = (
            (2, 0x02001000, 0x04000000),
            (2, 0x02000000, 0x04000000),
            (1, 0x1000, 1 << 36),
            (2, ((1 << 24) - 1) << 40 | (1 << 39) | 0x1000, 0x02000000),
        )
        for level, va, raw in cases:
            pa = ctypes.c_uint64(SENT64)
            facts = BlockFacts(SENT64, SENT64, SENT64)
            code = status_name(
                self.lib, self.lib.uat_refuse_block(level, va, raw, ctypes.byref(pa), ctypes.byref(facts)))
            folded = self.uat.fold_virtual("t8103", va)
            info = self.uat.level("t8103", level)
            self.uat.decode_descriptor(
                "t8103", level,
                self.uat.encode_descriptor("t8103", level, "block", raw, zero_fields()))
            self.assertEqual(code, "refused")
            self.assertEqual(pa.value, SENT64)
            self.assertEqual(facts.raw_output, raw)
            self.assertEqual(facts.span, info["span"])
            self.assertEqual(facts.virtual_offset, folded & (info["span"] - 1))
        pa = ctypes.c_uint64(SENT64)
        facts = BlockFacts(SENT64, SENT64, SENT64)
        code = status_name(self.lib, self.lib.uat_refuse_block(
            2, 0x02001000, 1 << 40, ctypes.byref(pa), ctypes.byref(facts)))
        with self.assertRaises(self.uat.UatError) as caught:
            self.uat.encode_descriptor("t8103", 2, "block", 1 << 40, zero_fields())
        self.assertEqual(code, caught.exception.code)
        self.assertEqual(pa.value, SENT64)
        self.assertEqual(facts.raw_output, SENT64)

    def test_source_mutants_disagree(self):
        text = (HERE / "uat_t8103.c").read_text(encoding="utf-8")
        width = "if (pa >= (1ull << UAT_PA_BITS)) return UAT_ERR_ADDRESS;"
        self.assertEqual(text.count(width), 1)
        wide = text.replace(width, "if (pa >= (1ull << 48)) return UAT_ERR_ADDRESS;", 1)
        reserved = "~UAT_PTE_DEFINED"
        self.assertEqual(text.count(reserved), 2)
        dropped = text.replace(reserved, "(~UAT_PTE_DEFINED & ~(1ull << 5))")
        wide_lib = bind(build_library(write_source("pa-range.c", wide), Path(self.tmp.name) / "pa.so"))
        drop_lib = bind(build_library(write_source("reserved-mask.c", dropped), Path(self.tmp.name) / "mask.so"))

        real = self.encode_ttbr(self.lib, 1 << 40, 0)
        mutant = self.encode_ttbr(wide_lib, 1 << 40, 0)
        try:
            self.uat.encode_descriptor("t8103", 0, "table", 1 << 40, {"asid": 0})
            self.fail("oracle accepted a 40-bit physical address")
        except self.uat.UatError as exc:
            self.assertEqual(exc.code, "address")
        self.assertEqual(real[0], "address")
        self.assertEqual(real[1], SENT64)
        self.assertEqual(mutant[0], "ok")
        self.assertEqual(mutant[1], (1 << 40) | 1)
        still = self.encode_ttbr(wide_lib, 1 << 48, 0)
        self.assertEqual(still[0], "address")
        self.assertEqual(still[1], SENT64)

        witness = PAGE_LEAF | (1 << 5)
        real_dec = self.decode(self.lib, 3, witness)
        drop_dec = self.decode(drop_lib, 3, witness)
        with self.assertRaises(self.uat.UatError) as caught:
            self.uat.decode_descriptor("t8103", 3, witness)
        self.assertEqual(caught.exception.code, "reserved")
        self.assertEqual(real_dec[0], "reserved")
        self.assertTrue(decoded_unchanged(real_dec[1]))
        self.assertEqual(drop_dec[0], "ok")
        self.assertEqual(drop_dec[1].kind, 3)
        self.assertEqual(drop_dec[1].pa, 0x18004000)
        other = self.decode(drop_lib, 3, PAGE_LEAF | (1 << 12))
        self.assertEqual(other[0], "reserved")
        self.assertTrue(decoded_unchanged(other[1]))

    def test_frozen_hashes(self):
        self.assertEqual(sha256(ORACLE_PATH), ORACLE_SHA256)
        self.assertEqual(sha256(HERE.parent / "revisions" / "001" / "uat_codec.py"), REV001_SHA256)
        self.assertEqual(sha256(HERE.parent / "revisions" / "002" / "uat_codec.py"), REV002_SHA256)
        self.assertEqual(sha256(HERE.parent / "revisions" / "003" / "uat_codec.py"), ORACLE_SHA256)
        provenance = json.loads((HERE / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertFalse(provenance["hardware_operation"])
        self.assertEqual(provenance["evidence_class"], "software-encoding")
        self.assertEqual(provenance["block_translation"]["decision"], "refused")
        self.assertEqual(provenance["oracle"]["sha256"], ORACLE_SHA256)
        required = {
            "uat_t8103.h", "uat_t8103.c", "test_uat_t8103.c",
            "test_uat_oracle.py", "README.md",
        }
        self.assertEqual(set(provenance["original_files"]), required)
        for name, meta in provenance["original_files"].items():
            path = HERE / name
            self.assertEqual(sha256(path), meta["sha256"], name)
            self.assertEqual(path.stat().st_size, meta["bytes"], name)

    def compare_fold(self, va):
        folded = ctypes.c_uint64(SENT64)
        code = status_name(self.lib, self.lib.uat_fold_virtual(va, ctypes.byref(folded)))
        try:
            expect = self.uat.fold_virtual("t8103", va)
        except self.uat.UatError as exc:
            self.assertEqual(code, exc.code, hex(va))
            self.assertEqual(folded.value, SENT64)
            return
        self.assertEqual(code, "ok", hex(va))
        self.assertEqual(folded.value, expect)
        for level in range(4):
            index = ctypes.c_uint32(0xABCDEF)
            code = status_name(
                self.lib, self.lib.uat_virtual_index(level, va, ctypes.byref(index)))
            self.assertEqual(code, "ok")
            self.assertEqual(index.value, self.uat.virtual_index("t8103", level, va))
        offset = ctypes.c_uint32(0xABCDEF)
        self.assertEqual(self.lib.uat_page_offset(va, ctypes.byref(offset)), 0)
        self.assertEqual(offset.value, self.uat.page_offset("t8103", va))

    def compare_pte(self, level, kind_name, kind, pa, fields):
        c_code, c_word = self.encode_pte(self.lib, level, kind, pa, fields)
        try:
            expect = self.uat.encode_descriptor("t8103", level, kind_name, pa, fields)
        except self.uat.UatError as exc:
            self.assertEqual(c_code, exc.code, (level, kind_name, hex(pa), fields))
            self.assertEqual(c_word, SENT64)
            return
        self.assertEqual(c_code, "ok")
        self.assertEqual(c_word, expect)
        self.compare_decode(level, expect)

    def compare_ttbr(self, pa, asid):
        c_code, c_word = self.encode_ttbr(self.lib, pa, asid)
        try:
            expect = self.uat.encode_descriptor("t8103", 0, "table", pa, {"asid": asid})
        except self.uat.UatError as exc:
            self.assertEqual(c_code, exc.code, (hex(pa), asid))
            self.assertEqual(c_word, SENT64)
            return
        self.assertEqual(c_code, "ok")
        self.assertEqual(c_word, expect)
        self.compare_decode(0, expect)

    def compare_decode(self, level, word):
        c_code, decoded = self.decode(self.lib, level, word)
        try:
            expect = self.uat.decode_descriptor("t8103", level, word)
        except self.uat.UatError as exc:
            self.assertEqual(c_code, exc.code, (level, hex(word)))
            self.assertTrue(decoded_unchanged(decoded))
            return
        self.assertEqual(c_code, "ok")
        self.assertEqual(KIND_C[decoded.kind], expect["kind"])
        self.assertEqual(decoded.level, level)
        self.assertEqual(decoded.word, word)
        self.assertEqual(decoded.reserved0, 0)
        if expect["kind"] == "empty":
            self.assertEqual(decoded.translated, 0)
            self.assertEqual(decoded.pa, 0)
            return
        if expect["kind"] == "block":
            self.assertEqual(decoded.translated, 0)
            self.assertEqual(decoded.pa, 0)
            self.assertEqual(decoded.raw_output, expect["raw_output"])
            self.assertEqual(decoded.span, expect["span"])
        else:
            self.assertEqual(decoded.translated, 1)
            self.assertEqual(decoded.pa, expect["pa"])
            self.assertEqual(decoded.raw_output, 0)
            self.assertEqual(decoded.span, 0)
        if expect["kind"] == "table" and level == 0:
            self.assertEqual(decoded.asid, expect["asid"])
        else:
            self.assertEqual(decoded.asid, 0)
            for key in ("os", "uxn", "pxn", "ng", "af", "sh", "ap", "attr"):
                self.assertEqual(getattr(decoded, key), expect[key])

    def encode_ttbr(self, lib, pa, asid):
        word = ctypes.c_uint64(SENT64)
        code = status_name(lib, lib.uat_encode_ttbr(pa, asid, ctypes.byref(word)))
        return code, word.value

    def encode_pte(self, lib, level, kind, pa, fields):
        word = ctypes.c_uint64(SENT64)
        flags = flags_of(fields)
        code = status_name(lib, lib.uat_encode_pte(
            level, kind, pa, ctypes.byref(flags), ctypes.byref(word)))
        return code, word.value

    def decode(self, lib, level, word):
        decoded = poison_decoded()
        code = status_name(lib, lib.uat_decode(level, word, ctypes.byref(decoded)))
        return code, decoded


def fields_from_bits(bits):
    return {
        "os": (bits >> 0) & 1,
        "uxn": (bits >> 1) & 1,
        "pxn": (bits >> 2) & 1,
        "ng": (bits >> 3) & 1,
        "af": (bits >> 4) & 1,
        "sh": (bits >> 5) & 3,
        "ap": (bits >> 7) & 3,
        "attr": (bits >> 9) & 7,
    }


def zero_fields():
    return fields_from_bits(0)


def boundary_values():
    low = (1 << 39) | 0x1234
    return [
        0, 1, 0x10, 0x02004010, 1 << 39, low,
        (((1 << 24) - 1) << 40) | low,
        ((1 << 64) - 1),
        1 << 40,
        ((1 << 24) - 1) << 40,
        (1 << 40) - 1,
        (1 << 39) - 1,
    ]


def build_library(source, output):
    subprocess.check_call(CC + FREE + ["-fPIC", "-shared", str(source), "-o", str(output)])
    return ctypes.CDLL(str(output))


def write_source(name, text):
    path = Path(OracleDifferential.tmp.name) / name
    path.write_text(text, encoding="utf-8")
    return path


def compile_checks(tmp):
    obj = tmp / "uat_t8103.o"
    subprocess.check_call(CC + FREE + ["-c", str(HERE / "uat_t8103.c"), "-o", str(obj)])
    undefined = subprocess.check_output(["nm", "-u", str(obj)], text=True)
    if undefined.strip():
        raise SystemExit("codec object has undefined symbols:\n" + undefined)
    defined = subprocess.check_output(["nm", str(obj)], text=True)
    for banned in ("malloc", "memcpy", "memset", "free", "mmap", "translate"):
        if banned in defined:
            raise SystemExit("codec object contains " + banned + ":\n" + defined)
    if "uat_refuse_block" not in defined:
        raise SystemExit("block refusal symbol missing")
    binary = tmp / "test_uat_t8103"
    subprocess.check_call(CC + [
        "-g", "-fsanitize=address,undefined",
        str(HERE / "uat_t8103.c"), str(HERE / "test_uat_t8103.c"), "-o", str(binary),
    ])
    env = os.environ.copy()
    env["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.check_call([str(binary)], env=env)
    ndebug = subprocess.run(
        CC + ["-DNDEBUG", "-c", str(HERE / "test_uat_t8103.c"), "-o", str(tmp / "ndebug.o")],
        check=False, capture_output=True, text=True)
    if ndebug.returncode == 0 or "UAT_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN" not in ndebug.stderr:
        raise SystemExit("NDEBUG guard did not fire:\n" + ndebug.stderr)


def main():
    with tempfile.TemporaryDirectory(prefix="uat-t8103-host-") as raw:
        compile_checks(Path(raw))
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(OracleDifferential)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    if not result.wasSuccessful():
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
