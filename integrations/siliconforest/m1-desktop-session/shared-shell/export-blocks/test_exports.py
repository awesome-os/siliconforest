#!/usr/bin/env python3
"""Host tests for the shared-desktop export span parser. No device access."""
import hashlib
import importlib.util
import unittest
from pathlib import Path

from decode_exports import (
    DESKTOP_DECODER_SHA256,
    TERMINAL_DECODER_SHA256,
    decode_exports,
)

HERE = Path(__file__).resolve().parent
DESKTOP_DECODER = HERE.parent / "records" / "decode_records.py"
TERMINAL_DECODER = HERE.parent.parent.parent / "m1-userspace-diagnostics" / "terminal-records" / "decode_records.py"
HOST = "c" * 48
INST_A = "b" * 48
INST_B = "a" * 48
INSTANCES = (INST_A, INST_B)
LIVE_TERMINAL = ("pty-rx.bin", "pty-tx.bin", "child.json", "child-ready.json")
CLOSED_TERMINAL = LIVE_TERMINAL + ("grid-final.txt", "result.json")
SESSION = {"session-result.txt": b"s", "manager-status.log": b"m"}
LOGS = {"application-stdout.log": b"o", "application-stderr.log": b"e"}


def _chunks(payload):
    return (len(payload) + 63) // 64


def _dl22(nonce, kind, name, payload, after):
    digest = hashlib.sha256(payload).hexdigest()
    size = len(payload)
    lines = [f"DL22_RECORD_BEGIN {nonce} {kind} {name} bytes={size} chunk_bytes=64"]
    for index in range(_chunks(payload)):
        chunk = payload[index * 64:(index + 1) * 64]
        lines.append(f"DL22_RECORD_HEX {nonce} {kind} {name} index={index} {chunk.hex()}")
    lines.append(
        f"DL22_RECORD_END {nonce} {kind} {name} bytes={size} chunks={_chunks(payload)} "
        f"file_bytes_after={after} sha256={digest}"
    )
    return lines


def _dl11(nonce, name, payload, after):
    digest = hashlib.sha256(payload).hexdigest()
    size = len(payload)
    lines = [f"DL11_RECORD_BEGIN {nonce} {name} bytes={size} chunk_bytes=64"]
    for index in range(_chunks(payload)):
        chunk = payload[index * 64:(index + 1) * 64]
        lines.append(f"DL11_RECORD_HEX {nonce} {name} index={index} {chunk.hex()}")
    lines.append(
        f"DL11_RECORD_END {nonce} {name} bytes={size} chunks={_chunks(payload)} "
        f"file_bytes_after={after} sha256={digest}"
    )
    return lines


def _payload(instance, name):
    return f"{instance[:4]}:{name}".encode()


def build(phase, *, instances=INSTANCES, prefix=None, skip=(), noise=False, omit_missing=False,
          instance_order=None, footer_failed=0):
    if prefix is None:
        prefix = phase == "live"
    order = tuple(instance_order or instances)
    names = LIVE_TERMINAL if phase == "live" else CLOSED_TERMINAL
    lines = [
        f"DL23_DESKTOP_BEGIN_{HOST} source=explicit_startup requested_terminals=2 physical_input_proven=false",
        "Terminal 000101234567",
        f"DL23_SESSION_EXPORT_BEGIN_{HOST} phase={phase}",
    ]
    expected_session = {}
    for name, payload in SESSION.items():
        if name in skip:
            continue
        after = len(payload) + (1 if prefix and name == "session-result.txt" else 0)
        lines.extend(_dl22(HOST, "session", name, payload, after))
        expected_session[name] = (payload, after)
    lines.append(f"DL23_SESSION_EXPORT_END_{HOST} rc=0")
    expected_apps = []
    inserted = False
    for instance in order:
        lines.append(f"DL23_APP_EXPORT_BEGIN_{HOST} instance={instance} phase={phase}")
        terminal = {}
        for name in names:
            payload = _payload(instance, name)
            after = len(payload) + (1 if prefix and name == "pty-rx.bin" else 0)
            lines.extend(_dl11(instance, name, payload, after))
            if noise and not inserted:
                lines.append("inserted-noise")
                inserted = True
            terminal[name] = (payload, after)
        if phase == "live" and not omit_missing:
            lines.append(f"DL11_RECORD_MISSING {instance} grid-final.txt errno=2")
            lines.append(f"DL11_RECORD_MISSING {instance} result.json errno=2")
        lines.append(f"DL23_APP_TERMINAL_EXPORT_END_{HOST} instance={instance} rc=0")
        logs = {}
        for name, tail in LOGS.items():
            payload = tail + instance[:1].encode()
            after = len(payload) + (1 if prefix and name == "application-stdout.log" else 0)
            lines.extend(_dl22(instance, "application", name, payload, after))
            logs[name] = (payload, after)
        lines.append(f"DL23_APP_EXPORT_END_{HOST} instance={instance} terminal=0 logs=0")
        expected_apps.append({"instance": instance, "terminal": terminal, "logs": logs})
    if phase == "live":
        lines.append(
            f"DL23_DESKTOP_LIVE_EXPORT_{HOST} failed={footer_failed} "
            "cleanup_claimed=false physical_input_proven=false"
        )
    else:
        lines.append(f"DL23_DESKTOP_CLOSED_EXPORT_{HOST} failed={footer_failed} physical_input_proven=false")
        lines.append("CHONKSTEP SHARED SHELL MANAGED SESSION RETURNED")
        lines.append(f"DL23_DESKTOP_RETURN_{HOST}")
        lines.append("Darwin")
    lines.append(f"DL23_DESKTOP_FINAL_STATUS_{HOST} desktop=0 export_failed={footer_failed}")
    raw = ("\n".join(lines) + "\n").encode()
    return raw, {"session": expected_session, "applications": expected_apps}


