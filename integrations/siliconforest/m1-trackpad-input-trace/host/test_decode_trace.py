"""Host tests for the passive dev2 trace decoder. No hardware and no guest build."""
import hashlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import decode_trace as trace

SOURCE = "m1-trackpad-input-trace/host/decode_trace.py"
CYCLE = "cycle-ggc_4986"
INIT = trace.INIT_LINE
SOLE = "SFM1SPIHID: sole probe attempt claimed, registry_id=1"


def report(ident: int, x: int = 0, y: int = 0, buttons: int = 0, fill: int = 0) -> bytes:
    length = trace.WIRE_LENGTH[ident]
    raw = bytearray(length)
    raw[0] = ident
    if ident == 0x02:
        raw[1] = buttons & 0xFF
        raw[2] = x & 0xFF
        raw[3] = y & 0xFF
    else:
        for index in range(1, length):
            raw[index] = (fill + index) & 0xFF
    return bytes(raw)


class Builder:
    def __init__(self, prose=True):
        self.lines = [SOLE]
        if prose:
            self.lines.append("keyboard line stays in the console")
        self.lines.append(INIT)
        self.seen = {0x02: 0, 0x3F: 0, 0x44: 0}
        self.seq = 0

    def unlogged(self, ident: int) -> None:
        self.seen[ident] += 1

    def add(self, raw: bytes, reason: int = 1) -> None:
        self.seq += 1
        self.seen[raw[0]] += 1
        seen = self.seen
        self.lines.append(
            f"DL24_TP_BEGIN seq={self.seq} id={raw[0]:02x} bytes={len(raw)} reason={reason} "
            f"seen02={seen[0x02]} seen3f={seen[0x3F]} seen44={seen[0x44]}"
        )
        for index, offset in enumerate(range(0, len(raw), 64)):
            chunk = raw[offset:offset + 64]
            self.lines.append(f"DL24_TP_HEX seq={self.seq} index={index} data={chunk.hex()}")
        self.lines.append(
            f"DL24_TP_END seq={self.seq} bytes={len(raw)} crc16={trace.crc16_arc(raw):04x}"
        )

    def console(self) -> bytes:
        return ("\n".join(self.lines) + "\n").encode()


def decode(console: bytes, **kwargs):
    values = {"source": SOURCE, "cycle": CYCLE, "descriptor_sha256": trace.DESCRIPTOR_SHA256}
    values.update(kwargs)
    return trace.decode(console, **values)


