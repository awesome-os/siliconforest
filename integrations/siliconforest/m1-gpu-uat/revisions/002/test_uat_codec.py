#!/usr/bin/env python3
"""Host checks for the software UAT codec. No device, compiler, or network."""

import hashlib
import json
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import uat_codec as uat

# Frozen words. Each literal is also rebuilt from shifts in the tests.
TTBR_ASID7 = 0x0007000080000001
PAGE_LEAF = 0x00C0000018004443
TABLE_L2 = 0x10004003
BLOCK_L2 = 0x00C0000002000441
LEAF = {"os": 1, "uxn": 1, "pxn": 0, "ng": 0, "af": 1, "sh": 0, "ap": 1, "attr": 0}
ZERO = {"os": 0, "uxn": 0, "pxn": 0, "ng": 0, "af": 0, "sh": 0, "ap": 0, "attr": 0}


def pack_ttbr(pa, asid):
    return (asid << 48) | pa | 1


def pack_pte(pa, type_bit, **flags):
    return (
        (flags["os"] << 55) | (flags["uxn"] << 54) | (flags["pxn"] << 53)
        | (flags["ng"] << 11) | (flags["af"] << 10) | (flags["sh"] << 8)
        | (flags["ap"] << 6) | (flags["attr"] << 2) | pa | (type_bit << 1) | 1
    )


class Geometry(unittest.TestCase):
    def test_only_t8103(self):
        self.assertEqual(set(uat.FORMATS), {"t8103"})
        for name in ("T8103", "g13", "g14", "g14x", "g15", "g15x", "pre_g15", ""):
            with self.assertRaises(uat.UatError) as caught:
                uat.levels(name)
            self.assertEqual(caught.exception.code, "format")

    def test_widths_and_spans(self):
        t = uat.levels("t8103")
        self.assertEqual([item["shift"] for item in t], [39, 36, 25, 14])
        self.assertEqual([item["entries"] for item in t], [2, 8, 2048, 2048])
        self.assertEqual([item["span"] for item in t], [1 << 39, 1 << 36, 1 << 25, 1 << 14])
        self.assertEqual(uat.table_bytes("t8103", 0), 16)
        self.assertEqual(uat.table_bytes("t8103", 1), 64)
        self.assertEqual(uat.table_bytes("t8103", 2), 16384)
        self.assertEqual(uat.table_bytes("t8103", 3), 16384)
        self.assertEqual(uat.PAGE_SIZE, 16384)
        self.assertEqual(uat.FORMATS["t8103"]["pa_bits"], 40)

    def test_distinguishing_index(self):
        self.assertEqual(uat.virtual_index("t8103", 0, 1 << 39), 1)
        self.assertEqual(uat.virtual_index("t8103", 1, 1 << 39), 0)
        va = 0x02004010
        self.assertEqual(
            [uat.virtual_index("t8103", level, va) for level in range(4)],
            [0, 0, 1, 1],
        )
        self.assertEqual(uat.page_offset("t8103", va), 0x10)

    def test_canonical_virtual(self):
        folded = (1 << 39) | 0x1234
        extended = ((1 << 24) - 1) << 40 | folded
        self.assertEqual(uat.fold_virtual("t8103", folded), folded)
        self.assertEqual(uat.fold_virtual("t8103", extended), folded)
        self.assertEqual(uat.virtual_index("t8103", 0, extended), 1)
        for bad in (1 << 40, ((1 << 24) - 1) << 40, -1, 1 << 64, True):
            with self.assertRaises(uat.UatError) as caught:
                uat.fold_virtual("t8103", bad)
            self.assertIn(caught.exception.code, ("canonical", "address", "argument"))

    def test_context_slot(self):
        self.assertEqual(uat.context_offset(0), 0)
        self.assertEqual(uat.context_offset(63), 1008)
        with self.assertRaises(uat.UatError) as caught:
            uat.context_offset(64)
        self.assertEqual(caught.exception.code, "argument")


