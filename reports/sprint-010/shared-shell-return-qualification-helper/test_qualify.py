#!/usr/bin/env python3
"""Host tests for the shared-shell return checker. No device access."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import qualify


HOST = "ab" * 24
ID1 = "0001" + HOST[:44]
ID2 = "0002" + HOST[:44]
STEPS = qualify.STEPS


def _chunks(payload):
    return (len(payload) + 63) // 64


def _record(prefix, nonce, kind, name, payload):
    digest = hashlib.sha256(payload).hexdigest()
    size = len(payload)
    kind_text = "" if kind is None else kind + " "
    lines = [f"{prefix}_BEGIN {nonce} {kind_text}{name} bytes={size} chunk_bytes=64"]
    for index in range(_chunks(payload)):
        chunk = payload[index * 64:(index + 1) * 64]
        lines.append(f"{prefix}_HEX {nonce} {kind_text}{name} index={index} {chunk.hex()}")
    lines.append(
        f"{prefix}_END {nonce} {kind_text}{name} bytes={size} chunks={_chunks(payload)} "
        f"file_bytes_after={size} sha256={digest}"
    )
    return lines


def _dl22(nonce, kind, name, payload):
    return _record("DL22_RECORD", nonce, kind, name, payload)


def _dl11(nonce, name, payload):
    return _record("DL11_RECORD", nonce, None, name, payload)


def _spawn(pid):
    return (
        "SpawnResultV1 { version: 1, stage: 10, pid: %d, primary_error: 0, spawn_error: 0, "
        "attr_destroy_error: 0, actions_destroy_error: 0, fd_close_error: 0, fd_close_failures: 0, "
        "requested_flags: 16398, observed_flags: 16398, child_created: 1, duplicate_count: 4, reserved: 0 }"
        % pid
    )


def _cleanup(status):
    return (
        "Cleanup { complete: true, raw_wait_status: %d, term_sent: false, kill_sent: false }"
        % status
    )


def _apps():
    return (
        {"seq": 0, "request": 1, "instance": ID1, "pid": 14, "shell": 140, "pty": 9, "wait": 0},
        {"seq": 1, "request": 2, "instance": ID2, "pid": 15, "shell": 150, "pty": 15, "wait": 0},
    )


def _session(attempts, journal_waits=None, attempt_token="wait"):
    lines = [
        "CHONKDESKTOP/1 initialized=1 input=both shell=shared native_runtime_qualification=not_inferred",
        "CHONKDESKTOP/1 elapsed_ms=60000 cleanup=Complete deadline=true frame_error=None manager_failed=false",
        "CHONKLAUNCH/1 snapshot attempts=%d journal_error=None app_internal_gates=unknown_until_final_app_record" % len(attempts),
    ]
    journal = ["CHONKLAUNCH/1 scope=manager app_internal_gates=unknown_until_final_app_record"]
    for app in attempts:
        cleanup = _cleanup(app["pty"])
        native = _spawn(app["pid"])
        lines.append(
            "attempt=%d source=explicit_startup request=Some(%d) instance=Some(\"%s\") "
            "pid=Some(%d) created_ms=10 failure=None failure_ms=None status_bytes=12 "
            "%s=Some(%d) wait_error=None cleanup=Some(%s) "
            "native_return=Some(0) native=Some(%s)"
            % (app["seq"], app["request"], app["instance"], app["pid"], attempt_token, app["wait"], cleanup, native)
        )
        for gate in qualify.GATES:
            observed = "None" if gate in ("TermAttempt", "KillAttempt") else "Some(10)"
            lines.append("attempt=%d gate=%s observed_ms=%s" % (app["seq"], gate, observed))
        journal.append(
            "CHONKLAUNCH/1 attempt=%d ms=10 request=Some(%d) instance=Some(\"%s\") source=explicit_startup event=requested"
            % (app["seq"], app["request"], app["instance"])
        )
        for gate in qualify.REQUIRED_OBSERVED:
            journal.append("CHONKLAUNCH/1 attempt=%d ms=10 event=%s" % (app["seq"], gate))
        journal.append("CHONKLAUNCH/1 attempt=%d native_return=0 native=%s" % (app["seq"], native))
        waited = app["wait"] if journal_waits is None else journal_waits.get(app["seq"], app["wait"])
        journal.append(
            "CHONKLAUNCH/1 attempt=%d status_bytes=12 raw_app_wait=Some(%d) wait_error=None cleanup=Some(%s)"
            % (app["seq"], waited, cleanup)
        )
    return ("\n".join(lines) + "\n").encode(), ("\n".join(journal) + "\n").encode()


def _json(value):
    return (json.dumps(value) + "\n").encode()


def _boundary():
    return {
        "pre_exec_fd_check_observed": True,
        "pre_exec_fd_scan_bound_exclusive": 1024,
        "exec_status_pipe_eof_observed": True,
        "shell_exec_checked": True,
        "inventory_scope": "pre_exec_only_not_after_exec_inventory",
    }


def _grid(rx):
    grid = qualify.Grid(20, 6)
    grid.feed(rx)
    return grid.text().encode()


def _terminal(app, rx):
    boundary = _boundary()
    child = _json({
        "parent_pid": app["pid"],
        "child_pid": app["shell"],
        "setup_checked": False,
        "job_control": "disabled; foreground group must remain owned shell",
    })
    ready = _json({
        "parent_pid": app["pid"],
        "child_pid": app["shell"],
        "sid": app["shell"],
        "pgid": app["shell"],
        "session_and_ctty_checked_in_child": True,
        "exec_status_pipe_closed": True,
        "exec_boundary": boundary,
        "shell": "/bin/sh",
    })
    result = _json({
        "schema": 1,
        "instance": app["instance"],
        "app_pid": app["pid"],
        "shell": "/bin/sh",
        "term": "dumb",
        "child_pid": app["shell"],
        "child_exec_boundary": boundary,
        "session": {
            "configure_serial": None,
            "ready": True,
            "keymap": True,
            "pty_exec_checked": True,
            "exec_boundary": boundary,
            "ssd": True,
            "frames": 1,
            "commits": 1,
            "pty_rx_bytes": len(rx),
            "pty_tx_bytes": 0,
            "input_accepted_bytes": 0,
            "input_unsent_bytes": 0,
            "unsupported_terminal_sequences": 0,
            "stop_cause": "returned",
            "term_signal": 0,
            "window_closed": False,
            "pty_eof": True,
        },
        "cleanup": {
            "complete": True,
            "status": app["pty"],
            "term_sent": False,
            "kill_sent": False,
            "error": None,
        },
        "errors": [],
    })
    return {
        "pty-rx.bin": rx,
        "pty-tx.bin": b"",
        "child.json": child,
        "child-ready.json": ready,
        "grid-final.txt": _grid(rx),
        "result.json": result,
    }


def _tx(nonce):
    challenge = (
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; "
        f"printf '\\nDL9_BEGIN_%s%s\\n' '{nonce[:24]}' '{nonce[24:]}'; "
        "{ uname -s; uname -m; uname -r; } 2>/dev/null; "
        f"printf 'DL9_END_%s%s\\n' '{nonce[:24]}' '{nonce[24:]}'\r"
    ).encode()
    launcher = f"/usr/bin/darwin-desktop-test '{nonce}'\r".encode()
    return challenge + launcher


def _console(session, journal, terminals, *, noise=False, terminal_rc=0, failed=0, desktop=0, returned=True):
    lines = [
        "Darwin",
        "arm64",
        "25.5.0",
        f"DL23_DESKTOP_BEGIN_{HOST} source=explicit_startup requested_terminals=2 physical_input_proven=false",
        f"DL23_SESSION_EXPORT_BEGIN_{HOST} phase=closed",
    ]
    for name, payload in (("session-result.txt", session), ("manager-status.log", journal)):
        lines.extend(_dl22(HOST, "session", name, payload))
        if noise and name == "session-result.txt":
            lines.append("inserted-noise")
    lines.append(f"DL23_SESSION_EXPORT_END_{HOST} rc=0")
    for index, (instance, files) in enumerate(terminals):
        lines.append(f"DL23_APP_EXPORT_BEGIN_{HOST} instance={instance} phase=closed")
        for name in ("pty-rx.bin", "pty-tx.bin", "child.json", "child-ready.json", "grid-final.txt", "result.json"):
            lines.extend(_dl11(instance, name, files[name]))
        rc = terminal_rc if index == 0 else 0
        lines.append(f"DL23_APP_TERMINAL_EXPORT_END_{HOST} instance={instance} rc={rc}")
        lines.extend(_dl22(instance, "application", "application-stdout.log", b"stdout\n"))
        lines.extend(_dl22(instance, "application", "application-stderr.log", b"stderr\n"))
        lines.append(f"DL23_APP_EXPORT_END_{HOST} instance={instance} terminal={rc} logs=0")
    lines.append(f"DL23_DESKTOP_CLOSED_EXPORT_{HOST} failed={failed} physical_input_proven=false")
    lines.append(f"DL23_DESKTOP_FINAL_STATUS_{HOST} desktop={desktop} export_failed={failed}")
    if returned:
        lines.extend([
            "",
            "CHONKSTEP SHARED SHELL MANAGED SESSION RETURNED",
            f"DL23_DESKTOP_RETURN_{HOST}",
            "Darwin",
            "arm64",
            "25.5.0",
            f"DL23_DESKTOP_END_{HOST} physical_input_proven=false",
        ])
    return ("\n".join(lines) + "\n").encode()


def _recovery(root, attestation):
    (root / "result.json").write_text(json.dumps({
        "linux_verified": True,
        "reset_attempts": 1,
        "diagnostics_complete": True,
        "guest_worker_returncode": 1,
        "guest_host_status": "HOST_WORKER_EXITED_WITHOUT_BOOT_VERDICT",
        "status": "DARWIN_COMMAND_LINE_OBSERVED_LINUX_VERIFIED",
        "command_line_observed": True,
    }))
    (root / "baseline.json").write_text(json.dumps({"boot_id": "baseline-boot-id-not-a-device"}))
    (root / "returned-linux.json").write_text(json.dumps({"boot_id": "returned-boot-id-not-a-device"}))
    (root / "guest/capture-core.json").write_text(json.dumps({
        "diagnostics_complete": False,
        "console_capture_finalized": True,
        "console_close_error": None,
    }))
    (root / "guest/exception-context.json").write_text(json.dumps({
        "diagnostics_complete": True,
        "context_error": None,
        "diagnostics": {"complete": True},
    }))
    for number, step in enumerate(STEPS, 1):
        for phase in ("started", "completed"):
            (root / f"guest/diagnostic-{number:02d}-{phase}.json").write_text(json.dumps({"step": step}))
    if attestation:
        path = root / "attest.json"
        path.write_text(json.dumps({
            "status": "LINUX_VERIFIED_LATCH_ABSENT_CONTROLLER_LOCK_FREE",
            "linux_verified": True,
            "reset_attempts": 1,
            "extra_reset": False,
            "incomplete_latch": False,
            "lock_acquired_and_released": True,
            "cycle": "synthetic",
        }))
        return path
    return None


def _tree(attempts=None, **options):
    if attempts is None:
        attempts = _apps()
    journal_waits = options.get("journal_waits")
    token = options.get("attempt_token", "wait")
    session, journal = _session(attempts, journal_waits, token)
    rx = b"$ " if options.get("prompt", True) else b"no-prompt"
    terminals = []
    for app in _apps():
        files = _terminal(app, rx)
        if options.get("corrupt_grid"):
            files["grid-final.txt"] = b"X" + files["grid-final.txt"][1:]
        terminals.append((app["instance"], files))
    # Session attempts can differ from the two exported application blocks.
    if options.get("session_apps") is not None:
        session, journal = _session(options["session_apps"], journal_waits, token)
    raw = _console(
        session,
        journal,
        terminals,
        noise=options.get("noise", False),
        terminal_rc=options.get("terminal_rc", 0),
        failed=options.get("failed", 0),
        desktop=options.get("desktop", 0),
        returned=options.get("returned", True),
    )
    root = Path(tempfile.mkdtemp())
    guest = root / "guest"
    guest.mkdir()
    (guest / "console.bin").write_bytes(raw)
    tx = _tx(HOST)
    (guest / "console-tx.bin").write_bytes(tx)
    (guest / "console-result.json").write_text(json.dumps({
        "nonce": HOST,
        "error": None,
        "native_mode_host_write_phase": "sealed",
        "desktop_command_sent": True,
        "console_tx_sha256": hashlib.sha256(tx).hexdigest(),
    }))
    if options.get("framebuffer"):
        (guest / "framebuffer.json").write_text(json.dumps({"physical_panel_observed": False, "bytes": 1}))
    attestation = _recovery(root, options.get("recovery", False)) if options.get("recovery_files") else None
    return root, attestation


class QualifyTests(unittest.TestCase):
    def test_pins_match_and_reject_a_changed_criteria_hash(self):
        self.assertEqual(qualify.bind()["criteria"], qualify.CRITERIA_SHA256)
        original = qualify.CRITERIA_SHA256
        qualify.CRITERIA_SHA256 = "0" * 64
        try:
            with self.assertRaises(ValueError):
                qualify.bind()
        finally:
            qualify.CRITERIA_SHA256 = original

    def test_missing_capture_is_not_a_pass(self):
        root = Path(tempfile.mkdtemp())
        report = qualify.qualify(root)
        self.assertEqual(report["qualification"], "NOT_QUALIFIED")
        self.assertFalse(report["runtime_qualified"])
        self.assertFalse(report["gui_window_qualified"])
        self.assertEqual(report["checks"]["C1_sources"]["status"], "PASS")
        for key in ("C2_requests", "C3", "C4", "C5", "C6", "C7_text_return", "C8"):
            self.assertEqual(report["checks"][key]["status"], "NOT_EVALUATED", key)
        self.assertNotEqual(report["groups"]["process_pty"]["status"], "PASS")

    def test_two_apps_pty_9_and_15_do_not_qualify_gui(self):
        root, attestation = _tree(framebuffer=True, recovery_files=True, recovery=True)
        report = qualify.qualify(root, attestation)
        text = json.dumps(report)
        self.assertNotIn("baseline-boot-id-not-a-device", text)
        self.assertNotIn("returned-boot-id-not-a-device", text)
        checks = report["checks"]
        self.assertEqual(checks["C3"]["status"], "PASS")
        self.assertEqual(checks["C4"]["status"], "PASS")
        self.assertEqual(checks["C5"]["status"], "PASS")
        self.assertEqual(checks["C6"]["status"], "PASS")
        self.assertEqual(checks["C1_runtime_tx"]["status"], "PASS")
        self.assertEqual(checks["C7_text_return"]["status"], "PASS")
        self.assertEqual(checks["C7"]["status"], "NOT_EVALUATED")
        self.assertEqual(checks["C7_framebuffer"]["status"], "NOT_EVALUATED")
        self.assertIn("physical_panel_observed=False", checks["C7_framebuffer"]["detail"])
        self.assertEqual(checks["C8"]["status"], "PASS")
        self.assertIn("guest_worker_returncode=1", checks["C8"]["detail"])
        self.assertIn('"pty_wait": 9', checks["applications_observed"]["detail"])
        self.assertIn('"pty_wait": 15', checks["applications_observed"]["detail"])
        self.assertEqual(report["groups"]["gui_window"]["status"], "NOT_EVALUATED")
        self.assertEqual(report["qualification"], "NOT_QUALIFIED")
        self.assertFalse(report["gui_window_qualified"])

    def test_zero_and_one_attempt_are_not_success(self):
        for attempts in ((), _apps()[:1]):
            root, _ = _tree(session_apps=attempts)
            report = qualify.qualify(root)
            self.assertEqual(report["checks"]["C3"]["status"], "FAIL", len(attempts))
            self.assertEqual(report["checks"]["C5"]["status"], "FAIL", len(attempts))
            self.assertNotEqual(report["groups"]["process_pty"]["status"], "PASS")

    def test_snapshot_raw_app_wait_field_fails_closed(self):
        root, _ = _tree(attempt_token="raw_app_wait")
        report = qualify.qualify(root)
        self.assertEqual(report["checks"]["C3"]["status"], "FAIL")
        self.assertNotEqual(report["checks"]["C5"]["status"], "PASS")

    def test_journal_wait_disagreement_fails(self):
        root, _ = _tree(journal_waits={0: 15})
        report = qualify.qualify(root)
        self.assertEqual(report["checks"]["C3"]["status"], "FAIL")
        self.assertEqual(report["checks"]["C5"]["status"], "FAIL")
        self.assertIn("raw_app_wait", report["checks"]["C5"]["detail"])

    def test_application_wait_15_fails_and_pty_status_alone_does_not_save_it(self):
        apps = _apps()
        apps[0]["wait"] = 15
        root, _ = _tree(session_apps=apps)
        report = qualify.qualify(root)
        self.assertEqual(report["checks"]["C3"]["status"], "PASS")
        self.assertEqual(report["checks"]["C4"]["status"], "PASS")
        self.assertEqual(report["checks"]["C5"]["status"], "FAIL")
        self.assertIn("Application wait is not 0", report["checks"]["C5"]["detail"])

    def test_missing_prompt_fails_pty_only(self):
        root, _ = _tree(prompt=False)
        report = qualify.qualify(root)
        self.assertEqual(report["checks"]["C4"]["status"], "FAIL")
        self.assertIn("prompt", report["checks"]["C4"]["detail"])
        self.assertEqual(report["checks"]["C5"]["status"], "PASS")
        self.assertEqual(report["groups"]["gui_window"]["status"], "NOT_EVALUATED")

    def test_grid_mismatch_fails_closed(self):
        root, _ = _tree(corrupt_grid=True)
        report = qualify.qualify(root)
        self.assertEqual(report["checks"]["C4"]["status"], "FAIL")
        self.assertIn("grid", report["checks"]["C4"]["detail"])

    def test_export_noise_is_not_salvaged(self):
        root, _ = _tree(noise=True, failed=1, desktop=1, returned=False)
        report = qualify.qualify(root)
        self.assertEqual(report["checks"]["C6"]["status"], "FAIL")
        self.assertIn("Noise", report["checks"]["C6"]["detail"])
        self.assertEqual(report["checks"]["C3"]["status"], "NOT_EVALUATED")
        self.assertEqual(report["checks"]["C4"]["status"], "NOT_EVALUATED")
        self.assertEqual(report["checks"]["C5"]["status"], "FAIL")
        self.assertEqual(report["checks"]["C7_text_return"]["status"], "FAIL")
        self.assertEqual(report["qualification"], "NOT_QUALIFIED")

    def test_nonzero_export_status_fails_without_decoding_a_pass(self):
        root, _ = _tree(terminal_rc=1, failed=1, desktop=1, returned=False)
        report = qualify.qualify(root)
        self.assertEqual(report["checks"]["C6"]["status"], "FAIL")
        self.assertIn("Export status", report["checks"]["C6"]["detail"])
        self.assertEqual(report["checks"]["C3"]["status"], "NOT_EVALUATED")
        self.assertEqual(report["checks"]["C5"]["status"], "FAIL")
        self.assertEqual(report["checks"]["C7_text_return"]["status"], "FAIL")
        self.assertFalse(report["runtime_qualified"])

    def test_known_prompt_grid_is_twenty_by_six(self):
        grid = qualify.Grid(20, 6)
        grid.feed(b"$ ")
        rows = grid.text().split("\n")
        self.assertEqual(len(rows), 6)
        self.assertTrue(all(len(row) == 20 for row in rows))
        self.assertTrue(rows[0].startswith("$ "))


if __name__ == "__main__":
    unittest.main()
