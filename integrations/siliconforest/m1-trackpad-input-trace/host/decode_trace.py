"""Original host decoder for one bounded passive dev2 input trace.

CRC-16/ARC is the standard reflected polynomial 0xA001, initial value 0,
xorout 0. The check value of the ASCII digits 123456789 is 0xBB3D. This
module does not copy another implementation.

A returned result is capture-valid. An exact-length CRC-valid 02 sample
whose X or Y byte is outside -127..127 is retained as
descriptor_value_invalid with pointer_motion false. That range does not
drop the sample, does not make the capture NO_INPUT_CAPTURED, and does not
reject the rest of the capture. Reason 2 is only the reserved extra 02
whose own X and Y are inside that range and not both zero.
"""
from __future__ import annotations

import hashlib
import re

DESCRIPTOR_SHA256 = "4ae5bc2626bff5458ba2325e29614f1fec5e8c5a4d32c396f64d59508ec77609"
INIT_LINE = (
    "DL24_TP_TRACE version=1 passive=1 cap02=16 cap3f=2 cap44=1 motion_reserved=1"
)
SOLE_PROBE = "SFM1SPIHID: sole probe attempt claimed"
WIRE_LENGTH = {0x02: 8, 0x3F: 17, 0x44: 1752}
CAP = {0x02: 16, 0x3F: 2, 0x44: 1}
MAX_RECORDS = 20
CHUNK = 64
CONSOLE_LIMIT = 32 << 20
SEEN_LIMIT = 0xFFFFFFFF

BEGIN = re.compile(
    r"DL24_TP_BEGIN seq=([0-9]+) id=([0-9a-f]{2}) bytes=([0-9]+) reason=([0-9]+) "
    r"seen02=([0-9]+) seen3f=([0-9]+) seen44=([0-9]+)\Z"
)
HEX = re.compile(r"DL24_TP_HEX seq=([0-9]+) index=([0-9]+) data=([0-9a-f]+)\Z")
END = re.compile(r"DL24_TP_END seq=([0-9]+) bytes=([0-9]+) crc16=([0-9a-f]{4})\Z")
SOURCE = re.compile(r"[A-Za-z0-9_./-]{1,160}\Z")
CYCLE = re.compile(r"cycle-[a-z0-9_]{8}\Z")

INFERENCES = {
    "mode_write": "not_inferred",
    "feature_02": "not_inferred",
    "feature_d9": "not_inferred",
    "device3": "not_inferred",
    "contacts": "not_inferred",
    "scroll": "not_inferred",
    "haptics": "not_inferred",
}


class TraceError(ValueError):
    """The console's DL24 trace is not capture-valid."""


def crc16_arc(data: bytes) -> int:
    """CRC-16/ARC over data, including a HID report id when data starts with one."""
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("crc input")
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def _canon(token: str, lo: int, hi: int, label: str) -> int:
    if re.fullmatch(r"0|[1-9][0-9]*", token) is None:
        raise TraceError(f"corrupt trace: {label}")
    value = int(token)
    if value < lo or value > hi or str(value) != token:
        raise TraceError(f"corrupt trace: {label}")
    return value


def _lines(console: bytes) -> list[bytes]:
    if not isinstance(console, (bytes, bytearray)) or len(console) > CONSOLE_LIMIT:
        raise TraceError("corrupt trace: console")
    body = bytes(console).replace(b"\r", b"")
    rows = body.split(b"\n")
    if rows and rows[-1] == b"":
        rows.pop()
    return rows


def _binding(source: str, cycle: str, descriptor_sha256: str) -> None:
    if not isinstance(source, str) or SOURCE.fullmatch(source) is None or ".." in source.split("/"):
        raise TraceError("binding: source")
    if not isinstance(cycle, str) or CYCLE.fullmatch(cycle) is None:
        raise TraceError("binding: cycle")
    if descriptor_sha256 != DESCRIPTOR_SHA256:
        raise TraceError("binding: foreign descriptor")