class Descriptors(unittest.TestCase):
    def test_golden_words_match_shifts(self):
        self.assertEqual(pack_ttbr(0x80000000, 7), TTBR_ASID7)
        self.assertEqual(pack_pte(0x18004000, 1, **LEAF), PAGE_LEAF)
        self.assertEqual(pack_pte(0x10004000, 1, **ZERO), TABLE_L2)
        self.assertEqual(pack_pte(0x02000000, 0, **LEAF), BLOCK_L2)

    def test_golden_round_trip(self):
        word = uat.encode_descriptor("t8103", 0, "table", 0x80000000, {"asid": 7})
        self.assertEqual(word, TTBR_ASID7)
        decoded = uat.decode_descriptor("t8103", 0, word)
        self.assertEqual(decoded["pa"], 0x80000000)
        self.assertEqual(decoded["asid"], 7)
        self.assertEqual(decoded["evidence"], "software-encoding")

        word = uat.encode_descriptor("t8103", 3, "page", 0x18004000, LEAF)
        self.assertEqual(word, PAGE_LEAF)
        decoded = uat.decode_descriptor("t8103", 3, word)
        self.assertEqual(decoded["kind"], "page")
        self.assertEqual(decoded["pa"], 0x18004000)
        self.assertEqual(decoded["attr_name"], "Normal")
        self.assertEqual(decoded["ap"], 1)
        self.assertEqual(decoded["uxn"], 1)
        self.assertEqual(decoded["os"], 1)

        word = uat.encode_descriptor("t8103", 2, "table", 0x10004000, ZERO)
        self.assertEqual(word, TABLE_L2)
        self.assertEqual(uat.decode_descriptor("t8103", 2, word)["kind"], "table")

        word = uat.encode_descriptor("t8103", 2, "block", 0x02000000, LEAF)
        self.assertEqual(word, BLOCK_L2)
        decoded = uat.decode_descriptor("t8103", 2, word)
        self.assertEqual(decoded["kind"], "block")
        self.assertEqual(decoded["raw_output"], 0x02000000)
        self.assertEqual(decoded["span"], 1 << 25)
        self.assertNotIn("pa", decoded)

    def test_little_endian_word(self):
        blob = uat.empty_table("t8103", 0)
        uat.store_word(blob, 0, TTBR_ASID7)
        self.assertEqual(bytes(blob[:8]), bytes.fromhex("0100008000000700"))
        self.assertEqual(uat.load_word(blob, 0), TTBR_ASID7)
        self.assertEqual(uat.load_word(bytes(blob), 1), 0)

    def test_address_width_and_alignment(self):
        wide = 1 << 40
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 0, "table", wide, {"asid": 0})
        self.assertEqual(caught.exception.code, "address")
        with self.assertRaises(uat.UatError) as caught:
            uat.decode_descriptor("t8103", 0, pack_ttbr(wide, 0))
        self.assertEqual(caught.exception.code, "address")
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("g15", 3, "page", 0x18004000, LEAF)
        self.assertEqual(caught.exception.code, "format")
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 3, "page", 0x18004200, LEAF)
        self.assertEqual(caught.exception.code, "alignment")
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 2, "block", 0x02004000, LEAF)
        self.assertEqual(caught.exception.code, "alignment")
        with self.assertRaises(uat.UatError) as caught:
            uat.decode_descriptor("t8103", 0, 0x80000003)
        self.assertEqual(caught.exception.code, "alignment")
        top = (1 << 40) - uat.PAGE_SIZE
        self.assertEqual(
            uat.encode_descriptor("t8103", 3, "page", top, LEAF) & uat._mask(47, 14), top)

    def test_reject_unknown_and_invalid(self):
        self.assertEqual(uat.decode_descriptor("t8103", 2, 0)["kind"], "empty")
        self.assertEqual(uat.encode_descriptor("t8103", 2, "empty"), 0)
        for word, code in (
            (1 << 10, "descriptor"),
            (PAGE_LEAF | (1 << 5), "reserved"),
            (PAGE_LEAF | (1 << 12), "reserved"),
            (PAGE_LEAF | (1 << 48), "reserved"),
            (PAGE_LEAF | (1 << 63), "reserved"),
            (0x18004001, "descriptor"),
        ):
            with self.assertRaises(uat.UatError) as caught:
                uat.decode_descriptor("t8103", 3, word)
            self.assertEqual(caught.exception.code, code, hex(word))
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 0, "page", 0x80000000, LEAF)
        self.assertEqual(caught.exception.code, "descriptor")
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 3, "block", 0x18004000, LEAF)
        self.assertEqual(caught.exception.code, "descriptor")
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 1, "table", 0x10000000, {"asid": 0})
        self.assertEqual(caught.exception.code, "argument")
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 0, "table", 0x80000000, {"asid": 1 << 16})
        self.assertEqual(caught.exception.code, "argument")
        with self.assertRaises(uat.UatError) as caught:
            uat.encode_descriptor("t8103", 2, "empty", 0)
        self.assertEqual(caught.exception.code, "argument")

    def test_flag_round_trip(self):
        count = 0
        for os_ in (0, 1):
            for uxn in (0, 1):
                for pxn in (0, 1):
                    for ng in (0, 1):
                        for af in (0, 1):
                            for sh in range(4):
                                for ap in range(4):
                                    for attr in range(8):
                                        fields = {
                                            "os": os_, "uxn": uxn, "pxn": pxn, "ng": ng,
                                            "af": af, "sh": sh, "ap": ap, "attr": attr,
                                        }
                                        word = uat.encode_descriptor(
                                            "t8103", 3, "page", 0x18004000, fields)
                                        self.assertEqual(word, pack_pte(0x18004000, 1, **fields))
                                        decoded = uat.decode_descriptor("t8103", 3, word)
                                        for key, value in fields.items():
                                            self.assertEqual(decoded[key], value)
                                        count += 1
        self.assertEqual(count, 4096)
        zero = uat.encode_descriptor("t8103", 0, "table", 0, {"asid": 0})
        self.assertEqual(zero, 1)
        self.assertEqual(uat.decode_descriptor("t8103", 0, zero)["pa"], 0)


