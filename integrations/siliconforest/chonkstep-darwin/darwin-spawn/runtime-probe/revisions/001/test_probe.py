#!/usr/bin/env python3
"""Synthetic record/classifier and source-policy tests; no C build or execution."""
import copy
import json
from pathlib import Path
import re
import unittest

from qualify import CHILD, evaluate, strict_json

ROOT = Path(__file__).resolve().parent
NONCE = "a" * 48


def synthetic():
    """Explicitly invented test data, never a runtime receipt."""
    stdout = f"CHONKFD/1 OUT {NONCE}\n".encode()
    stderr = f"CHONKFD/1 ERR {NONCE}\n".encode()
    status = f"CHONKFD/1 RESULT {NONCE} 123 256 15 0 0 0\n".encode()
    record = {
        "schema": 1, "nonce": NONCE, "child_path": CHILD, "darwin_arm64": True,
        "limit_checked": True, "soft_limit": 256, "hard_limit": 10240,
        "soft_infinite": False, "hard_infinite": False, "scan_bound": 256,
        "ambient_open_noncloexec": True, "ambient_received_noncloexec": True,
        "ambient_held_through_spawn": True, "unknown_rights_cleanup": False,
        "spawn_called": True, "spawn_return": 0,
        "spawn": {"version": 1, "stage": 10, "pid": 123, "primary_error": 0,
                  "spawn_error": 0, "attr_destroy_error": 0, "actions_destroy_error": 0,
                  "fd_close_error": 0, "fd_close_failures": 0, "requested_flags": 0x400E,
                  "observed_flags": 0x400E, "child_created": 1, "duplicate_count": 4},
        "input_sent": len(f"CHONKFD/1 IN {NONCE}\n"), "stdout_bytes": len(stdout),
        "stderr_bytes": len(stderr), "status_bytes": len(status), "stdout_exact": True,
        "stderr_exact": True, "all_output_eof": True, "output_overflow": False,
        "child_report": {"valid": True, "pid": 123, "scan_bound": 256, "low_mask": 15,
                         "extra_fds": 0, "failure_bits": 0, "first_errno": 0},
        "wait": {"reaped": True, "lost": False, "raw_status": 0, "error": 0,
                 "term_sent": False, "kill_sent": False},
        "error_overflow": False, "errors": [],
    }
    return record, stdout, stderr, status


def policy(parent, child, common):
    failures = []
    if parent.count("chonk_darwin_spawn_v1(&request,&p->spawn)") != 1:
        failures.append("real_adapter_once")
    if re.search(r"\b(fork|posix_spawn|posix_spawnp|system|popen|execve)\s*\(", parent + child):
        failures.append("fallback_spawn")
    if "CHONK_SPAWN_TEST_API" in parent + child + common:
        failures.append("test_spawn_substitution")
    if "#if !defined(__APPLE__) || !defined(__aarch64__)" not in common:
        failures.append("pinned_target_guard")
    if "_Static_assert(PROBE_REQUIRED_FLAGS==0x400e" not in common:
        failures.append("full_flag_assertion")
    for index in range(4):
        if f"request.source_fds[{index}]=" not in parent:
            failures.append("selected_destination")
    adopt = parent.find("if (p->spawn.child_created && p->spawn.pid>0)")
    cleanup_error = parent.find('if (p->spawn_return) fail(p,"managed_spawn"')
    if adopt < 0 or cleanup_error < adopt:
        failures.append("adopt_despite_local_error")
    if "if (!child->live) return;" not in parent or "child->live=0;child->lost=1" not in parent:
        failures.append("sole_wait_owner")
    cleanup = parent[parent.index("static void cleanup_child"):parent.index("static int parse_report")]
    if cleanup.count("check_wait(p);") < 2 or cleanup.count("if (!p->child.live) return;") < 2:
        failures.append("check_before_signal")
    if "kill(p->child.pid,SIGTERM)" not in cleanup or "kill(p->child.pid,SIGKILL)" not in cleanup:
        failures.append("positive_owned_signals")
    if "kill(-" in parent or "waitpid(-1" in parent:
        failures.append("unowned_process_operation")
    if "p->limit.rlim_cur==RLIM_INFINITY || p->limit.rlim_cur<=3 || p->limit.rlim_cur>PROBE_SCAN_CAP" not in parent:
        failures.append("actual_limit_fail_closed")
    if "fd<(int)bound" not in child or "fcntl(fd,F_GETFD)" not in child:
        failures.append("actual_bounded_inventory")
    if "probe_before(deadline)" not in common or "probe_before(deadline)" not in parent or "probe_before(deadline)" not in child:
        failures.append("real_io_deadlines")
    if "probe_ignore_pipe()" not in parent or "probe_ignore_pipe()" not in child:
        failures.append("sigpipe_checked_both")
    if "if (io_setup_error) return 68;" not in child:
        failures.append("refuse_blocking_io")
    if "if (!(flags&O_NONBLOCK)) return EIO;" not in common or "if (!(flags&FD_CLOEXEC)) return EIO;" not in common:
        failures.append("checked_flag_readback")
    if "channel->overflow=1" not in parent or 'fail(p,"output_saturation"' not in parent:
        failures.append("bounded_output_saturation")
    if "p->unknown_rights_cleanup=1" not in parent:
        failures.append("uncertain_received_rights")
    return failures