def _assert_payloads(test, decoded, expected, terminal_names):
    test.assertEqual(set(decoded), {"session", "applications"})
    test.assertIs(type(decoded["applications"]), tuple)
    test.assertEqual([item["instance"] for item in decoded["applications"]],
                     [item["instance"] for item in expected["applications"]])
    for name, (payload, after) in expected["session"].items():
        got = decoded["session"][name]
        test.assertEqual(got["data"], payload)
        test.assertEqual(got["sha256"], hashlib.sha256(payload).hexdigest())
        test.assertEqual(got["file_bytes_after"], after)
        test.assertLess(len(payload), 64)
    test.assertEqual(set(decoded["session"]), set(expected["session"]))
    for got_app, want_app in zip(decoded["applications"], expected["applications"]):
        test.assertEqual(set(got_app), {"instance", "terminal", "logs"})
        test.assertEqual(set(got_app["terminal"]), set(terminal_names))
        for name, (payload, after) in want_app["terminal"].items():
            got = got_app["terminal"][name]
            test.assertEqual(got["data"], payload)
            test.assertEqual(got["sha256"], hashlib.sha256(payload).hexdigest())
            test.assertEqual(got["file_bytes_after"], after)
        for name, (payload, after) in want_app["logs"].items():
            got = got_app["logs"][name]
            test.assertEqual(got["data"], payload)
            test.assertEqual(got["sha256"], hashlib.sha256(payload).hexdigest())
            test.assertEqual(got["file_bytes_after"], after)