class Walk(unittest.TestCase):
    def setUp(self):
        self.root = uat.empty_table("t8103", 0)
        uat.store_word(self.root, 0, uat.encode_descriptor(
            "t8103", 0, "table", 0x10000000, {"asid": 0}))
        self.l1 = uat.empty_table("t8103", 1)
        uat.store_word(self.l1, 0, uat.encode_descriptor(
            "t8103", 1, "table", 0x10004000, ZERO))
        self.l2 = uat.empty_table("t8103", 2)
        uat.store_word(self.l2, 1, uat.encode_descriptor(
            "t8103", 2, "table", 0x10008000, ZERO))
        self.l3 = uat.empty_table("t8103", 3)
        uat.store_word(self.l3, 1, uat.encode_descriptor(
            "t8103", 3, "page", 0x18004000, LEAF))
        self.tables = {0x10000000: self.l1, 0x10004000: self.l2, 0x10008000: self.l3}

    def test_page_translation(self):
        before = bytes(self.root), bytes(self.l1), bytes(self.l2), bytes(self.l3)
        hit = uat.translate("t8103", 0x02004010, self.root, self.tables)
        self.assertEqual(hit["kind"], "page")
        self.assertEqual(hit["pa"], 0x18004010)
        self.assertEqual(hit["output"], 0x18004000)
        self.assertEqual(hit["level"], 3)
        self.assertEqual(hit["index"], 1)
        self.assertEqual(hit["evidence"], "software-encoding")
        miss = uat.translate("t8103", 0x02000000, self.root, self.tables)
        self.assertEqual(miss["kind"], "unmapped")
        self.assertEqual(miss["level"], 3)
        self.assertNotIn("pa", miss)
        self.assertEqual((bytes(self.root), bytes(self.l1), bytes(self.l2), bytes(self.l3)), before)

    def test_nonzero_block_offset_is_not_a_translated_pa(self):
        uat.store_word(self.l2, 1, uat.encode_descriptor(
            "t8103", 2, "block", 0x04000000, LEAF))
        tables = {0x10000000: self.l1, 0x10004000: self.l2}
        base = uat.translate("t8103", 0x02000000, self.root, tables)
        hit = uat.translate("t8103", 0x02001000, self.root, tables)
        self.assertEqual(uat.page_offset("t8103", 0x02001000), 0x1000)
        self.assertNotEqual(hit["virtual_offset"], 0)
        self.assertEqual(hit["virtual_offset"], 0x1000)
        self.assertEqual(hit["kind"], "block")
        self.assertEqual(hit["translation"], "refused")
        self.assertEqual(hit["raw_output"], 0x04000000)
        self.assertEqual(hit["span"], 1 << 25)
        self.assertNotIn("pa", hit)
        self.assertNotIn("pa", hit["descriptor"])
        self.assertNotIn(hit["raw_output"] + hit["virtual_offset"], hit.values())
        self.assertEqual(base["translation"], "refused")
        self.assertEqual(base["virtual_offset"], 0)
        self.assertNotIn("pa", base)
        self.assertEqual(base["raw_output"], hit["raw_output"])

    def test_incomplete_or_corrupt_image(self):
        with self.assertRaises(uat.UatError) as caught:
            uat.translate("t8103", 0x02004010, self.root, {0x10000000: self.l1})
        self.assertEqual(caught.exception.code, "missing")
        short = bytearray(8)
        with self.assertRaises(uat.UatError) as caught:
            uat.translate("t8103", 0, short, {})
        self.assertEqual(caught.exception.code, "length")
        bad = bytearray(self.l3)
        uat.store_word(bad, 1, PAGE_LEAF | (1 << 5))
        tables = dict(self.tables)
        tables[0x10008000] = bad
        with self.assertRaises(uat.UatError) as caught:
            uat.translate("t8103", 0x02004010, self.root, tables)
        self.assertEqual(caught.exception.code, "reserved")
        with self.assertRaises(uat.UatError) as caught:
            uat.translate("t8103", 0, memoryview(bytes(self.root)), self.tables)
        self.assertEqual(caught.exception.code, "argument")
        with self.assertRaises(uat.UatError) as caught:
            uat.translate("t8103", 1 << 40, self.root, self.tables)
        self.assertEqual(caught.exception.code, "canonical")