def _classify(report_id: int, raw: bytes) -> dict:
    if report_id != 0x02:
        return {
            "classification": "vendor_unknown",
            "value_status": "not_applicable",
            "pointer_motion": False,
        }
    x = raw[2] - 256 if raw[2] >= 128 else raw[2]
    y = raw[3] - 256 if raw[3] >= 128 else raw[3]
    if -127 <= x <= 127 and -127 <= y <= 127:
        return {
            "classification": "standard_mouse_pointer",
            "value_status": "valid",
            "pointer_motion": x != 0 or y != 0,
            "buttons": raw[1] & 7,
            "x": x,
            "y": y,
        }
    return {
        "classification": "descriptor_value_invalid",
        "value_status": "descriptor_value_invalid",
        "pointer_motion": False,
    }


def _finish_open(open_record: dict | None) -> dict:
    if open_record is None:
        raise TraceError("truncated trace")
    raw = b"".join(open_record["chunks"])
    if not open_record["chunks"] or len(raw) != open_record["nbytes"]:
        raise TraceError("truncated trace")
    for index, chunk in enumerate(open_record["chunks"]):
        last = index + 1 == len(open_record["chunks"])
        if len(chunk) < 1 or len(chunk) > CHUNK or (not last and len(chunk) != CHUNK):
            raise TraceError("length")
    return open_record


def _check_seen(logged: dict[int, int], seen: dict[int, int]) -> None:
    for ident, cap in CAP.items():
        if logged[ident] < cap:
            if seen[ident] != logged[ident]:
                raise TraceError("order")
        elif seen[ident] < logged[ident]:
            raise TraceError("order")