class ExportBlockTests(unittest.TestCase):
    def test_live_prefix_returns_payloads(self):
        raw, expected = build("live", footer_failed=1)
        raw = raw.replace(b"\n", b"\r\n")
        decoded = decode_exports(raw, HOST, INSTANCES, "live")
        _assert_payloads(self, decoded, expected, LIVE_TERMINAL)
        self.assertEqual(decoded["session"]["session-result.txt"]["file_bytes_after"],
                         len(SESSION["session-result.txt"]) + 1)
        self.assertEqual(decoded["applications"][0]["terminal"]["pty-rx.bin"]["file_bytes_after"],
                         len(_payload(INST_A, "pty-rx.bin")) + 1)
        self.assertNotIn("grid-final.txt", decoded["applications"][0]["terminal"])
        self.assertNotIn("result.json", decoded["applications"][1]["terminal"])

    def test_closed_six_files_stable_size(self):
        raw, expected = build("closed")
        decoded = decode_exports(raw, HOST, list(INSTANCES), "closed")
        _assert_payloads(self, decoded, expected, CLOSED_TERMINAL)
        for app in decoded["applications"]:
            for item in app["terminal"].values():
                self.assertEqual(item["file_bytes_after"], len(item["data"]))
            for item in app["logs"].values():
                self.assertEqual(item["file_bytes_after"], len(item["data"]))
        for item in decoded["session"].values():
            self.assertEqual(item["file_bytes_after"], len(item["data"]))

    def test_foreign_nonce(self):
        raw, _ = build("live")
        foreign = ("d" * 48).encode()
        begin = f"DL23_SESSION_EXPORT_BEGIN_{HOST}".encode()
        with self.subTest("wrapper"):
            mutated = raw.replace(begin, b"DL23_SESSION_EXPORT_BEGIN_" + foreign, 1)
            with self.assertRaisesRegex(ValueError, "Foreign nonce"):
                decode_exports(mutated, HOST, INSTANCES, "live")
        with self.subTest("record"):
            mutated = raw.replace(f"DL22_RECORD_BEGIN {HOST}".encode(),
                                  b"DL22_RECORD_BEGIN " + foreign, 1)
            with self.assertRaisesRegex(ValueError, "foreign"):
                decode_exports(mutated, HOST, INSTANCES, "live")
        with self.subTest("argument"):
            with self.assertRaisesRegex(ValueError, "Invalid capture/nonce"):
                decode_exports(raw, "D" * 48, INSTANCES, "live")

    def test_foreign_kind(self):
        raw, _ = build("closed")
        mutated = raw.replace(b" session ", b" application ", 1)
        with self.assertRaisesRegex(ValueError, "foreign"):
            decode_exports(mutated, HOST, INSTANCES, "closed")

    def test_export_status(self):
        raw, _ = build("live")
        cases = (
            (f"DL23_SESSION_EXPORT_END_{HOST} rc=0".encode(),
             f"DL23_SESSION_EXPORT_END_{HOST} rc=1".encode()),
            (f"DL23_APP_TERMINAL_EXPORT_END_{HOST} instance={INST_A} rc=0".encode(),
             f"DL23_APP_TERMINAL_EXPORT_END_{HOST} instance={INST_A} rc=2".encode()),
            (f"DL23_APP_EXPORT_END_{HOST} instance={INST_A} terminal=0 logs=0".encode(),
             f"DL23_APP_EXPORT_END_{HOST} instance={INST_A} terminal=0 logs=1".encode()),
            (f"DL23_SESSION_EXPORT_END_{HOST} rc=0".encode(),
             f"DL23_SESSION_EXPORT_END_{HOST} rc=00".encode()),
        )
        for old, new in cases:
            with self.subTest(new.decode()):
                with self.assertRaisesRegex(ValueError, "Export status"):
                    decode_exports(raw.replace(old, new, 1), HOST, INSTANCES, "live")

    def test_phase(self):
        live, _ = build("live")
        with self.subTest("marker"):
            with self.assertRaisesRegex(ValueError, "Phase mismatch"):
                decode_exports(live, HOST, INSTANCES, "closed")
        with self.subTest("closed prefix"):
            prefixed, _ = build("closed", prefix=True)
            with self.assertRaisesRegex(ValueError, "corrupt"):
                decode_exports(prefixed, HOST, INSTANCES, "closed")
        with self.subTest("argument"):
            with self.assertRaisesRegex(ValueError, "Invalid phase"):
                decode_exports(live, HOST, INSTANCES, "open")

    def test_duplicates(self):
        raw, _ = build("closed")
        with self.subTest("second session"):
            extra = raw + f"DL23_SESSION_EXPORT_BEGIN_{HOST} phase=closed\n".encode()
            with self.assertRaisesRegex(ValueError, "Duplicate export"):
                decode_exports(extra, HOST, INSTANCES, "closed")
        with self.subTest("repeated file"):
            begin = f"DL22_RECORD_BEGIN {HOST} session session-result.txt".encode()
            end = f"DL23_SESSION_EXPORT_END_{HOST}".encode()
            record = raw.split(begin, 1)[1].split(end, 1)[0]
            mutated = raw.replace(end, begin + record + end, 1)
            with self.assertRaisesRegex(ValueError, "Repeated"):
                decode_exports(mutated, HOST, INSTANCES, "closed")
        with self.subTest("instance arguments"):
            with self.assertRaisesRegex(ValueError, "Invalid instances"):
                decode_exports(raw, HOST, (INST_A, INST_A), "closed")

    def test_swapped_instances(self):
        raw, _ = build("live", instance_order=(INST_B, INST_A))
        with self.assertRaisesRegex(ValueError, "Wrapper order"):
            decode_exports(raw, HOST, INSTANCES, "live")

    def test_missing(self):
        with self.subTest("session file"):
            raw, _ = build("closed", skip={"manager-status.log"})
            with self.assertRaisesRegex(ValueError, "Incomplete export"):
                decode_exports(raw, HOST, INSTANCES, "closed")
        with self.subTest("live missing lines"):
            raw, _ = build("live", omit_missing=True)
            with self.assertRaisesRegex(ValueError, "Incomplete export"):
                decode_exports(raw, HOST, INSTANCES, "live")
        with self.subTest("wrapper"):
            raw, _ = build("live")
            cut = raw.split(f"DL23_SESSION_EXPORT_END_{HOST}".encode(), 1)[0].rstrip(b"\n")
            with self.assertRaisesRegex(ValueError, "Incomplete export"):
                decode_exports(cut, HOST, INSTANCES, "live")

    def test_inserted_noise_not_salvaged(self):
        raw, expected = build("live", noise=True)
        with self.assertRaisesRegex(ValueError, "Noise in export block"):
            decode_exports(raw, HOST, INSTANCES, "live")
        interior = []
        for name in LIVE_TERMINAL:
            payload = _payload(INST_A, name)
            interior.extend(_dl11(INST_A, name, payload, len(payload)))
        interior.append("inserted-noise")
        interior.append(f"DL11_RECORD_MISSING {INST_A} grid-final.txt errno=2")
        interior.append(f"DL11_RECORD_MISSING {INST_A} result.json errno=2")
        spec = importlib.util.spec_from_file_location("terminal_records_decode_reference", TERMINAL_DECODER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        skipped = module.decode(("\n".join(interior) + "\n").encode(), INST_A, phase="live")
        self.assertEqual(set(skipped), set(LIVE_TERMINAL))
        self.assertEqual(skipped["pty-rx.bin"]["data"], expected["applications"][0]["terminal"]["pty-rx.bin"][0])
        blank, _ = build("closed")
        blank = blank.replace(b"\nDL11_RECORD_", b"\n\nDL11_RECORD_", 1)
        with self.assertRaisesRegex(ValueError, "Noise in export block"):
            decode_exports(blank, HOST, INSTANCES, "closed")

    def test_records_outside_the_span_raise(self):
        raw, _ = build("closed")
        ahead = b"DL22_RECORD_BEGIN " + HOST.encode() + b" session session-result.txt bytes=1 chunk_bytes=64\n" + raw
        with self.assertRaisesRegex(ValueError, "Record outside export"):
            decode_exports(ahead, HOST, INSTANCES, "closed")
        behind = raw + f"DL11_RECORD_MISSING {INST_A} grid-final.txt errno=2\n".encode()
        with self.assertRaisesRegex(ValueError, "Record outside export"):
            decode_exports(behind, HOST, INSTANCES, "closed")

    def test_corrupted_sha(self):
        raw, _ = build("closed")
        start = raw.index(b"sha256=") + len(b"sha256=")
        replacement = b"0" if raw[start:start + 1] != b"0" else b"1"
        mutated = raw[:start] + replacement + raw[start + 1:]
        with self.assertRaisesRegex(ValueError, "corrupt"):
            decode_exports(mutated, HOST, INSTANCES, "closed")

    def test_wrapper_reorder_and_unknown_instance(self):
        raw, _ = build("live")
        with self.subTest("reorder"):
            inserted = f"DL23_APP_EXPORT_BEGIN_{HOST} instance={INST_A} phase=live\n".encode() + raw
            with self.assertRaisesRegex(ValueError, "Wrapper order"):
                decode_exports(inserted, HOST, INSTANCES, "live")
        with self.subTest("unknown"):
            old = f"DL23_APP_EXPORT_BEGIN_{HOST} instance={INST_A}".encode()
            new = f"DL23_APP_EXPORT_BEGIN_{HOST} instance={'e' * 48}".encode()
            with self.assertRaisesRegex(ValueError, "Unknown instance"):
                decode_exports(raw.replace(old, new, 1), HOST, INSTANCES, "live")
        with self.subTest("malformed export"):
            old = f"DL23_SESSION_EXPORT_END_{HOST} rc=0".encode()
            new = f"DL23_SESSION_EXPORT_END_{HOST} rc=".encode()
            with self.assertRaisesRegex(ValueError, "Malformed export"):
                decode_exports(raw.replace(old, new, 1), HOST, INSTANCES, "live")

    def test_supplied_ids_are_not_derived(self):
        counter = ("0001" + HOST[:44], "0002" + HOST[:44])
        self.assertNotEqual(counter, INSTANCES)
        raw, expected = build("live", instances=counter)
        with self.assertRaisesRegex(ValueError, "Unknown instance"):
            decode_exports(raw, HOST, INSTANCES, "live")
        decoded = decode_exports(raw, HOST, counter, "live")
        self.assertEqual(tuple(item["instance"] for item in decoded["applications"]), counter)
        self.assertEqual(decoded["applications"][0]["logs"]["application-stdout.log"]["data"],
                         expected["applications"][0]["logs"]["application-stdout.log"][0])

    def test_frozen_decoder_hashes(self):
        self.assertEqual(hashlib.sha256(DESKTOP_DECODER.read_bytes()).hexdigest(), DESKTOP_DECODER_SHA256)
        self.assertEqual(hashlib.sha256(TERMINAL_DECODER.read_bytes()).hexdigest(), TERMINAL_DECODER_SHA256)
        self.assertEqual(DESKTOP_DECODER_SHA256, "60ec15988f3ce5a7a03e198b510129070d00eec091525b0bd9fa029f09d3b5df")
        self.assertEqual(TERMINAL_DECODER_SHA256, "c8f2193dbf843d1970b56f9c0aaed06f2aab43c6348a66f5201a08408977c37f")


if __name__ == "__main__":
    unittest.main()