class Freeze(unittest.TestCase):
    def test_codec_stays_free_of_host_io(self):
        source = Path(uat.__file__).read_text()
        for banned in ("import", "ioctl", "/dev/", "mmap", "subprocess", "socket", "ctypes"):
            self.assertNotIn(banned, source)

    def test_provenance_matches_files(self):
        record = json.loads((HERE / "PROVENANCE.json").read_text())
        self.assertEqual(record["status"], "SOURCE_FACTS_FOR_ORIGINAL_HOST_ONLY_CODEC")
        self.assertFalse(record["hardware_operation"])
        lab = HERE
        while lab.name != "darwin-lab":
            lab = lab.parent
        self.assertEqual(lab.name, "darwin-lab")
        self.assertEqual(record["revision"], 2)
        self.assertEqual(record["block_translation"]["decision"], "refused")
        geometry = record["other_generation_geometry"]
        self.assertFalse(geometry["accepted_by_codec"])
        self.assertEqual(geometry["l0_shift"], 42)
        self.assertEqual(geometry["l1_entries"], 64)
        self.assertNotIn("output_bits", geometry)
        parent = HERE.parents[1]
        for name, digest in record["predecessor"]["files"].items():
            blob = (parent / name).read_bytes()
            self.assertEqual(hashlib.sha256(blob).hexdigest(), digest)
            self.assertEqual(hashlib.sha256((parent / "revisions/001" / name).read_bytes()).hexdigest(), digest)
        for item in record["references"]:
            blob = (lab / item["path"]).read_bytes()
            digest = hashlib.sha256(blob).hexdigest()
            self.assertEqual(digest, item["sha256"], item["path"])
            self.assertEqual(len(blob), item["bytes"], item["path"])
        for name, digest in record["original_files"].items():
            blob = (HERE / name).read_bytes()
            actual = hashlib.sha256(blob).hexdigest()
            self.assertEqual(actual, digest["sha256"], name)


if __name__ == "__main__":
    unittest.main()
