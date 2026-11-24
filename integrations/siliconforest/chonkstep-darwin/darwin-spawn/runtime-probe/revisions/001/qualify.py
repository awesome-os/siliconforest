#!/usr/bin/env python3
"""Closed-record checks only; root must establish execution/artifact provenance."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

RESULT_CAP = 12287
CHANNEL_CAP = 256
SCAN_CAP = 65536
CHILD = "/usr/bin/darwin-spawn-whitelist-child"
TOP = set("schema nonce child_path darwin_arm64 limit_checked soft_limit hard_limit soft_infinite hard_infinite scan_bound ambient_open_noncloexec ambient_received_noncloexec ambient_held_through_spawn unknown_rights_cleanup spawn_called spawn_return spawn input_sent stdout_bytes stderr_bytes status_bytes stdout_exact stderr_exact all_output_eof output_overflow child_report wait error_overflow errors".split())
SPAWN = set("version stage pid primary_error spawn_error attr_destroy_error actions_destroy_error fd_close_error fd_close_failures requested_flags observed_flags child_created duplicate_count".split())
CHILD_REPORT = set("valid pid scan_bound low_mask extra_fds failure_bits first_errno".split())
WAIT = set("reaped lost raw_status error term_sent kill_sent".split())


def strict_json(data: bytes) -> dict:
    if not 0 < len(data) <= RESULT_CAP:
        raise ValueError("result size")

    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON field")
            result[key] = value
        return result

    def constant(_):
        raise ValueError("non-finite JSON number")

    result = json.loads(data.decode("utf-8"), object_pairs_hook=pairs, parse_constant=constant)
    if type(result) is not dict:
        raise ValueError("result is not object")
    return result


def evaluate(record: dict, stdout: bytes, stderr: bytes, status: bytes, parent_exit_code: int) -> list[str]:
    """Synthetic tests exercise these predicates; no host spawn is substituted."""
    failures = []

    def require(condition, name):
        if not condition:
            failures.append(name)

    def integer(value, low=0, high=2**32 - 1):
        return type(value) is int and low <= value <= high

    require(type(parent_exit_code) is int and parent_exit_code == 0, "actual_parent_exit")
    if type(record) is not dict or set(record) != TOP:
        return failures + ["record_fields"]
    for name, keys in (("spawn", SPAWN), ("child_report", CHILD_REPORT), ("wait", WAIT)):
        if type(record[name]) is not dict or set(record[name]) != keys:
            return failures + [name + "_fields"]
    spawn, child, wait = record["spawn"], record["child_report"], record["wait"]
    nonce = record["nonce"]
    require(type(nonce) is str and re.fullmatch("[0-9a-f]{48}", nonce) is not None, "nonce")
    if type(nonce) is not str:
        return failures
    require(type(record["schema"]) is int and record["schema"] == 1, "schema")
    require(record["child_path"] == CHILD, "fixed_child_path")
    for name in ("darwin_arm64", "limit_checked", "ambient_open_noncloexec", "ambient_received_noncloexec", "ambient_held_through_spawn", "spawn_called", "stdout_exact", "stderr_exact", "all_output_eof"):
        require(record[name] is True, name)
    for name in ("soft_infinite", "output_overflow", "error_overflow", "unknown_rights_cleanup"):
        require(record[name] is False, name)
    require(type(record["hard_infinite"]) is bool, "hard_limit_infinity_type")
    require(integer(record["soft_limit"], 4, SCAN_CAP), "finite_fd_limit")
    require(integer(record["scan_bound"], 4, SCAN_CAP) and record["scan_bound"] == record["soft_limit"], "actual_scan_bound")
    require(integer(record["hard_limit"], 0, 2**64 - 1), "hard_limit")
    if record["hard_infinite"] is True:
        require(record["hard_limit"] == 0, "hard_infinity_sentinel")
    elif integer(record["hard_limit"], 0, 2**64 - 1) and integer(record["soft_limit"]):
        require(record["hard_limit"] >= record["soft_limit"], "hard_soft_order")
    require(type(record["spawn_return"]) is int and record["spawn_return"] == 0, "spawn_return")
    for name in SPAWN:
        require(integer(spawn[name], 0, 2**31 - 1 if name.endswith("error") or name == "pid" else 2**32 - 1), "spawn_numeric_" + name)
    for name in ("primary_error", "spawn_error", "attr_destroy_error", "actions_destroy_error", "fd_close_error", "fd_close_failures"):
        require(spawn[name] == 0, "spawn_" + name)
    require(spawn["version"] == 1 and spawn["stage"] == 10 and spawn["child_created"] == 1 and spawn["duplicate_count"] == 4, "checked_spawn")
    require(integer(spawn["pid"], 1, 2**31 - 1), "positive_owned_pid")
    flags = spawn["requested_flags"]
    require(integer(flags, 0, 65535) and flags == spawn["observed_flags"] == 0x400E, "checked_cloexec_default_flags")
    expected_out = f"CHONKFD/1 OUT {nonce}\n".encode()
    expected_err = f"CHONKFD/1 ERR {nonce}\n".encode()
    require(stdout == expected_out and len(stdout) <= CHANNEL_CAP, "actual_stdout")
    require(stderr == expected_err and len(stderr) <= CHANNEL_CAP, "actual_stderr")
    require(integer(record["input_sent"]) and record["input_sent"] == len(f"CHONKFD/1 IN {nonce}\n"), "actual_stdin")
    for name, data in (("stdout_bytes", stdout), ("stderr_bytes", stderr), ("status_bytes", status)):
        require(integer(record[name], 0, CHANNEL_CAP) and record[name] == len(data), name)
    require(len(status) <= CHANNEL_CAP, "status_cap")
    match = re.fullmatch(rb"CHONKFD/1 RESULT ([0-9a-f]{48}) ([1-9][0-9]{0,9}) ([1-9][0-9]{0,9}) (0|[1-9][0-9]{0,9}) (0|[1-9][0-9]{0,9}) (0|[1-9][0-9]{0,9}) (0|[1-9][0-9]{0,9})\n", status)
    require(match is not None, "bounded_status_frame")
    if match:
        identity, *numbers = match.groups()
        pid, bound, low, extra, bits, first_errno = map(int, numbers)
        require(identity.decode() == nonce, "status_identity")
        require(1 <= pid <= 2**31 - 1 and pid == spawn["pid"], "status_owned_pid")
        require(4 <= bound <= SCAN_CAP and bound == record["scan_bound"], "status_scan_bound")
        require(low == 15 and extra == 0 and bits == 0 and first_errno == 0, "actual_child_whitelist")
        require(child["valid"] is True, "child_report_valid")
        for key, value in (("pid", pid), ("scan_bound", bound), ("low_mask", low), ("extra_fds", extra), ("failure_bits", bits), ("first_errno", first_errno)):
            require(integer(child[key]) and child[key] == value, "child_report_" + key)
    require(wait["reaped"] is True and wait["lost"] is False, "sole_owner_reaped")
    require(type(wait["raw_status"]) is int and wait["raw_status"] == 0, "actual_child_exit_zero")
    require(type(wait["error"]) is int and wait["error"] == 0 and wait["term_sent"] is False and wait["kill_sent"] is False, "normal_unforced_child_exit")
    require(type(record["errors"]) is list and not record["errors"], "no_runtime_errors")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--parent-exit-code", type=int, required=True)
    args = parser.parse_args()
    def bounded_read(path, cap):
        with path.open("rb") as stream:
            data = stream.read(cap + 1)
        if len(data) > cap:
            raise ValueError("record exceeds bound")
        return data
    try:
        record = strict_json(bounded_read(args.records / "result.json", RESULT_CAP))
        transcripts = [bounded_read(args.records / name, CHANNEL_CAP) for name in ("stdout.bin", "stderr.bin", "status.bin")]
        failures = evaluate(record, *transcripts, args.parent_exit_code)
    except (OSError, ValueError, TypeError, KeyError, RecursionError) as error:
        failures = [f"unreadable_or_malformed_records:{type(error).__name__}"]
    print(json.dumps({"record_criteria_met": not failures, "failures": failures, "scope": "closed-record validation; root must bind actual execution, parent status and artifacts separately"}, sort_keys=True))
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
