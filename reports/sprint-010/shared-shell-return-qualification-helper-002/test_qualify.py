#!/usr/bin/env python3
"""Host tests for the candidate-002 checker. No device access."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import qualify


HOST = "ab" * 24
FROZEN_ATTEMPT001_HELPER = "0bc5a2d7e24dd6c5ce1ad0c291717d8cd5442abfc7feff405a8a671344db4406"
CAPTURE_PAYLOAD = bytes.fromhex(
    "0009a253000000008000000000000000"
    "36000000100000008c00000000000000"
    "0e000000000000000000000000000000"
    "37000000300000008f00000000000000"
    "63686f6e6b2d7465726d696e616c2d64617277696e006f70"
    "000000000000000000000000000000000000000000000000"
    "ed5891f1000000000000000000000000"
)


def _chunks(payload):
    return (len(payload) + 63) // 64


def _dl22(nonce, kind, name, payload):
    digest = hashlib.sha256(payload).hexdigest()
    size = len(payload)
    lines = [f"DL22_RECORD_BEGIN {nonce} {kind} {name} bytes={size} chunk_bytes=64"]
    for index in range(_chunks(payload)):
        chunk = payload[index * 64:(index + 1) * 64]
        lines.append(f"DL22_RECORD_HEX {nonce} {kind} {name} index={index} {chunk.hex()}")
    lines.append(
        f"DL22_RECORD_END {nonce} {kind} {name} bytes={size} chunks={_chunks(payload)} "
        f"file_bytes_after={size} sha256={digest}"
    )
    return lines


def _spawn(pid):
    return (
        "SpawnResultV1 { version: 1, stage: 10, pid: %d, primary_error: 0, spawn_error: 0, "
        "attr_destroy_error: 0, actions_destroy_error: 0, fd_close_error: 0, fd_close_failures: 0, "
        "requested_flags: 16398, observed_flags: 16398, child_created: 1, duplicate_count: 4, reserved: 0 }"
        % pid
    )


def _attempt_line(sequence, pid, wait, failure, status_bytes):
    instance = ("0001" if sequence == 0 else "0002") + HOST[:44]
    failure_text = "None" if failure is None else 'Some("%s")' % failure
    failure_ms = "None" if failure is None else "Some(4)"
    cleanup = "None" if wait != 0 else "Some(Cleanup { complete: true, raw_wait_status: 9, term_sent: true, kill_sent: false })"
    return (
        "attempt=%d source=explicit_startup request=Some(%d) instance=Some(\"%s\") "
        "pid=Some(%d) created_ms=1 failure=%s failure_ms=%s status_bytes=%d wait=Some(%d) "
        "wait_error=None cleanup=%s native_return=Some(0) native=Some(%s)"
        % (sequence, sequence + 1, instance, pid, failure_text, failure_ms, status_bytes, wait, cleanup, _spawn(pid))
    )


def _gates(sequence, ready):
    lines = []
    for gate in qualify.GATES:
        observed = "None"
        if gate in ("Admission", "StatusNonblocking", "SpawnReturned", "PidOwned", "StatusEof", "AppReaped"):
            observed = "Some(2)"
        if ready and gate in ("AppReady", "AppCleanup"):
            observed = "Some(3)"
        lines.append(f"attempt={sequence} gate={gate} observed_ms={observed}")
    return lines


def _session(reason0, reason1):
    lines = [
        "CHONKDESKTOP/1 initialized=1 input=both shell=shared native_runtime_qualification=not_inferred",
        "CHONKDESKTOP/1 elapsed_ms=9 cleanup=Incomplete deadline=false frame_error=None manager_failed=true",
        "CHONKLAUNCH/1 snapshot attempts=2 journal_error=None app_internal_gates=unknown_until_final_app_record",
        _attempt_line(0, 14, 9, "missing CLEANUP at EOF", 0),
        f"attempt=0 exit_reason={reason0}",
        *_gates(0, False),
        _attempt_line(1, 15, 0, None, 80),
        f"attempt=1 exit_reason={reason1}",
        *_gates(1, False),
    ]
    return ("\n".join(lines) + "\n").encode()


def _journal(reason0, reason1):
    lines = ["CHONKLAUNCH/1 scope=manager app_internal_gates=unknown_until_final_app_record"]
    for sequence, pid, reason, wait, status_bytes, failure in (
        (0, 14, reason0, 9, 0, "missing CLEANUP at EOF"),
        (1, 15, reason1, 0, 80, None),
    ):
        instance = ("0001" if sequence == 0 else "0002") + HOST[:44]
        lines.append(
            "CHONKLAUNCH/1 attempt=%d ms=1 request=Some(%d) instance=Some(\"%s\") "
            "source=explicit_startup event=requested" % (sequence, sequence + 1, instance)
        )
        for gate in ("Admission", "StatusNonblocking", "SpawnReturned", "PidOwned", "StatusEof", "AppReaped"):
            lines.append(f"CHONKLAUNCH/1 attempt={sequence} ms=2 event={gate}")
        lines.append(f"CHONKLAUNCH/1 attempt={sequence} native_return=0 native={_spawn(pid)}")
        lines.append(f"CHONKLAUNCH/1 attempt={sequence} ms=3 event=ExitReason exit_reason={reason}")
        if failure:
            lines.append(f'CHONKLAUNCH/1 attempt={sequence} ms=4 failure="{failure}"')
        cleanup = "None" if wait != 0 else "Some(Cleanup { complete: true, raw_wait_status: 9, term_sent: true, kill_sent: false })"
        lines.append(
            "CHONKLAUNCH/1 attempt=%d status_bytes=%d raw_app_wait=Some(%d) wait_error=None cleanup=%s"
            % (sequence, status_bytes, wait, cleanup)
        )
    return ("\n".join(lines) + "\n").encode()


def _console(reason0, reason1):
    session = _session(reason0, reason1)
    journal = _journal(reason0, reason1)
    ids = qualify._instances(HOST)
    lines = [
        f"DL23_SESSION_EXPORT_BEGIN_{HOST} phase=closed",
        *_dl22(HOST, "session", "session-result.txt", session),
        *_dl22(HOST, "session", "manager-status.log", journal),
        f"DL23_SESSION_EXPORT_END_{HOST} rc=0",
        f"DL23_APP_EXPORT_BEGIN_{HOST} instance={ids[0]} phase=closed",
        f"DL11_RECORD_MISSING {ids[0]} pty-rx.bin errno=2",
        f"DL23_APP_TERMINAL_EXPORT_END_{HOST} instance={ids[0]} rc=1",
        *_dl22(ids[0], "application", "application-stdout.log", b""),
        *_dl22(ids[0], "application", "application-stderr.log", b""),
        f"DL23_APP_EXPORT_END_{HOST} instance={ids[0]} terminal=1 logs=0",
        f"DL23_DESKTOP_CLOSED_EXPORT_{HOST} failed=1 physical_input_proven=false",
        f"DL23_DESKTOP_FINAL_STATUS_{HOST} desktop=1 export_failed=1",
    ]
    return ("\n".join(lines) + "\n").encode()


class Candidate002Helper(unittest.TestCase):
    def test_pins_and_attempt001_helper_unchanged(self):
        pins = qualify.bind()
        self.assertEqual(pins["criteria"], qualify.CRITERIA_SHA256)
        frozen = Path(qualify.ROOT) / "reports/sprint-010/shared-shell-return-qualification-helper/qualify.py"
        self.assertEqual(hashlib.sha256(frozen.read_bytes()).hexdigest(), FROZEN_ATTEMPT001_HELPER)

    def test_snapshot_requires_exit_reason_before_gates(self):
        raw = _session("not_observed", "query_failed errno=2").decode().splitlines()
        raw.pop(4)
        with self.assertRaisesRegex(ValueError, "exit_reason"):
            qualify.parse_session_result(("\n".join(raw) + "\n").encode())

    def test_not_observed_fails_d1_and_sigkill_stays_unqualified(self):
        with tempfile.TemporaryDirectory() as tmp:
            guest = Path(tmp) / "guest"
            guest.mkdir()
            (guest / "console.bin").write_bytes(_console("not_observed", "query_failed errno=2"))
            result = qualify.qualify(tmp)
        self.assertEqual(result["qualification"], "NOT_QUALIFIED")
        self.assertFalse(result["runtime_qualified"])
        self.assertEqual(result["checks"]["C6"]["status"], "FAIL")
        self.assertEqual(result["checks"]["D1"]["status"], "FAIL")
        self.assertIn("not_observed", result["checks"]["D1"]["detail"])
        self.assertEqual(result["checks"]["C5"]["status"], "FAIL")
        self.assertIn("wait is 9", result["checks"]["C5"]["detail"])
        self.assertEqual(result["checks"]["C4"]["status"], "FAIL")
        self.assertIn("records not accepted", result["checks"]["C4"]["detail"])
        self.assertNotEqual(result["groups"]["diagnostic"]["status"], "PASS")

    def test_full_reason_d1_pass_does_not_upgrade_failures(self):
        reason = "full namespace=2 code=0x9 flags=0x0 payload_bytes=128 payload_hex=" + CAPTURE_PAYLOAD.hex()
        with tempfile.TemporaryDirectory() as tmp:
            guest = Path(tmp) / "guest"
            guest.mkdir()
            (guest / "console.bin").write_bytes(_console(reason, "query_failed errno=2"))
            (guest / "console-tx.bin").write_bytes(b"not-the-launcher")
            (guest / "console-result.json").write_text("{}\n")
            result = qualify.qualify(tmp)
        self.assertEqual(result["qualification"], "NOT_QUALIFIED")
        self.assertEqual(result["checks"]["D1"]["status"], "PASS")
        self.assertIn("kcdata_pid=14", result["checks"]["D1"]["detail"])
        self.assertIn("kcdata_procname=chonk-terminal-darwin", result["checks"]["D1"]["detail"])
        self.assertIn("name_tail_hex=6f70", result["checks"]["D1"]["detail"])
        self.assertEqual(result["checks"]["C5"]["status"], "FAIL")
        self.assertEqual(result["checks"]["C6"]["status"], "FAIL")
        self.assertEqual(result["checks"]["C1_runtime_tx"]["status"], "FAIL")
        self.assertNotIn("D1", result["checks"]["C9"]["detail"])

    def test_sender_comes_only_from_pid_and_procname_items(self):
        found = qualify.attribute_signal_payload(CAPTURE_PAYLOAD)
        self.assertEqual(found["sender_pid"], 14)
        self.assertEqual(found["sender_name"], "chonk-terminal-darwin")
        begin = bytes.fromhex("0009a253000000008000000000000000")
        end = bytes.fromhex("ed5891f1000000000000000000000000")
        nameless = qualify.attribute_signal_payload(begin + end)
        self.assertIsNone(nameless["sender_pid"])
        self.assertIsNone(nameless["sender_name"])

    def test_oversize_payload_fails_d1(self):
        payload = b"ab" * 321
        text = "full namespace=2 code=0x9 flags=0x0 payload_bytes=321 payload_hex=" + payload.hex()
        session = {
            "attempts": [
                {"sequence": 0, "wait": 9, "exit_reason": text},
                {"sequence": 1, "wait": 0, "exit_reason": "query_failed errno=2"},
            ]
        }
        journal = {0: {"exit_reasons": [text]}, 1: {"exit_reasons": ["query_failed errno=2"]}}
        graded = qualify.grade_d1(session, journal)
        self.assertEqual(graded["status"], "FAIL")
        self.assertIn("320", graded["detail"])


if __name__ == "__main__":
    unittest.main()
