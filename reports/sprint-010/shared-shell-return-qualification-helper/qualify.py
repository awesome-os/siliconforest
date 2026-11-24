"""Host-only C1-C9 checker for one closed shared-shell capture.

Pins the frozen criteria and the frozen export parser before it reads a
capture. Missing evidence stays not_evaluated. A final text grid is not a
window result. This module does not claim a runtime pass by itself.
"""
import hashlib
import importlib.util
import json
import re
from pathlib import Path


class Incomplete(Exception):
    """The named evidence is absent. Callers keep that check not_evaluated."""

ROOT = Path(__file__).resolve().parents[3]
CRITERIA = ROOT / "reports/sprint-010/shared-shell-return-qualification-criteria-001.json"
PACKET_REVIEW = ROOT / "reports/sprint-010/shared-shell-return-candidate001-packet-review.json"
PARSER = (
    ROOT
    / "integrations/puredarwin/m1-desktop-session/shared-shell/export-blocks/decode_exports.py"
)
CRITERIA_SHA256 = "9768f31606b6681505ae39c369465472669745fd9a6ac6f300cc70973a10f4e0"
PARSER_SHA256 = "4e845eefd9359d6f729d644a943e925ad518c3cf19b2e2176ede2c4cb99fe0d3"
PACKET_REVIEW_SHA256 = "e67b200b081a0e12882334c52a39f4614bc8940e7cadc645b1b6350fcdbaee97"
FLAGS = 16398
GATES = (
    "Admission",
    "StatusNonblocking",
    "SpawnReturned",
    "PidOwned",
    "AppReady",
    "AppCleanup",
    "StatusEof",
    "AppReaped",
    "TermAttempt",
    "KillAttempt",
)
REQUIRED_OBSERVED = GATES[:8]
STEPS = (
    "global:_PE_kputc",
    "global:_darwin_lab_m1_progress",
    "global:_debug_buf_base",
    "global:_debug_buf_ptr",
    "global:_debug_buf_size",
    "register:SCTLR_EL12",
    "register:TCR_EL12",
    "register:TTBR0_EL12",
    "register:TTBR1_EL12",
    "register:ELR_EL12",
    "register:ESR_EL12",
    "register:FAR_EL12",
    "framebuffer-carveouts",
    "framebuffer-pixels",
)
EXEC_KEYS = (
    "pre_exec_fd_check_observed",
    "pre_exec_fd_scan_bound_exclusive",
    "exec_status_pipe_eof_observed",
    "shell_exec_checked",
    "inventory_scope",
)
SESSION_KEYS = (
    "configure_serial",
    "ready",
    "keymap",
    "pty_exec_checked",
    "exec_boundary",
    "ssd",
    "frames",
    "commits",
    "pty_rx_bytes",
    "pty_tx_bytes",
    "input_accepted_bytes",
    "input_unsent_bytes",
    "unsupported_terminal_sequences",
    "stop_cause",
    "term_signal",
    "window_closed",
    "pty_eof",
)
SPAWN = re.compile(
    r"SpawnResultV1 \{ version: (\d+), stage: (\d+), pid: (-?\d+), "
    r"primary_error: (-?\d+), spawn_error: (-?\d+), attr_destroy_error: (-?\d+), "
    r"actions_destroy_error: (-?\d+), fd_close_error: (-?\d+), fd_close_failures: (\d+), "
    r"requested_flags: (\d+), observed_flags: (\d+), child_created: (\d+), "
    r"duplicate_count: (\d+), reserved: (\d+) \}"
)
CLEANUP = re.compile(
    r"Cleanup \{ complete: (true|false), raw_wait_status: (-?\d+), "
    r"term_sent: (true|false), kill_sent: (true|false) \}"
)
ATTEMPT = re.compile(
    r"attempt=(\d+) source=(explicit_startup|input|direct_library) "
    r"request=(None|Some\(\d+\)) instance=(None|Some\(\"[0-9a-f]{48}\"\)) "
    r"pid=(None|Some\(\d+\)) created_ms=(\d+) failure=(None|Some\(\".*\"\)) "
    r"failure_ms=(None|Some\(\d+\)) status_bytes=(\d+) wait=(None|Some\(-?\d+\)) "
    r"wait_error=(None|Some\(-?\d+\)) cleanup=(None|Some\(Cleanup \{ .*? \}\)) "
    r"native_return=(None|Some\(-?\d+\)) native=(None|Some\(SpawnResultV1 \{ .*? \}\))\Z"
)
HEX48 = re.compile(r"[0-9a-f]{48}\Z")


def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bind():
    found = {
        "criteria": _sha(CRITERIA),
        "parser": _sha(PARSER),
        "packet_review": _sha(PACKET_REVIEW),
    }
    expected = {
        "criteria": CRITERIA_SHA256,
        "parser": PARSER_SHA256,
        "packet_review": PACKET_REVIEW_SHA256,
    }
    if found != expected:
        raise ValueError("Frozen criteria, parser, or packet-review hash changed")
    return found


