"""Strict host parser for one shared-desktop export span.

The caller supplies the host nonce, the two instance ids, and the phase.
Carriage returns are removed, then each interior is the exact normalized
bytes between its wrappers. Frozen decoders then check those bytes.
The return value is payloads and hashes.
"""
import hashlib
import importlib.util
import re
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_DESKTOP_DECODER = _HERE.parent / "records" / "decode_records.py"
_TERMINAL_DECODER = (
    _HERE.parent.parent.parent / "m1-userspace-diagnostics" / "terminal-records" / "decode_records.py"
)
DESKTOP_DECODER_SHA256 = "60ec15988f3ce5a7a03e198b510129070d00eec091525b0bd9fa029f09d3b5df"
TERMINAL_DECODER_SHA256 = "c8f2193dbf843d1970b56f9c0aaed06f2aab43c6348a66f5201a08408977c37f"
_MAX_BYTES = 32 << 20
_HEX48 = re.compile(r"[0-9a-f]{48}\Z")
_PATTERNS = (
    ("session_begin", re.compile(br"DL23_SESSION_EXPORT_BEGIN_(?P<nonce>[0-9a-f]{48}) phase=(?P<phase>live|closed)\Z")),
    ("session_end", re.compile(br"DL23_SESSION_EXPORT_END_(?P<nonce>[0-9a-f]{48}) rc=(?P<rc>-?[0-9]+)\Z")),
    ("app_begin", re.compile(br"DL23_APP_EXPORT_BEGIN_(?P<nonce>[0-9a-f]{48}) instance=(?P<instance>[0-9a-f]{48}) phase=(?P<phase>live|closed)\Z")),
    ("terminal_end", re.compile(br"DL23_APP_TERMINAL_EXPORT_END_(?P<nonce>[0-9a-f]{48}) instance=(?P<instance>[0-9a-f]{48}) rc=(?P<rc>-?[0-9]+)\Z")),
    ("app_end", re.compile(br"DL23_APP_EXPORT_END_(?P<nonce>[0-9a-f]{48}) instance=(?P<instance>[0-9a-f]{48}) terminal=(?P<terminal>-?[0-9]+) logs=(?P<logs>-?[0-9]+)\Z")),
    ("live_foot", re.compile(br"DL23_DESKTOP_LIVE_EXPORT_(?P<nonce>[0-9a-f]{48}) failed=-?[0-9]+ cleanup_claimed=false physical_input_proven=false\Z")),
    ("closed_foot", re.compile(br"DL23_DESKTOP_CLOSED_EXPORT_(?P<nonce>[0-9a-f]{48}) failed=-?[0-9]+ physical_input_proven=false\Z")),
)
_STATUS_KEYS = ("rc", "terminal", "logs")


def _load(path, module_name, expected):
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ValueError("Frozen decoder hash") from error
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("Frozen decoder hash")
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise ValueError("Frozen decoder hash")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.decode


_decode_desktop = _load(_DESKTOP_DECODER, "m1_desktop_session_dl22_records", DESKTOP_DECODER_SHA256)
_decode_terminal = _load(_TERMINAL_DECODER, "m1_userspace_diagnostics_dl11_records", TERMINAL_DECODER_SHA256)


def _canonical(value):
    return isinstance(value, str) and _HEX48.fullmatch(value) is not None


def _match_export(line):
    for name, pattern in _PATTERNS:
        match = pattern.fullmatch(line)
        if match:
            return name, match
    return None, None


def _group(match, key):
    if key not in match.re.groupindex:
        return None
    return match.group(key).decode("ascii")


def _exact_interior(lines):
    # The split removed the newline that terminated the last record line.
    if not lines:
        raise ValueError("Incomplete export")
    return b"\n".join(lines) + b"\n"


def _prefixed(lines, prefix):
    for line in lines:
        if not line.startswith(prefix):
            raise ValueError("Noise in export block")
    return _exact_interior(lines)