class DecodeTraceTest(unittest.TestCase):
    def test_crc16_arc_check_value(self):
        self.assertEqual(trace.crc16_arc(b"123456789"), 0xBB3D)
        self.assertEqual(trace.crc16_arc(b"\x01"), 0xC0C1)
        self.assertEqual(trace.crc16_arc(b""), 0)

    def test_no_reports_is_no_input(self):
        result = decode(b"ordinary console\nstill not a trace\n")
        self.assertEqual(result["status"], "NO_INPUT_CAPTURED")
        self.assertEqual(result["sample_count"], 0)
        self.assertFalse(result["pointer_motion"])
        self.assertEqual(result["samples_sha256"], hashlib.sha256(b"").hexdigest())
        self.assertEqual(result["inferences"]["mode_write"], "not_inferred")
        self.assertFalse(result["physical_input_proven"])
        self.assertEqual(result["nontrace_line_count"], 2)

    def test_init_without_samples_is_no_input(self):
        result = decode((SOLE + "\n" + INIT + "\n").encode())
        self.assertEqual(result["status"], "NO_INPUT_CAPTURED")
        self.assertTrue(result["initialized"])
        self.assertEqual(result["sample_count"], 0)
        self.assertFalse(result["pointer_motion"])

    def test_invalid_xy_is_retained_and_is_not_no_input(self):
        built = Builder()
        raw = report(0x02, x=0x80, y=0)
        built.add(raw)
        result = decode(built.console())
        self.assertEqual(result["status"], "TRACE_CAPTURED")
        self.assertTrue(result["capture_valid"])
        self.assertEqual(result["sample_count"], 1)
        record = result["records"][0]
        self.assertEqual(record["raw_hex"], raw.hex())
        self.assertEqual(record["value_status"], "descriptor_value_invalid")
        self.assertEqual(record["classification"], "descriptor_value_invalid")
        self.assertFalse(record["pointer_motion"])
        self.assertNotIn("x", record)
        self.assertNotIn("y", record)
        self.assertFalse(result["pointer_motion"])
        self.assertEqual(result["samples_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(result["inferences"]["mode_write"], "not_inferred")

    def test_other_valid_sample_still_proves_motion(self):
        built = Builder()
        invalid = report(0x02, x=0x80, y=1)
        moving = report(0x02, x=4, y=-2, buttons=0xF9)
        built.add(invalid)
        built.add(moving)
        result = decode(built.console())
        self.assertEqual(result["sample_count"], 2)
        self.assertEqual(result["records"][0]["value_status"], "descriptor_value_invalid")
        self.assertFalse(result["records"][0]["pointer_motion"])
        good = result["records"][1]
        self.assertEqual(good["classification"], "standard_mouse_pointer")
        self.assertTrue(good["pointer_motion"])
        self.assertEqual(good["buttons"], 1)
        self.assertEqual((good["x"], good["y"]), (4, -2))
        self.assertTrue(result["pointer_motion"])
        self.assertEqual(result["samples_sha256"], hashlib.sha256(invalid + moving).hexdigest())

    def test_edges_and_zero_motion(self):
        built = Builder()
        still = report(0x02, x=0, y=0, buttons=0x07)
        edge = report(0x02, x=-127, y=127, buttons=0x08)
        built.add(still)
        built.add(edge)
        result = decode(built.console())
        self.assertFalse(result["records"][0]["pointer_motion"])
        self.assertEqual(result["records"][0]["buttons"], 7)
        self.assertTrue(result["records"][1]["pointer_motion"])
        self.assertEqual(result["records"][1]["buttons"], 0)
        self.assertEqual((result["records"][1]["x"], result["records"][1]["y"]), (-127, 127))

    def test_repeated_identical_bodies_stay(self):
        built = Builder()
        raw = report(0x02)
        built.add(raw)
        built.add(raw)
        result = decode(built.console())
        self.assertEqual(result["sample_count"], 2)
        self.assertEqual(result["records"][0]["raw_hex"], result["records"][1]["raw_hex"])

    def test_vendor_bytes_are_not_motion(self):
        built = Builder()
        short = report(0x3F, fill=0x7F)
        long = report(0x44, fill=0x11)
        built.add(short)
        built.add(long)
        result = decode(built.console())
        self.assertEqual([item["classification"] for item in result["records"]], ["vendor_unknown", "vendor_unknown"])
        self.assertFalse(result["pointer_motion"])
        self.assertEqual(result["records"][1]["nbytes"], 1752)
        self.assertEqual(long[0], 0x44)
        self.assertEqual(result["records"][1]["sha256"], hashlib.sha256(long).hexdigest())

    def test_sixteen_invalid_then_reserved_motion(self):
        built = Builder()
        samples = []
        for index in range(16):
            raw = report(0x02, x=0x80, y=index)
            samples.append(raw)
            built.add(raw, reason=1)
        built.unlogged(0x02)
        motion = report(0x02, x=1, y=0)
        samples.append(motion)
        built.add(motion, reason=2)
        result = decode(built.console())
        self.assertEqual(result["sample_count"], 17)
        self.assertTrue(all(item["value_status"] == "descriptor_value_invalid" for item in result["records"][:16]))
        self.assertEqual(result["records"][16]["reason"], 2)
        self.assertTrue(result["records"][16]["pointer_motion"])
        self.assertTrue(result["pointer_motion"])
        self.assertEqual(result["samples_sha256"], hashlib.sha256(b"".join(samples)).hexdigest())

    def test_full_twenty_record_bound(self):
        built = Builder()
        for _ in range(16):
            built.add(report(0x02))
        built.add(report(0x3F, fill=1))
        built.add(report(0x3F, fill=2))
        built.add(report(0x44, fill=3))
        built.unlogged(0x02)
        built.add(report(0x02, x=-5), reason=2)
        result = decode(built.console())
        self.assertEqual(result["sample_count"], 20)
        self.assertEqual(result["records"][-1]["reason"], 2)
        self.assertEqual(result["records"][-1]["x"], -5)

    def test_reason2_rejects_out_of_range_xy(self):
        built = Builder()
        for _ in range(16):
            built.add(report(0x02))
        built.add(report(0x02, x=0x80), reason=2)
        with self.assertRaises(trace.TraceError) as caught:
            decode(built.console())
        self.assertIn("reason", str(caught.exception))

    def test_reason2_rejects_when_ordinary_motion_exists(self):
        built = Builder()
        built.add(report(0x02, x=2))
        for _ in range(15):
            built.add(report(0x02))
        built.add(report(0x02, x=3), reason=2)
        with self.assertRaises(trace.TraceError) as caught:
            decode(built.console())
        self.assertIn("reason", str(caught.exception))

    def test_reason2_requires_the_sixteen(self):
        built = Builder()
        for _ in range(15):
            built.add(report(0x02))
        built.add(report(0x02, x=1), reason=2)
        with self.assertRaises(trace.TraceError) as caught:
            decode(built.console())
        self.assertIn("reason", str(caught.exception))

    def test_structural_rejections_do_not_pass(self):
        cases = []
        crc = Builder()
        raw = report(0x02, x=1)
        crc.add(raw)
        crc.lines[-1] = crc.lines[-1][:-1] + ("0" if crc.lines[-1][-1] != "0" else "1")
        cases.append((crc.console(), "crc"))

        foreign = Builder()
        foreign.seq = 1
        foreign.lines.append(
            "DL24_TP_BEGIN seq=1 id=01 bytes=8 reason=1 seen02=1 seen3f=0 seen44=0"
        )
        cases.append((foreign.console(), "foreign id"))

        length = Builder()
        length.lines.append(
            "DL24_TP_BEGIN seq=1 id=02 bytes=9 reason=1 seen02=1 seen3f=0 seen44=0"
        )
        cases.append((length.console(), "length"))

        truncated = Builder()
        truncated.add(report(0x02, x=1))
        del truncated.lines[-1]
        cases.append((truncated.console(), "truncated"))

        gap = Builder()
        gap.add(report(0x02))
        gap.seq = 2
        gap.add(report(0x02, x=1))
        cases.append((gap.console(), "order"))

        duplicate = Builder()
        duplicate.lines.append(INIT)
        cases.append((duplicate.console(), "duplicate"))

        corrupt = Builder()
        corrupt.lines.append("DL24_TP_HEX seq=1 index=0 data=GG")
        cases.append((corrupt.console(), "corrupt"))

        for console, label in cases:
            with self.subTest(label=label):
                with self.assertRaises(trace.TraceError) as caught:
                    decode(console)
                self.assertIn(label, str(caught.exception))

    def test_bad_trace_line_is_not_filtered_into_a_pass(self):
        built = Builder()
        built.add(report(0x02, x=1))
        built.lines.insert(-3, "DL24_TP_BEGIN seq=1 id=02 bytes=8 reason=1 seen02=1 seen3f=0 seen44=0")
        with self.assertRaises(trace.TraceError):
            decode(built.console())

    def test_nontrace_text_is_preserved(self):
        built = Builder()
        built.lines.insert(1, "trace error: this prose is not a DL24 line")
        built.add(report(0x02, x=1))
        result = decode(built.console())
        self.assertEqual(result["status"], "TRACE_CAPTURED")
        self.assertGreaterEqual(result["nontrace_line_count"], 3)
        kept = [line for line in built.lines if not line.startswith("DL24_TP_")]
        self.assertEqual(result["nontrace_sha256"], hashlib.sha256("\n".join(kept).encode()).hexdigest())

    def test_cycle_seen_limit_and_sole_probe_order(self):
        self.assertEqual(trace.SEEN_LIMIT, 0xFFFFFFFF)
        quiet = decode(b"ordinary console\n", cycle="cycle-ggc_4986")
        self.assertEqual(quiet["status"], "NO_INPUT_CAPTURED")
        self.assertEqual(quiet["cycle"], "cycle-ggc_4986")
        for rejected in ("cycle-trace01", "cycle-ggc_49867", "cycle-GGC_4986", "cycle-ggc-4986"):
            with self.subTest(cycle=rejected):
                with self.assertRaises(trace.TraceError) as caught:
                    decode(b"ordinary console\n", cycle=rejected)
                self.assertIn("cycle", str(caught.exception))

        late = (INIT + "\n" + SOLE + "\n").encode()
        with self.assertRaises(trace.TraceError) as caught:
            decode(late)
        self.assertIn("sole probe", str(caught.exception))

        built = Builder()
        for _ in range(16):
            built.add(report(0x02))
        built.seen[0x02] = trace.SEEN_LIMIT
        built.add(report(0x3F, fill=4))
        saturated = decode(built.console())
        self.assertEqual(saturated["records"][-1]["seen02"], trace.SEEN_LIMIT)
        self.assertEqual(saturated["status"], "TRACE_CAPTURED")

        overflow = Builder()
        for _ in range(16):
            overflow.add(report(0x02))
        overflow.seen[0x02] = trace.SEEN_LIMIT + 1
        overflow.add(report(0x3F, fill=5))
        with self.assertRaises(trace.TraceError) as caught:
            decode(overflow.console())
        self.assertIn("seen02", str(caught.exception))

    def test_binding(self):
        built = Builder()
        built.add(report(0x02, x=1))
        console = built.console()
        with self.assertRaises(trace.TraceError) as caught:
            decode(console, descriptor_sha256="0" * 64)
        self.assertIn("foreign descriptor", str(caught.exception))
        without = console.replace(SOLE.encode() + b"\n", b"")
        with self.assertRaises(trace.TraceError) as caught:
            decode(without)
        self.assertIn("sole probe", str(caught.exception))
        result = decode(console)
        self.assertEqual(result["source"], SOURCE)
        self.assertEqual(result["cycle"], CYCLE)
        self.assertEqual(result["descriptor_sha256"], trace.DESCRIPTOR_SHA256)

    def test_cr_is_transport_only(self):
        built = Builder(prose=False)
        built.add(report(0x02, x=2))
        result = decode(built.console().replace(b"\n", b"\r\n"))
        self.assertEqual(result["records"][0]["x"], 2)


if __name__ == "__main__":
    unittest.main()