def _load_parser():
    bind()
    spec = importlib.util.spec_from_file_location("shared_shell_decode_exports_pinned", PARSER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.decode_exports


def _lines(raw):
    if not isinstance(raw, bytes) or b"\r" in raw:
        raise ValueError("Record text must be LF-only bytes")
    text = raw.decode("ascii")
    if text.endswith("\n"):
        text = text[:-1]
    if not text or text.endswith("\n"):
        raise ValueError("Empty record")
    return text.split("\n")


def _opt_int(token):
    if token == "None":
        return None
    match = re.fullmatch(r"Some\((-?\d+)\)", token)
    if not match or str(int(match[1])) != match[1]:
        raise ValueError("Noncanonical optional integer")
    return int(match[1])


def _spawn(token):
    match = SPAWN.fullmatch(token)
    if not match:
        raise ValueError("Malformed SpawnResultV1")
    names = (
        "version", "stage", "pid", "primary_error", "spawn_error", "attr_destroy_error",
        "actions_destroy_error", "fd_close_error", "fd_close_failures", "requested_flags",
        "observed_flags", "child_created", "duplicate_count", "reserved",
    )
    return {name: int(value) for name, value in zip(names, match.groups())}


def _cleanup(token):
    match = CLEANUP.fullmatch(token)
    if not match:
        raise ValueError("Malformed Cleanup")
    return {
        "complete": match[1] == "true",
        "raw_wait_status": int(match[2]),
        "term_sent": match[3] == "true",
        "kill_sent": match[4] == "true",
    }


def _some_cleanup(token):
    if token == "None":
        return None
    if token.startswith("Some(") and token.endswith(")"):
        return _cleanup(token[5:-1])
    raise ValueError("Malformed cleanup option")


def _some_spawn(token):
    if token == "None":
        return None
    if token.startswith("Some(") and token.endswith(")"):
        return _spawn(token[5:-1])
    raise ValueError("Malformed native option")


def parse_session_result(raw):
    lines = _lines(raw)
    if not lines[0].startswith("CHONKDESKTOP/1 initialized=1 input=both shell=shared"):
        raise ValueError("Session result does not open with the shared-shell line")
    if lines[0] != "CHONKDESKTOP/1 initialized=1 input=both shell=shared native_runtime_qualification=not_inferred":
        raise ValueError("Unexpected initialized line")
    seal = re.fullmatch(
        r"CHONKDESKTOP/1 elapsed_ms=(\d+) cleanup=(Complete|Pending|Incomplete) "
        r"deadline=(true|false) frame_error=(None|Some\(\".*\"\)) manager_failed=(true|false)",
        lines[1] if len(lines) > 1 else "",
    )
    if not seal:
        raise ValueError("Malformed session seal")
    header = re.fullmatch(
        r"CHONKLAUNCH/1 snapshot attempts=(\d+) journal_error=(None|Some\(\".*\"\)) "
        r"app_internal_gates=unknown_until_final_app_record",
        lines[2] if len(lines) > 2 else "",
    )
    if not header:
        raise ValueError("Malformed snapshot header")
    attempts = []
    index = 3
    while index < len(lines):
        row = ATTEMPT.fullmatch(lines[index])
        if not row:
            raise ValueError("Malformed snapshot attempt")
        gates = {}
        for gate in GATES:
            index += 1
            if index >= len(lines):
                raise ValueError("Truncated snapshot gates")
            found = re.fullmatch(
                rf"attempt={row[1]} gate={gate} observed_ms=(None|Some\(\d+\))",
                lines[index],
            )
            if not found:
                raise ValueError("Snapshot gate missing or reordered")
            gates[gate] = _opt_int(found[1])
        instance = None
        if row[4] != "None":
            instance = row[4][6:-2]
        native = _some_spawn(row[14])
        attempts.append({
            "sequence": int(row[1]),
            "source": row[2],
            "request": _opt_int(row[3]),
            "instance": instance,
            "pid": _opt_int(row[5]),
            "created_ms": int(row[6]),
            "failure": None if row[7] == "None" else row[7],
            "failure_ms": _opt_int(row[8]),
            "status_bytes": int(row[9]),
            "wait": _opt_int(row[10]),
            "wait_error": _opt_int(row[11]),
            "cleanup": _some_cleanup(row[12]),
            "native_return": _opt_int(row[13]),
            "native": native,
            "gates": gates,
        })
        index += 1
    if int(header[1]) != len(attempts):
        raise ValueError("Snapshot attempt count disagrees with the rows")
    return {
        "elapsed_ms": int(seal[1]),
        "cleanup": seal[2],
        "deadline": seal[3] == "true",
        "frame_error": seal[4],
        "manager_failed": seal[5] == "true",
        "journal_error": header[2],
        "attempts": attempts,
    }


def parse_manager_journal(raw):
    lines = _lines(raw)
    if lines[0] != "CHONKLAUNCH/1 scope=manager app_internal_gates=unknown_until_final_app_record":
        raise ValueError("Journal does not open with the manager scope")
    attempts = {}
    for line in lines[1:]:
        requested = re.fullmatch(
            r"CHONKLAUNCH/1 attempt=(\d+) ms=(\d+) request=(None|Some\(\d+\)) "
            r"instance=(None|Some\(\"[0-9a-f]{48}\"\)) source=(explicit_startup|input|direct_library) "
            r"event=requested",
            line,
        )
        gate = re.fullmatch(r"CHONKLAUNCH/1 attempt=(\d+) ms=(\d+) event=([A-Za-z]+)", line)
        failure = re.fullmatch(r"CHONKLAUNCH/1 attempt=(\d+) ms=(\d+) failure=Some\(\".*\"\)", line)
        native = re.fullmatch(
            r"CHONKLAUNCH/1 attempt=(\d+) native_return=(-?\d+) native=(SpawnResultV1 \{ .* \})",
            line,
        )
        status = re.fullmatch(
            r"CHONKLAUNCH/1 attempt=(\d+) status_bytes=(\d+) raw_app_wait=(None|Some\(-?\d+\)) "
            r"wait_error=(None|Some\(-?\d+\)) cleanup=(None|Some\(Cleanup \{ .*? \}\))",
            line,
        )
        matched = requested or gate or failure or native or status
        if not matched:
            raise ValueError("Foreign or malformed journal line")
        key = int(matched[1])
        slot = attempts.setdefault(key, {"gates": [], "status": [], "failure": False})
        if requested:
            if "requested" in slot:
                raise ValueError("Duplicate journal request")
            instance = None if requested[4] == "None" else requested[4][6:-2]
            slot["requested"] = {
                "request": _opt_int(requested[3]),
                "instance": instance,
                "source": requested[5],
            }
        elif gate:
            if gate[3] not in GATES:
                raise ValueError("Unknown journal gate")
            slot["gates"].append(gate[3])
        elif failure:
            slot["failure"] = True
        elif native:
            if "native" in slot:
                raise ValueError("Duplicate journal native result")
            slot["native_return"] = int(native[2]) if str(int(native[2])) == native[2] else None
            if slot["native_return"] is None:
                raise ValueError("Noncanonical journal native_return")
            slot["native"] = _spawn(native[3])
        else:
            slot["status"].append({
                "status_bytes": int(status[2]),
                "raw_app_wait": _opt_int(status[3]),
                "wait_error": _opt_int(status[4]),
                "cleanup": _some_cleanup(status[5]),
            })
    return attempts


def _object(raw, keys):
    if not isinstance(raw, bytes) or not raw.endswith(b"\n") or b"\r" in raw:
        raise ValueError("JSON record must be one LF-terminated document")
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ValueError("JSON record keys do not match the schema")
    return value


def parse_child(raw):
    value = _object(raw, ("parent_pid", "child_pid", "setup_checked", "job_control"))
    if value["setup_checked"] is not False or value["job_control"] != "disabled; foreground group must remain owned shell":
        raise ValueError("child.json schema mismatch")
    if type(value["parent_pid"]) is not int or type(value["child_pid"]) is not int:
        raise ValueError("child.json pid type")
    return value


def parse_exec_boundary(value):
    if not isinstance(value, dict) or set(value) != set(EXEC_KEYS):
        raise ValueError("exec_boundary keys")
    bound = value["pre_exec_fd_scan_bound_exclusive"]
    if (
        value["pre_exec_fd_check_observed"] is not True
        or value["exec_status_pipe_eof_observed"] is not True
        or value["shell_exec_checked"] is not True
        or value["inventory_scope"] != "pre_exec_only_not_after_exec_inventory"
        or type(bound) is not int
        or not 3 <= bound <= 1048576
    ):
        raise ValueError("exec_boundary facts")
    return value


def parse_child_ready(raw):
    value = _object(
        raw,
        (
            "parent_pid", "child_pid", "sid", "pgid", "session_and_ctty_checked_in_child",
            "exec_status_pipe_closed", "exec_boundary", "shell",
        ),
    )
    if (
        value["session_and_ctty_checked_in_child"] is not True
        or value["exec_status_pipe_closed"] is not True
        or value["shell"] != "/bin/sh"
        or value["sid"] != value["child_pid"]
        or value["pgid"] != value["child_pid"]
    ):
        raise ValueError("child-ready identity")
    parse_exec_boundary(value["exec_boundary"])
    return value


def parse_result(raw):
    value = _object(
        raw,
        (
            "schema", "instance", "app_pid", "shell", "term", "child_pid",
            "child_exec_boundary", "session", "cleanup", "errors",
        ),
    )
    session = value["session"]
    cleanup = value["cleanup"]
    if (
        value["schema"] != 1
        or not isinstance(value["instance"], str)
        or HEX48.fullmatch(value["instance"]) is None
        or value["shell"] != "/bin/sh"
        or value["term"] != "dumb"
        or not isinstance(session, dict)
        or set(session) != set(SESSION_KEYS)
        or not isinstance(cleanup, dict)
        or set(cleanup) != {"complete", "status", "term_sent", "kill_sent", "error"}
        or not isinstance(value["errors"], list)
    ):
        raise ValueError("result.json schema")
    parse_exec_boundary(value["child_exec_boundary"])
    parse_exec_boundary(session["exec_boundary"])
    return value


class Grid:
    def __init__(self, cols, rows):
        if not 20 <= cols <= 160 or not 6 <= rows <= 60:
            raise ValueError("Grid dimensions")
        self.cols = cols
        self.rows = rows
        self.cells = [" "] * (cols * rows)
        self.x = 0
        self.y = 0
        self.unsupported = 0
        self.escape = ("none",)
        self.utf8 = bytearray()
        self.wrap = False

    def text(self):
        rows = []
        for y in range(self.rows):
            rows.append("".join(self.cells[y * self.cols:(y + 1) * self.cols]))
        return "\n".join(rows)

    def _newline(self):
        self.wrap = False
        if self.y + 1 == self.rows:
            self.cells = self.cells[self.cols:] + [" "] * self.cols
        else:
            self.y += 1

    def _put(self, char):
        if self.wrap:
            self.x = 0
            self._newline()
        if not char.isascii():
            self.unsupported += 1
            char = "\ufffd"
        self.cells[self.y * self.cols + self.x] = char
        if self.x + 1 == self.cols:
            self.wrap = True
        else:
            self.x += 1

    def feed(self, data):
        for byte in data:
            kind = self.escape
            self.escape = ("none",)
            if kind[0] == "esc":
                if byte == 0x5b:
                    self.escape = ("csi", bytearray())
                elif byte == 0x5d:
                    self.escape = ("osc", 0, False)
                else:
                    self.unsupported += 1
                continue
            if kind[0] == "csi":
                params = kind[1]
                if 0x40 <= byte <= 0x7e:
                    self._csi(params, byte)
                elif len(params) < 64 and 0x20 <= byte <= 0x3f:
                    params.append(byte)
                    self.escape = ("csi", params)
                else:
                    self.unsupported += 1
                    self.escape = ("discard",)
                continue
            if kind[0] == "discard":
                if not 0x40 <= byte <= 0x7e:
                    self.escape = ("discard",)
                continue
            if kind[0] == "osc":
                count, esc = kind[1], kind[2]
                if byte == 7 or (esc and byte == 0x5c):
                    self.unsupported += 1
                else:
                    self.escape = ("osc", min(count + 1, 256), byte == 27)
                continue
            if self.utf8 and not 0x80 <= byte <= 0xbf:
                self.utf8.clear()
                self._put("\ufffd")
            if byte == 27:
                self.escape = ("esc",)
            elif byte == 13:
                self.x = 0
                self.wrap = False
            elif byte == 10:
                self._newline()
            elif byte in (8, 127):
                self.x = max(0, self.x - 1)
                self.wrap = False
            elif byte == 9:
                self.x = min(((self.x // 8) + 1) * 8, self.cols - 1)
                self.wrap = False
            elif byte == 12:
                self.cells = [" "] * (self.cols * self.rows)
                self.x = self.y = 0
                self.wrap = False
            elif byte in (0, 7):
                pass
            elif byte < 32:
                self.unsupported += 1
            elif byte < 127:
                self._put(chr(byte))
            else:
                self.utf8.append(byte)
                try:
                    char = self.utf8.decode()
                except UnicodeDecodeError as error:
                    incomplete = error.reason == "unexpected end of data" and len(self.utf8) < 4
                    if not incomplete:
                        self.utf8.clear()
                        self._put("\ufffd")
                else:
                    self.utf8.clear()
                    self._put(char)

    def _csi(self, params, end):
        if params.startswith(b"?"):
            self.unsupported += 1
            return
        try:
            text = params.decode()
        except UnicodeDecodeError:
            self.unsupported += 1
            return
        values = []
        for item in text.split(";"):
            if item == "":
                values.append(0)
            elif item.isdigit():
                values.append(int(item))
            else:
                self.unsupported += 1
                return
        if len(values) > 8:
            self.unsupported += 1
            return
        number = values[0] if values else 0
        step = min(max(number, 1), 1000)
        self.wrap = False
        if end == 0x41:
            self.y = max(0, self.y - step)
        elif end == 0x42:
            self.y = min(self.y + step, self.rows - 1)
        elif end == 0x43:
            self.x = min(self.x + step, self.cols - 1)
        elif end == 0x44:
            self.x = max(0, self.x - step)
        elif end == 0x47:
            self.x = min(max(step - 1, 0), self.cols - 1)
        elif end in (0x48, 0x66):
            self.y = min(max(number, 1) - 1, self.rows - 1)
            column = values[1] if len(values) > 1 else 1
            self.x = min(max(column, 1) - 1, self.cols - 1)
        elif end == 0x4b:
            row = self.y * self.cols
            if number == 0:
                self.cells[row + self.x:row + self.cols] = [" "] * (self.cols - self.x)
            elif number == 1:
                self.cells[row:row + self.x + 1] = [" "] * (self.x + 1)
            elif number == 2:
                self.cells[row:row + self.cols] = [" "] * self.cols
            else:
                self.unsupported += 1
        elif end == 0x4a:
            pos = self.y * self.cols + self.x
            if number == 0:
                self.cells[pos:] = [" "] * (len(self.cells) - pos)
            elif number == 1:
                self.cells[:pos + 1] = [" "] * (pos + 1)
            elif number == 2:
                self.cells = [" "] * len(self.cells)
            else:
                self.unsupported += 1
        elif end == 0x6d:
            if any(item != 0 for item in values):
                self.unsupported += 1
        else:
            self.unsupported += 1


def replay_grid(grid_bytes, pty_bytes):
    if b"\r" in grid_bytes or grid_bytes.endswith(b"\n"):
        raise ValueError("grid-final geometry is not the writer shape")
    text = grid_bytes.decode("utf-8")
    rows = text.split("\n")
    width = len(rows[0])
    if not rows or any(len(row) != width for row in rows):
        raise ValueError("grid-final is not a rectangle")
    grid = Grid(width, len(rows))
    grid.feed(pty_bytes)
    if grid.text() != text:
        raise ValueError("pty-rx does not replay at the measured grid geometry")
    return {"cols": width, "rows": len(rows)}


def _check(status, detail):
    return {"status": status, "detail": detail}


def _roll(items):
    states = [item["status"] for item in items.values()]
    if any(state == "FAIL" for state in states):
        return "FAIL"
    if any(state == "NOT_EVALUATED" for state in states):
        return "NOT_EVALUATED"
    return "PASS"


def _instances(nonce):
    return ("0001" + nonce[:44], "0002" + nonce[:44])


def _agree(snapshot, journal):
    if set(journal) != {0, 1}:
        raise ValueError("Journal attempt set is not the two explicit requests")
    for attempt in snapshot:
        slot = journal[attempt["sequence"]]
        if slot.get("failure"):
            raise ValueError("Journal failure contradicts a clean snapshot")
        requested = slot.get("requested")
        if requested != {
            "request": attempt["request"],
            "instance": attempt["instance"],
            "source": attempt["source"],
        }:
            raise ValueError("Journal request disagrees with the snapshot")
        if slot.get("native_return") != attempt["native_return"] or slot.get("native") != attempt["native"]:
            raise ValueError("Journal native result disagrees with the snapshot")
        if not slot["status"]:
            raise ValueError("Journal has no raw_app_wait observation")
        final = slot["status"][-1]
        if (
            final["raw_app_wait"] != attempt["wait"]
            or final["wait_error"] != attempt["wait_error"]
            or final["cleanup"] != attempt["cleanup"]
            or final["status_bytes"] != attempt["status_bytes"]
        ):
            raise ValueError("Journal raw_app_wait disagrees with snapshot wait")
        for earlier in slot["status"][:-1]:
            if earlier["raw_app_wait"] not in (None, attempt["wait"]):
                raise ValueError("Earlier journal wait contradicts the application exit")
        for gate in REQUIRED_OBSERVED:
            if gate not in slot["gates"]:
                raise ValueError("Journal omits an observed gate")
        if "KillAttempt" in slot["gates"]:
            raise ValueError("Journal records an application KillAttempt")


def _app_checks(attempt, terminal, logs):
    ready = parse_child_ready(terminal["child-ready.json"]["data"])
    child = parse_child(terminal["child.json"]["data"])
    result = parse_result(terminal["result.json"]["data"])
    if child["parent_pid"] != attempt["pid"] or child["child_pid"] <= 0:
        raise ValueError("child.json does not bind the application pid")
    if ready["parent_pid"] != attempt["pid"] or ready["child_pid"] != child["child_pid"]:
        raise ValueError("child-ready pid binding")
    if result["instance"] != attempt["instance"] or result["app_pid"] != attempt["pid"]:
        raise ValueError("result instance or application pid")
    if result["child_pid"] != child["child_pid"]:
        raise ValueError("result shell pid")
    if result["child_exec_boundary"] != ready["exec_boundary"] or result["session"]["exec_boundary"] != ready["exec_boundary"]:
        raise ValueError("exec_boundary disagreement")
    session = result["session"]
    if not (
        session["ready"] is True
        and session["keymap"] is True
        and session["pty_exec_checked"] is True
        and session["ssd"] is True
        and type(session["frames"]) is int
        and session["frames"] > 0
        and type(session["commits"]) is int
        and session["commits"] > 0
        and session["stop_cause"] != "fault"
        and result["errors"] == []
    ):
        raise ValueError("result session is not a completed shell observation")
    rx = terminal["pty-rx.bin"]["data"]
    tx = terminal["pty-tx.bin"]["data"]
    if b"$ " not in rx:
        raise ValueError("pty-rx has no interactive shell prompt")
    if session["pty_rx_bytes"] != len(rx) or session["pty_tx_bytes"] != len(tx):
        raise ValueError("Transcript counts disagree with exported bytes")
    replay_grid(terminal["grid-final.txt"]["data"], rx)
    if set(logs) != {"application-stdout.log", "application-stderr.log"}:
        raise ValueError("Application logs were not both preserved")
    return {
        "application_pid": attempt["pid"],
        "shell_pid": child["child_pid"],
        "application_wait": attempt["wait"],
        "pty_wait": result["cleanup"]["status"],
        "pty_rx_bytes": len(rx),
        "pty_tx_bytes": len(tx),
    }


def _c5_application(attempt, terminal):
    result = parse_result(terminal["result.json"]["data"])
    snapshot = attempt["cleanup"]
    cleanup = result["cleanup"]
    if attempt["wait"] != 0:
        raise ValueError("Application wait is not 0")
    if (
        snapshot is None
        or snapshot["complete"] is not True
        or cleanup["complete"] is not True
        or cleanup["error"] is not None
        or type(cleanup["status"]) is not int
        or cleanup["status"] != snapshot["raw_wait_status"]
        or cleanup["term_sent"] is not snapshot["term_sent"]
        or cleanup["kill_sent"] is not snapshot["kill_sent"]
    ):
        raise ValueError("PTY cleanup does not match snapshot raw_wait_status")


def _expect_attempt(attempt, nonce, sequence):
    expected = _instances(nonce)[sequence]
    if (
        attempt["sequence"] != sequence
        or attempt["source"] != "explicit_startup"
        or attempt["request"] != sequence + 1
        or attempt["instance"] != expected
        or attempt["pid"] is None
        or attempt["pid"] <= 0
        or attempt["failure"] is not None
        or attempt["failure_ms"] is not None
        or attempt["wait_error"] is not None
        or attempt["status_bytes"] <= 0
    ):
        raise ValueError("Attempt is not one explicit owned application")
    for gate in REQUIRED_OBSERVED:
        if attempt["gates"][gate] is None:
            raise ValueError("Required gate was not observed")
    if attempt["gates"]["KillAttempt"] is not None:
        raise ValueError("Application KillAttempt was observed")
    native = attempt["native"]
    if (
        native is None
        or attempt["native_return"] != 0
        or native["pid"] != attempt["pid"]
        or native["version"] != 1
        or native["stage"] != 10
        or native["child_created"] != 1
        or native["requested_flags"] != FLAGS
        or native["observed_flags"] != FLAGS
        or native["duplicate_count"] != 4
        or native["reserved"] != 0
        or any(native[name] != 0 for name in (
            "primary_error", "spawn_error", "attr_destroy_error", "actions_destroy_error",
            "fd_close_error", "fd_close_failures",
        ))
    ):
        raise ValueError("Native spawn result is not the owned 0x400e result")


def _nonce_from_console(raw):
    markers = []
    for line in raw.replace(b"\r", b"").split(b"\n"):
        match = re.fullmatch(br"DL23_SESSION_EXPORT_BEGIN_([0-9a-f]{48}) phase=closed", line)
        if match:
            markers.append(match[1].decode())
    if len(markers) != 1:
        raise ValueError("Closed session export begin marker")
    return markers[0]


def _console_lines(raw):
    return raw.replace(b"\r", b"").split(b"\n")


def _final_status(raw, nonce):
    prefix = f"DL23_DESKTOP_FINAL_STATUS_{nonce} ".encode()
    found = [line for line in _console_lines(raw) if line.startswith(prefix)]
    if len(found) != 1:
        raise Incomplete("Final status line is absent")
    if found[0] != f"DL23_DESKTOP_FINAL_STATUS_{nonce} desktop=0 export_failed=0".encode():
        raise ValueError("Frontend exit or export footer is not zero")


def _footer_problem(raw, nonce):
    try:
        _final_status(raw, nonce)
    except Incomplete as error:
        return str(error)
    except Exception as error:
        return str(error)
    return None


def _text_return(raw, nonce):
    lines = _console_lines(raw)
    prefix = f"DL23_DESKTOP_CLOSED_EXPORT_{nonce} ".encode()
    closed = [line for line in lines if line.startswith(prefix)]
    if not closed:
        raise Incomplete("Closed export line is absent")
    success = f"DL23_DESKTOP_CLOSED_EXPORT_{nonce} failed=0 physical_input_proven=false".encode()
    if closed != [success]:
        raise ValueError("Closed export did not succeed")
    cursor = lines.index(success) + 1
    needed = (
        b"CHONKSTEP SHARED SHELL MANAGED SESSION RETURNED",
        f"DL23_DESKTOP_RETURN_{nonce}".encode(),
        b"Darwin",
        b"arm64",
        b"25.5.0",
        f"DL23_DESKTOP_END_{nonce} physical_input_proven=false".encode(),
    )
    for item in needed:
        found = next((index for index in range(cursor, len(lines)) if lines[index] == item), None)
        if found is None:
            raise ValueError("Text return marker missing")
        cursor = found + 1


def _marker_note(raw, nonce):
    prefixes = (
        f"DL23_SESSION_EXPORT_END_{nonce} ",
        f"DL23_APP_TERMINAL_EXPORT_END_{nonce} ",
        f"DL23_APP_EXPORT_END_{nonce} ",
        f"DL23_DESKTOP_CLOSED_EXPORT_{nonce} ",
        f"DL23_DESKTOP_FINAL_STATUS_{nonce} ",
        f"DL23_DESKTOP_RETURN_{nonce}",
        f"DL23_DESKTOP_END_{nonce} ",
    )
    found = []
    for line in _console_lines(raw):
        try:
            text = line.decode("ascii")
        except UnicodeDecodeError:
            continue
        if any(text.startswith(prefix) for prefix in prefixes) and len(text) <= 180:
            found.append(text)
        if len(found) == 12:
            break
    return "; ".join(found)


def _read(path):
    if not path.is_file():
        return None
    return path.read_bytes()


def _tx_ok(capture, nonce):
    result_path = capture / "guest/console-result.json"
    tx_path = capture / "guest/console-tx.bin"
    if not result_path.is_file() or not tx_path.is_file():
        return _check("NOT_EVALUATED", "console-tx.bin and console-result.json are required")
    result = json.loads(result_path.read_text())
    tx = tx_path.read_bytes()
    if result.get("nonce") != nonce or result.get("error") is not None:
        return _check("FAIL", "Console result nonce or error")
    if result.get("native_mode_host_write_phase") != "sealed" or result.get("desktop_command_sent") is not True:
        return _check("FAIL", "Host writer is not sealed after one launcher")
    challenge = (
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin; export PATH; "
        f"printf '\\nDL9_BEGIN_%s%s\\n' '{nonce[:24]}' '{nonce[24:]}'; "
        "{ uname -s; uname -m; uname -r; } 2>/dev/null; "
        f"printf 'DL9_END_%s%s\\n' '{nonce[:24]}' '{nonce[24:]}'\r"
    ).encode()
    launcher = f"/usr/bin/darwin-desktop-test '{nonce}'\r".encode()
    accepted = (challenge + launcher, b"/bin/sh\r" + challenge + launcher)
    if tx not in accepted or hashlib.sha256(tx).hexdigest() != result.get("console_tx_sha256"):
        return _check("FAIL", "Host TX is not the optional shell, fresh challenge, and one launcher")
    return _check("PASS", "Sealed host TX matches the bound writer")


def _boot_hash(value):
    if not isinstance(value, str) or not value or len(value) > 128:
        return None
    return hashlib.sha256(value.encode()).hexdigest()


def _retained(controller):
    parts = []
    worker = controller.get("guest_worker_returncode")
    host_status = controller.get("guest_host_status")
    if type(worker) is int:
        parts.append(f"guest_worker_returncode={worker}")
    if isinstance(host_status, str) and host_status.isascii() and len(host_status) <= 80:
        parts.append("guest_host_status=" + host_status)
    return "; ".join(parts)


def _recovery(capture, attestation):
    names = [
        "result.json",
        "baseline.json",
        "returned-linux.json",
        "guest/capture-core.json",
        "guest/exception-context.json",
    ]
    for number in range(1, 15):
        names.append(f"guest/diagnostic-{number:02d}-started.json")
        names.append(f"guest/diagnostic-{number:02d}-completed.json")
    if attestation is None:
        implied = capture / "attestations/linux-recovery.json"
        attestation = implied if implied.is_file() else None
    if attestation is None or not Path(attestation).is_file() or any(not (capture / name).is_file() for name in names):
        return _check("NOT_EVALUATED", "Controller, Linux baseline, core, diagnostics, or latch attestation is absent")
    try:
        controller = json.loads((capture / "result.json").read_text())
        before = json.loads((capture / "baseline.json").read_text())
        linux = json.loads((capture / "returned-linux.json").read_text())
        attest = json.loads(Path(attestation).read_text())
        core = json.loads((capture / "guest/capture-core.json").read_text())
        context = json.loads((capture / "guest/exception-context.json").read_text())
        labels = []
        for number in range(1, 15):
            started = json.loads((capture / f"guest/diagnostic-{number:02d}-started.json").read_text())
            completed = json.loads((capture / f"guest/diagnostic-{number:02d}-completed.json").read_text())
            if started.get("step") != completed.get("step"):
                raise ValueError("diagnostic pair")
            labels.append(started.get("step"))
        before_hash = _boot_hash(before.get("boot_id"))
        after_hash = _boot_hash(linux.get("boot_id"))
        expected = {
            "status": "LINUX_VERIFIED_LATCH_ABSENT_CONTROLLER_LOCK_FREE",
            "linux_verified": True,
            "reset_attempts": 1,
            "extra_reset": False,
            "incomplete_latch": False,
            "lock_acquired_and_released": True,
        }
        if (
            controller.get("linux_verified") is not True
            or controller.get("reset_attempts") != 1
            or controller.get("diagnostics_complete") is not True
            or core.get("diagnostics_complete") is not False
            or core.get("console_capture_finalized") is not True
            or core.get("console_close_error") is not None
            or context.get("diagnostics_complete") is not True
            or context.get("context_error") is not None
            or not isinstance(context.get("diagnostics"), dict)
            or context["diagnostics"].get("complete") is not True
            or labels != list(STEPS)
            or any(attest.get(key) != value for key, value in expected.items())
            or before_hash is None
            or after_hash is None
            or before_hash == after_hash
        ):
            raise ValueError("recovery rule")
    except Exception:
        retained = ""
        try:
            retained = _retained(json.loads((capture / "result.json").read_text()))
        except Exception:
            retained = ""
        detail = "Recovery evidence does not meet the frozen capture rule"
        if retained:
            detail += "; " + retained
        return _check("FAIL", detail)
    retained = _retained(controller)
    detail = (
        f"boot_id_sha256_before={before_hash} boot_id_sha256_after={after_hash}; "
        "latch absent, lock released, one reset; recovery does not qualify the desktop"
    )
    if retained:
        detail += "; " + retained
    return _check("PASS", detail)


def _requests(session):
    attempts = session["attempts"]
    if len(attempts) == 2 and all(
        item["source"] == "explicit_startup" and item["request"] == index + 1
        for index, item in enumerate(attempts)
    ):
        return _check("PASS", "Two explicit_startup requests are present")
    return _check("FAIL", "Snapshot is not two explicit_startup requests")


def _closed_file_count(decoded):
    terminal_names = {
        "pty-rx.bin", "pty-tx.bin", "child.json", "child-ready.json", "grid-final.txt", "result.json",
    }
    log_names = {"application-stdout.log", "application-stderr.log"}
    if set(decoded["session"]) != {"session-result.txt", "manager-status.log"} or len(decoded["applications"]) != 2:
        raise ValueError("Export did not contain eighteen files")
    for app in decoded["applications"]:
        if set(app["terminal"]) != terminal_names or set(app["logs"]) != log_names:
            raise ValueError("Export did not contain eighteen files")


def _c5_reasons(session, compared, agreed, footer_problem, application_problems):
    reasons = []
    if compared and not agreed:
        reasons.append("snapshot wait and journal raw_app_wait do not agree")
    if session["cleanup"] != "Complete" or session["frame_error"] != "None" or session["manager_failed"]:
        reasons.append("Coordinator seal is not a clean finite session")
    if len(session["attempts"]) != 2:
        reasons.append("Both application waits of 0 were not observed")
    else:
        for attempt in session["attempts"]:
            if attempt["gates"]["KillAttempt"] is not None:
                reasons.append("Application KillAttempt was observed")
    reasons.extend(application_problems)
    if footer_problem:
        reasons.append(footer_problem)
    return reasons


def _framebuffer(capture):
    path = capture / "guest/framebuffer.json"
    if not path.is_file():
        return _check("NOT_EVALUATED", "A final text grid or framebuffer file is not a glyph or two-window qualification")
    try:
        panel = json.loads(path.read_text()).get("physical_panel_observed")
    except Exception:
        panel = None
    if type(panel) is not bool:
        panel = None
    return _check(
        "NOT_EVALUATED",
        f"physical_panel_observed={panel}; glyphs and two windows are not qualified from the framebuffer file",
    )


def _grade_text(console, nonce):
    if nonce is None:
        return _check("NOT_EVALUATED", "No session nonce")
    try:
        _text_return(console, nonce)
    except Incomplete:
        return _check("NOT_EVALUATED", "Closed export line is absent")
    except Exception as error:
        return _check("FAIL", str(error))
    return _check("PASS", "Closed return lines and release token follow the closed export")


def _grade_refused_c5(console, nonce):
    if nonce is None:
        return _check("NOT_EVALUATED", "No session nonce")
    problem = _footer_problem(console, nonce)
    if problem and problem != "Final status line is absent":
        return _check("FAIL", problem)
    return _check("NOT_EVALUATED", "Export interiors were refused, so application wait was not graded")


def qualify(capture, attestation=None):
    pins = bind()
    capture = Path(capture)
    checks = {}
    console = _read(capture / "guest/console.bin")
    checks["C1_sources"] = _check("PASS", "Criteria, export parser, and packet review hashes match the pins")
    checks["C2"] = _check("NOT_EVALUATED", "Seat, CPU backend, and packaged fonts are not in the exported text records")
    decoded = None
    nonce = None
    if console is None:
        checks["C1_runtime_tx"] = _check("NOT_EVALUATED", "No console capture")
        checks["C2_requests"] = _check("NOT_EVALUATED", "No exported session-result")
        checks["C3"] = _check("NOT_EVALUATED", "No exported session-result")
        checks["C4"] = _check("NOT_EVALUATED", "No exported application records")
        checks["C5"] = _check("NOT_EVALUATED", "No snapshot or frontend exit")
        checks["C6"] = _check("NOT_EVALUATED", "No export span")
        checks["C7_text_return"] = _check("NOT_EVALUATED", "No console text")
    else:
        try:
            nonce = _nonce_from_console(console)
        except Exception as error:
            checks["C6"] = _check("FAIL", error.__class__.__name__ + ": " + str(error))
        else:
            try:
                decoded = _load_parser()(console, nonce, _instances(nonce), "closed")
                _closed_file_count(decoded)
                checks["C6"] = _check("PASS", "Eighteen closed files decoded in wrapper order")
            except Exception as error:
                decoded = None
                detail = error.__class__.__name__ + ": " + str(error)
                note = _marker_note(console, nonce)
                if note:
                    detail += "; " + note
                checks["C6"] = _check("FAIL", detail)
        checks["C1_runtime_tx"] = _tx_ok(capture, nonce) if nonce else _check("NOT_EVALUATED", "No session nonce")
        checks["C7_text_return"] = _grade_text(console, nonce)
        if decoded is None:
            checks["C2_requests"] = _check("NOT_EVALUATED", "Export did not yield a session block")
            checks["C3"] = _check("NOT_EVALUATED", "Export did not yield a session block")
            checks["C4"] = _check("NOT_EVALUATED", "Export did not yield application blocks")
            checks["C5"] = _grade_refused_c5(console, nonce)
        else:
            session = None
            try:
                session = parse_session_result(decoded["session"]["session-result.txt"]["data"])
                journal = parse_manager_journal(decoded["session"]["manager-status.log"]["data"])
            except Exception as error:
                checks["C2_requests"] = _check("NOT_EVALUATED", "Session result or journal did not parse")
                checks["C3"] = _check("FAIL", str(error))
                checks["C4"] = _check("NOT_EVALUATED", "Application records depend on a parsed snapshot")
                checks["C5"] = _grade_refused_c5(console, nonce)
            else:
                checks["C2_requests"] = _requests(session)
                compared = False
                agreed = False
                try:
                    if len(session["attempts"]) != 2 or session["journal_error"] != "None":
                        raise ValueError("Snapshot is not two explicit applications with a clean journal")
                    compared = True
                    _agree(session["attempts"], journal)
                    agreed = True
                    pids = []
                    for sequence, attempt in enumerate(session["attempts"]):
                        _expect_attempt(attempt, nonce, sequence)
                        pids.append(attempt["pid"])
                    if len(set(pids)) != 2:
                        raise ValueError("Application PIDs are not distinct")
                    if [item["instance"] for item in decoded["applications"]] != list(_instances(nonce)):
                        raise ValueError("Exported instance order disagrees with the session ids")
                    checks["C3"] = _check("PASS", "Two explicit applications, distinct pids, and native 0x400e")
                except Exception as error:
                    checks["C3"] = _check("FAIL", str(error))
                ordered = [item["instance"] for item in decoded["applications"]] == list(_instances(nonce))
                application_problems = []
                if len(session["attempts"]) == 2 and ordered:
                    observed = []
                    failures = []
                    for attempt, exported in zip(session["attempts"], decoded["applications"]):
                        try:
                            observed.append(_app_checks(attempt, exported["terminal"], exported["logs"]))
                        except Exception as error:
                            failures.append(str(error))
                        try:
                            _c5_application(attempt, exported["terminal"])
                        except Exception as error:
                            application_problems.append(str(error))
                    if failures:
                        checks["C4"] = _check("FAIL", "; ".join(failures))
                    else:
                        checks["C4"] = _check("PASS", "Each child-ready and pty-rx prompt binds its own shell")
                        checks["applications_observed"] = _check("PASS", json.dumps(observed, sort_keys=True))
                else:
                    checks["C4"] = _check("NOT_EVALUATED", "Application records depend on the two snapshot attempts")
                reasons = _c5_reasons(session, compared, agreed, _footer_problem(console, nonce), application_problems)
                if reasons:
                    checks["C5"] = _check("FAIL", "; ".join(reasons))
                else:
                    checks["C5"] = _check("PASS", "Application exit 0, PTY cleanup matched, frontend exit 0")
    checks["C7_framebuffer"] = _framebuffer(capture)
    checks["C7"] = _check(
        "FAIL" if checks["C7_text_return"]["status"] == "FAIL" else "NOT_EVALUATED",
        "Text return is separate from framebuffer glyphs",
    )
    checks["C8"] = _recovery(capture, attestation)
    process = {key: checks[key] for key in ("C2_requests", "C3", "C4", "C5")}
    evidence = {key: checks[key] for key in ("C1_sources", "C1_runtime_tx", "C6")}
    preserved = [
        key for key in sorted(checks)
        if checks[key]["status"] in ("FAIL", "NOT_EVALUATED") and key != "C9"
    ]
    checks["C9"] = _check("PASS", "Unresolved ids remain unresolved: " + ",".join(preserved))
    return {
        "status": "CAPTURE_REVIEWED",
        "qualification": "NOT_QUALIFIED",
        "qualification_reason": "Framebuffer glyphs, two windows, and any failed or missing prerequisite stay unresolved",
        "runtime_qualified": False,
        "gui_window_qualified": False,
        "pins": pins,
        "groups": {
            "process_pty": {"status": _roll(process), "checks": list(process)},
            "evidence": {"status": _roll(evidence), "checks": list(evidence)},
            "recovery": {"status": checks["C8"]["status"]},
            "text_console": {"status": checks["C7_text_return"]["status"]},
            "gui_window": {"status": "NOT_EVALUATED"},
        },
        "checks": checks,
        "not_claimed": [
            "two simultaneously visible windows",
            "physical panel or raw HID",
            "after-exec FD inventory or Mach ports",
        ],
    }


def prepare():
    pins = bind()
    return {
        "status": "PREPARED_AWAITING_CAPTURE",
        "runtime_qualified": False,
        "gui_window_qualified": False,
        "pins": pins,
        "packet_review_verdict": "binding approval only; runtime_qualified remains false until a copied capture is checked",
    }


if __name__ == "__main__":
    import sys
    args = sys.argv[1:]
    if not args:
        print(json.dumps(prepare(), indent=2, sort_keys=True))
    elif args[0] == "--capture" and len(args) in (2, 4) and (len(args) == 2 or args[2] == "--attestation"):
        attestation = args[3] if len(args) == 4 else None
        print(json.dumps(qualify(args[1], attestation), indent=2, sort_keys=True))
    else:
        raise SystemExit("usage: qualify.py [--capture DIRECTORY [--attestation FILE]]")