def decode_exports(raw, host_nonce, instances, phase):
    if not isinstance(raw, bytes) or len(raw) > _MAX_BYTES or not _canonical(host_nonce):
        raise ValueError("Invalid capture/nonce")
    if phase not in ("live", "closed"):
        raise ValueError("Invalid phase")
    if isinstance(instances, (str, bytes)) or not isinstance(instances, (tuple, list)) or len(instances) != 2:
        raise ValueError("Invalid instances")
    if instances[0] == instances[1] or not _canonical(instances[0]) or not _canonical(instances[1]):
        raise ValueError("Invalid instances")
    supplied = (instances[0], instances[1])
    known = set(supplied)
    lines = raw.replace(b"\r", b"").split(b"\n")
    if lines and lines[-1] == b"":
        lines.pop()
    expected = "session_begin"
    index = 0
    interior = []
    session = None
    pending_terminal = None
    applications = []
    saw_footer = False

    for line in lines:
        name, match = _match_export(line)
        if name is None:
            if line.startswith(b"DL23_") and b"EXPORT" in line:
                raise ValueError("Malformed export")
            kind = b"DL11_RECORD_" if line.startswith(b"DL11_RECORD_") else b"DL22_RECORD_" if line.startswith(b"DL22_RECORD_") else None
            if expected in ("session_end", "terminal_end", "app_end"):
                want = b"DL11_RECORD_" if expected == "terminal_end" else b"DL22_RECORD_"
                if kind != want:
                    raise ValueError("Noise in export block")
                interior.append(line)
                continue
            if kind is not None:
                raise ValueError("Record outside export")
            if expected == "session_begin" or expected == "post":
                continue
            raise ValueError("Noise in export block")

        nonce = _group(match, "nonce")
        if nonce != host_nonce:
            raise ValueError("Foreign nonce")
        instance = _group(match, "instance")
        if instance is not None and instance not in known:
            raise ValueError("Unknown instance")
        for key in _STATUS_KEYS:
            if key in match.re.groupindex and match.group(key) != b"0":
                raise ValueError("Export status")
        marker_phase = _group(match, "phase")
        if marker_phase is not None and marker_phase != phase:
            raise ValueError("Phase mismatch")
        if name in ("live_foot", "closed_foot") and name != ("live_foot" if phase == "live" else "closed_foot"):
            raise ValueError("Phase mismatch")

        if name == "session_begin" and expected == "session_begin":
            expected = "session_end"
            continue
        if name == "session_end" and expected == "session_end":
            session = _decode_desktop(_prefixed(interior, b"DL22_RECORD_"), host_nonce, "session", phase)
            interior = []
            expected = "app_begin"
            continue
        if name == "app_begin" and expected == "app_begin" and instance == supplied[index]:
            expected = "terminal_end"
            continue
        if name == "terminal_end" and expected == "terminal_end" and instance == supplied[index]:
            # DL11 skips non-record lines. The prefix check above is what rejects them.
            pending_terminal = _decode_terminal(_prefixed(interior, b"DL11_RECORD_"), instance, phase)
            interior = []
            expected = "app_end"
            continue
        if name == "app_end" and expected == "app_end" and instance == supplied[index]:
            logs = _decode_desktop(_prefixed(interior, b"DL22_RECORD_"), instance, "application", phase)
            applications.append({"instance": instance, "terminal": pending_terminal, "logs": logs})
            pending_terminal = None
            interior = []
            index += 1
            expected = "app_begin" if index < len(supplied) else "post"
            continue
        if name in ("live_foot", "closed_foot") and expected == "post":
            if saw_footer:
                raise ValueError("Duplicate export")
            saw_footer = True
            continue
        if expected == "post":
            raise ValueError("Duplicate export")
        raise ValueError("Wrapper order")

    if expected != "post" or session is None or len(applications) != 2:
        raise ValueError("Incomplete export")
    return {"session": session, "applications": tuple(applications)}