def decode(console: bytes, *, source: str, cycle: str, descriptor_sha256: str) -> dict:
    """Parse one full console. Non-DL24 lines stay in the nontrace hash.

    A malformed DL24 line raises. It is not omitted in order to return a capture.
    """
    _binding(source, cycle, descriptor_sha256)
    rows = _lines(console)
    trace: list[str] = []
    nontrace: list[bytes] = []
    sole_at: list[int] = []
    header_at: int | None = None
    for index, row in enumerate(rows):
        if row.startswith(b"DL24_TP_"):
            try:
                text = row.decode("ascii")
            except UnicodeDecodeError as error:
                raise TraceError("corrupt trace") from error
            trace.append(text)
            if header_at is None and text == INIT_LINE:
                header_at = index
        else:
            nontrace.append(row)
            try:
                text = row.decode("ascii")
            except UnicodeDecodeError:
                text = ""
            if text.startswith(SOLE_PROBE):
                sole_at.append(index)
    if not trace:
        if len(sole_at) > 1:
            raise TraceError("duplicate")
        return _result(source, cycle, [], nontrace, initialized=False)

    if header_at is None or len(sole_at) != 1 or sole_at[0] >= header_at:
        raise TraceError("binding: sole probe")
    if trace[0] != INIT_LINE:
        if trace[0].startswith("DL24_TP_TRACE"):
            raise TraceError("corrupt trace")
        raise TraceError("order")
    if trace.count(INIT_LINE) != 1:
        raise TraceError("duplicate")

    records: list[dict] = []
    open_record: dict | None = None
    logged = {0x02: 0, 0x3F: 0, 0x44: 0}
    last_seen = {0x02: 0, 0x3F: 0, 0x44: 0}
    ordinary_motion = False
    reason2 = False

    for line in trace[1:]:
        begin = BEGIN.fullmatch(line)
        hex_line = HEX.fullmatch(line)
        end = END.fullmatch(line)
        if begin:
            if open_record is not None:
                raise TraceError("truncated trace")
            if len(records) >= MAX_RECORDS:
                raise TraceError("length")
            seq = _canon(begin[1], 1, MAX_RECORDS, "seq")
            if seq != len(records) + 1:
                raise TraceError("order")
            ident = int(begin[2], 16)
            if ident not in WIRE_LENGTH:
                raise TraceError("foreign id")
            nbytes = _canon(begin[3], 1, max(WIRE_LENGTH.values()), "bytes")
            if nbytes != WIRE_LENGTH[ident]:
                raise TraceError("length")
            reason = _canon(begin[4], 1, 2, "reason")
            seen = {
                0x02: _canon(begin[5], 0, SEEN_LIMIT, "seen02"),
                0x3F: _canon(begin[6], 0, SEEN_LIMIT, "seen3f"),
                0x44: _canon(begin[7], 0, SEEN_LIMIT, "seen44"),
            }
            if any(seen[ident_key] < last_seen[ident_key] for ident_key in seen):
                raise TraceError("order")
            if seen[ident] <= last_seen[ident]:
                raise TraceError("order")
            open_record = {
                "seq": seq,
                "id": ident,
                "reason": reason,
                "nbytes": nbytes,
                "seen": seen,
                "chunks": [],
            }
            continue
        if hex_line:
            if open_record is None:
                raise TraceError("truncated trace")
            seq = _canon(hex_line[1], 1, MAX_RECORDS, "seq")
            index = _canon(hex_line[2], 0, 64, "index")
            if seq != open_record["seq"] or index != len(open_record["chunks"]):
                raise TraceError("order")
            chunk = bytes.fromhex(hex_line[3])
            if len(chunk) < 1 or len(chunk) > CHUNK:
                raise TraceError("length")
            if index > 0 and len(open_record["chunks"][-1]) != CHUNK:
                raise TraceError("length")
            used = sum(len(item) for item in open_record["chunks"])
            if used + len(chunk) > open_record["nbytes"]:
                raise TraceError("length")
            open_record["chunks"].append(chunk)
            continue
        if end:
            if open_record is None:
                raise TraceError("truncated trace")
            seq = _canon(end[1], 1, MAX_RECORDS, "seq")
            nbytes = _canon(end[2], 1, max(WIRE_LENGTH.values()), "bytes")
            if seq != open_record["seq"]:
                raise TraceError("order")
            if nbytes != open_record["nbytes"]:
                raise TraceError("length")
            raw = b"".join(open_record["chunks"])
            if len(raw) != nbytes:
                raise TraceError("truncated trace")
            _finish_open(open_record)
            if raw[0] != open_record["id"]:
                raise TraceError("foreign id")
            digest = crc16_arc(raw)
            if f"{digest:04x}" != end[3]:
                raise TraceError("crc")
            classified = _classify(open_record["id"], raw)
            ident = open_record["id"]
            reason = open_record["reason"]
            seen = open_record["seen"]
            if reason == 2:
                if ident != 0x02 or reason2 or logged[0x02] != CAP[0x02] or ordinary_motion:
                    raise TraceError("reason")
                if not classified["pointer_motion"]:
                    raise TraceError("reason")
                reason2 = True
            else:
                if logged[ident] >= CAP[ident]:
                    raise TraceError("length")
                logged[ident] += 1
                if seen[ident] != logged[ident]:
                    raise TraceError("order")
                if ident == 0x02 and classified["pointer_motion"]:
                    ordinary_motion = True
            _check_seen(logged, seen)
            last_seen = seen
            record = {
                "seq": seq,
                "id": f"{ident:02x}",
                "reason": reason,
                "nbytes": nbytes,
                "seen02": seen[0x02],
                "seen3f": seen[0x3F],
                "seen44": seen[0x44],
                "crc16": end[3],
                "sha256": hashlib.sha256(raw).hexdigest(),
                "raw_hex": raw.hex(),
                "capture_valid": True,
                "classification": classified["classification"],
                "value_status": classified["value_status"],
                "pointer_motion": classified["pointer_motion"],
            }
            if classified["value_status"] == "valid":
                record["buttons"] = classified["buttons"]
                record["x"] = classified["x"]
                record["y"] = classified["y"]
            records.append(record)
            open_record = None
            continue
        raise TraceError("corrupt trace")

    if open_record is not None:
        raise TraceError("truncated trace")
    return _result(source, cycle, records, nontrace, initialized=True)


def _result(source: str, cycle: str, records: list[dict], nontrace: list[bytes], *, initialized: bool) -> dict:
    raw = b"".join(bytes.fromhex(record["raw_hex"]) for record in records)
    motion = any(record["pointer_motion"] for record in records)
    return {
        "status": "NO_INPUT_CAPTURED" if not records else "TRACE_CAPTURED",
        "capture_valid": True,
        "initialized": initialized,
        "sample_count": len(records),
        "samples_sha256": hashlib.sha256(raw).hexdigest(),
        "pointer_motion": motion,
        "physical_input_proven": False,
        "physical_panel_observed": False,
        "source": source,
        "cycle": cycle,
        "descriptor_sha256": DESCRIPTOR_SHA256,
        "nontrace_line_count": len(nontrace),
        "nontrace_sha256": hashlib.sha256(b"\n".join(nontrace)).hexdigest(),
        "inferences": dict(INFERENCES),
        "records": records,
    }