class SyntheticRecordTests(unittest.TestCase):
    def test_synthetic_complete_example_only_checks_classifier(self):
        record, out, err, status = synthetic()
        self.assertEqual(evaluate(record, out, err, status, 0), [])
        self.assertEqual(strict_json(json.dumps(record).encode()), record)

    def test_every_required_field_missing_fails(self):
        original, out, err, status = synthetic()
        for field in original:
            record = copy.deepcopy(original)
            del record[field]
            with self.subTest(field=field):
                self.assertTrue(evaluate(record, out, err, status, 0))
        for group in ("spawn", "child_report", "wait"):
            for field in original[group]:
                record = copy.deepcopy(original)
                del record[group][field]
                with self.subTest(group=group, field=field):
                    self.assertTrue(evaluate(record, out, err, status, 0))

    def test_runtime_fdlimit_infinity_too_large_too_small_or_mismatch(self):
        for value in (0, 3, 65537, 2**64 - 1, True):
            record, out, err, status = synthetic()
            record["soft_limit"] = record["scan_bound"] = value
            with self.subTest(value=value):
                self.assertTrue(evaluate(record, out, err, status, 0))
        for key, value in (("soft_infinite", True), ("scan_bound", 257), ("hard_limit", 128)):
            record, out, err, status = synthetic()
            record[key] = value
            self.assertTrue(evaluate(record, out, err, status, 0))

    def test_missing_flag_full_set_and_local_cleanup_failure_cannot_pass(self):
        for flags in (0, 14, 0x4000, 0x4006):
            record, out, err, status = synthetic()
            record["spawn"]["requested_flags"] = record["spawn"]["observed_flags"] = flags
            self.assertTrue(evaluate(record, out, err, status, 0))
        for key in ("primary_error", "spawn_error", "attr_destroy_error", "actions_destroy_error", "fd_close_error", "fd_close_failures"):
            record, out, err, status = synthetic()
            record["spawn"][key] = 5
            self.assertTrue(evaluate(record, out, err, status, 0))
        record, out, err, status = synthetic()
        record["spawn_return"] = 5  # PID still present/reaped: launch still failed.
        self.assertTrue(evaluate(record, out, err, status, 0))

    def test_partial_broken_saturated_endpoints_and_unknown_cleanup_fail(self):
        record, out, err, status = synthetic()
        for data in (out[:-1], out + b"x", b"", b"x" * 257):
            self.assertTrue(evaluate(record, data, err, status, 0))
        for key, value in (("input_sent", 1), ("all_output_eof", False), ("output_overflow", True), ("unknown_rights_cleanup", True), ("ambient_received_noncloexec", False)):
            changed = copy.deepcopy(record)
            changed[key] = value
            self.assertTrue(evaluate(changed, out, err, status, 0))

    def test_status_identity_pid_bound_extra_fds_or_bits_fail(self):
        record, out, err, status = synthetic()
        for data in (status.replace(b"a", b"b"), status.replace(b"123", b"124"), status.replace(b"256", b"257"), status.replace(b"15 0 0 0", b"15 1 4 22"), status + b"\n", status[:-1], status + b"\0", status.replace(b" 123 ", b" +123 "), status.replace(b" 123 ", b" 0123 "), b"x" * 257):
            with self.subTest(data=data):
                self.assertTrue(evaluate(record, out, err, data, 0))

    def test_reap_alone_after_term_kill_echild_or_unknown_wait_is_not_pass(self):
        for key, value in (("term_sent", True), ("kill_sent", True), ("lost", True), ("error", 10), ("raw_status", -1), ("raw_status", 9), ("reaped", False)):
            record, out, err, status = synthetic()
            record["wait"][key] = value
            self.assertTrue(evaluate(record, out, err, status, 0))
        record, out, err, status = synthetic()
        self.assertTrue(evaluate(record, out, err, status, 1))
        self.assertTrue(evaluate(record, out, err, status, False))

    def test_json_duplicate_fields_nonfinite_oversize_and_bool_numeric_fail(self):
        for payload in (b'{"schema":1,"schema":1}', b'{"n":NaN}', b'{}' * 7000, b'[]'):
            with self.assertRaises(ValueError):
                strict_json(payload)
        record, out, err, status = synthetic()
        record["spawn"]["pid"] = True
        self.assertTrue(evaluate(record, out, err, status, 0))


class SourcePolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parent = (ROOT / "parent.c").read_text()
        cls.child = (ROOT / "child.c").read_text()
        cls.common = (ROOT / "probe_common.h").read_text()

    def test_declared_source_policy(self):
        self.assertEqual(policy(self.parent, self.child, self.common), [])

    def test_negative_controls_reject_unsafe_source_changes(self):
        mutations = [
            (self.parent.replace("chonk_darwin_spawn_v1(&request,&p->spawn)", "posix_spawn(&pid,path,0,0,args,env)"), self.child, self.common),
            (self.parent.replace("if (p->spawn.child_created && p->spawn.pid>0)", "if (!p->spawn_return && p->spawn.child_created && p->spawn.pid>0)"), self.child, self.common),
            (self.parent.replace("check_wait(p);", "/* removed */"), self.child, self.common),
            (self.parent.replace("p->limit.rlim_cur>PROBE_SCAN_CAP", "0"), self.child, self.common),
            (self.parent, self.child, self.common.replace("probe_before(deadline)", "0")),
            (self.parent, self.child.replace("probe_ignore_pipe()", "0"), self.common),
            (self.parent, self.child.replace("if (io_setup_error) return 68;", ""), self.common),
            (self.parent, self.child, self.common.replace("if (!(flags&O_NONBLOCK)) return EIO;", "")),
            (self.parent.replace("p->unknown_rights_cleanup=1", "/* forgotten */"), self.child, self.common),
        ]
        for index, source in enumerate(mutations):
            with self.subTest(index=index):
                self.assertTrue(policy(*source))


if __name__ == "__main__":
    unittest.main(verbosity=2)
