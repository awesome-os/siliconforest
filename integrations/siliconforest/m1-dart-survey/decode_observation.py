"""Strict saved-console decoder. Reads bytes only; never contacts a target."""
import hashlib
import re
import struct

KEY = "PDDARTSurveyDiagnosticV1"
INSTANCE = "PDDARTSurveyObservationV1"
OFFSETS = [0, 4, 0x60, 0xfc, 0x80, 0x84, 0x88, 0x8c]
for _sid in range(16):
    OFFSETS += [0x100 + 4 * _sid] + [0x200 + 16 * _sid + 4 * r for r in range(4)]
GUARDS = [(0, 0x1b8), (0, 0x348), (0, 0x3e8), (0, 0x1a0),
          (2, 0), (1, 0x24), (3, 0x800), (3, 0x814), (3, 0x804)]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def observation(raw):
    require(len(raw) == 764, "Diagnostic length")
    words = struct.unpack("<191I", raw)
    version, count, reads, region, offset = words[:5]
    result = struct.unpack_from("<i", raw, 20)[0]
    require(version == 1 and result in {0, 1, 2, -1, -2, -3, -4}, "Version/result")
    require(count <= 88 and 10 * count <= reads <= min(889, 10 * count + 10), "Sample/read count")
    samples = list(zip(words[15::2], words[16::2]))
    require([x[0] for x in samples[:count]] == OFFSETS[:count], "Sample order")
    require(all(x == (0, 0) for x in samples[count:]), "Unwritten sample tail")
    require(region <= 5 and ((region == 5 and offset == 0) or
            (region, offset) in GUARDS or (region == 4 and offset in OFFSETS)), "Failure location")
    step = reads - 10 * count
    at_guard = 1 <= step <= 9 and (region, offset) == GUARDS[step - 1]
    # Any completed profile check must have passed before a later stop.
    if count >= 1 and not (result == 2 and count == 1):
        require((samples[0][1] >> 24) & 15 == 14, "Earlier page-shift check failed")
    if count >= 3 and not (result == 2 and count == 3):
        require(not samples[2][1] & 0x8000, "Earlier lock check failed")
    if count >= 4 and not (result == 2 and count == 4):
        require(not samples[3][1] & 0xffff0000, "Earlier stream check failed")
    if result == 0:
        require((count, reads, region, offset) == (88, 889, 5, 0), "Incomplete success")
        require((samples[0][1] >> 24) & 15 == 14 and not samples[2][1] & 0x8000 and
                not samples[3][1] & 0xffff0000, "Unsupported success")
        require(all((v & 0x90001c00) == 0 and v & 255 == 255 for v in words[6:10]),
                "Last successful power guard")
        require(words[10] & 0x8f == 15 and all(v & 1 for v in words[11:15]),
                "Last successful clock guard")
    elif result == 1:
        require(at_guard, "Not-ready outside guard")
        # Only this freshly read field is authoritative for the refusal.
        # Other saved guard fields may be stale or unread.
        value = words[6 + step - 1]
        passes = ((value & 0x90001c00) == 0 and value & 255 == 255) if step <= 4 else (
            value & 0x8f == 15 if step == 5 else bool(value & 1))
        require(not passes, "Not-ready guard actually passed")
    elif result == 2:
        cause = ((count == 1 and (samples[0][1] >> 24) & 15 != 14) or
                 (count == 3 and samples[2][1] & 0x8000) or
                 (count == 4 and samples[3][1] & 0xffff0000))
        require(cause and step == 0 and region == 4 and offset == OFFSETS[count - 1],
                "Unsupported cause")
    elif result == -3:
        require((reads == 0 and region == 5) or at_guard or
                (step == 10 and count < 88 and region == 4 and offset == OFFSETS[count]), "IO location")
    elif result in {-1, -2}:
        require(reads == 0 and region == 5, "Argument/board location")
    else:
        # This fixed profile completes in889 reads and88 samples, below1024.
        raise ValueError("Unreachable limit status for this frozen sampler")
    return dict(version=version, sample_count=count, reads=reads, result=result,
                failure_region=region, failure_offset=offset,
                noncoherent=True, initialization_proven=False)


def decode(console, nonce):
    require(isinstance(console, bytes) and len(console) <= 32 << 20, "Console bound")
    require(isinstance(nonce, str) and re.fullmatch("[0-9a-f]{48}", nonce), "Nonce")
    lines = console.replace(b"\r", b"").split(b"\n")
    wanted = [x.decode("ascii") for x in lines if x.startswith(
        (b"DL13_DART_RECORD_", b"DL13_DART_COPY ", b"DL13_DART_VERDICT "))]
    require(len(wanted) <= 35, "Receipt line bound")
    records = {}
    active = None
    chunks = []
    verdict = None
    for line in wanted:
        fields = line.split(" ")
        require(len(fields) >= 3 and fields[1] == nonce, "Wrong receipt nonce")
        kind = fields[0]
        require(verdict is None, "Receipt data after verdict")
        if kind == "DL13_DART_RECORD_BEGIN":
            expected_key = KEY if not records else INSTANCE
            require(active is None and len(fields) == 5 and fields[2] == expected_key and
                    fields[2] not in records and fields[3:] == ["bytes=764", "chunk_bytes=64"],
                    "Record begin")
            active = fields[2]
            chunks = []
        elif kind == "DL13_DART_RECORD_HEX":
            require(active == fields[2] and len(fields) == 5 and len(chunks) < 12 and
                    fields[3] == "index=" + str(len(chunks)), "Chunk index")
            n = 60 if len(chunks) == 11 else 64
            require(re.fullmatch("[0-9a-f]{" + str(2 * n) + "}", fields[4]), "Chunk bytes")
            chunks.append(bytes.fromhex(fields[4]))
        elif kind == "DL13_DART_RECORD_END":
            require(active == fields[2] and len(fields) == 6 and len(chunks) == 12 and
                    fields[3:5] == ["bytes=764", "chunks=12"], "Record end")
            raw = b"".join(chunks)
            require(fields[5] == "sha256=" + hashlib.sha256(raw).hexdigest(), "Digest")
            records[active] = raw
            active = None
        elif kind == "DL13_DART_COPY":
            raise ValueError("Missing/error property copy")
        elif kind == "DL13_DART_VERDICT":
            require(active is None and KEY in records and len(fields) == 10, "Verdict layout")
            parsed = {}
            for field in fields[2:]:
                k, sep, v = field.partition("=")
                require(sep and k not in parsed, "Verdict field")
                parsed[k] = v
            verdict = parsed
        else:
            raise ValueError("Unknown receipt line")
    require(active is None and KEY in records and verdict is not None, "Incomplete receipt")
    summary = observation(records[KEY])
    expected = "SAMPLED" if summary["result"] == 0 else "REFUSED"
    require(verdict.get("verdict") == expected and
            verdict.get("initialization_proven") == "false", "Verdict scope")
    for k, key in [("result", "result"), ("samples", "sample_count"), ("reads", "reads"),
                   ("region", "failure_region"), ("offset", "failure_offset")]:
        require(verdict.get(k) == str(summary[key]), "Verdict/raw mismatch")
    if INSTANCE in records:
        require(records[INSTANCE] == records[KEY] and verdict.get("instance") == "equal", "Instance differs")
    else:
        require(verdict.get("instance") == "absent", "Instance not accounted")
    return dict(summary=summary, records=records, verdict=expected, instance=verdict["instance"])
