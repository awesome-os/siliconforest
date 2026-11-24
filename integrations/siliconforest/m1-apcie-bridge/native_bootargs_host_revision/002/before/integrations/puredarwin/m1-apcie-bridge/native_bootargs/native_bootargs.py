"""Original native BootArgs wire codec and pure bounded write planner.

The profile is explicit, not inferred from numeric Revision/Version. No I/O,
backend, proxy, native call or actual target-memory write exists in this module.
"""
from dataclasses import dataclass
import hashlib
import struct

WIRE_PROFILE = "darwin-xnu-e7-arm64-bootargs1152-r2v2-v1"
WIRE_SIZE = 1152
COMMAND_OFFSET = 108
COMMAND_BYTES = 1024
FLAGS_OFFSET = 1136
ACTUAL_OFFSET = 1144
U64_LIMIT = 1 << 64
FIELDS = frozenset(("revision", "version", "virt_base", "phys_base", "mem_size",
                    "top_of_kernel_data", "video", "machine_type", "devtree",
                    "devtree_size", "cmdline", "boot_flags", "mem_size_actual"))
VIDEO_FIELDS = ("base", "display", "stride", "width", "height", "depth")
# These are ABI offsets, not policy/RAM assertions or a revision-indexed table.
COMMON = (("virt_base", 8), ("phys_base", 16), ("mem_size", 24),
          ("top_of_kernel_data", 32), ("devtree", 96))
PADDING = ((4, 8), (92, 96), (1132, 1136))


class BootArgsError(ValueError):
    """Unrecognized profile, malformed wire/values, or invalid write plan."""


def _profile(profile):
    if type(profile) is not str or profile != WIRE_PROFILE:
        raise BootArgsError("explicit native1152 wire profile required")


def _integer(value, bits, name):
    if type(value) is not int or not 0 <= value < (1 << bits):
        raise BootArgsError(name + " must be an unsigned integer of the exact width")
    return value


def _dict(value, keys, name):
    # No arbitrary Mapping/Container callbacks, coercion, backend objects or extras.
    if type(value) is not dict or value.keys() != keys:
        raise BootArgsError(name + " has unexpected type or fields")


def _command(value):
    if type(value) is not str:
        raise BootArgsError("cmdline must be a string")
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise BootArgsError("cmdline must be ASCII without embedded NUL") from exc
    if len(raw) >= COMMAND_BYTES or b"\0" in raw:
        raise BootArgsError("cmdline must fit ASCII without embedded NUL plus NUL")
    return raw


def encode(values, *, wire_profile):
    """Encode exact trusted field values. Does not read legacy/native tail bytes.

    Inputs are plain dictionaries with exact integer/string values. Envelope,
    firmware trust and actual native allocation review remain caller obligations.
    """
    _profile(wire_profile)
    _dict(values, FIELDS, "values")
    _dict(values["video"], frozenset(VIDEO_FIELDS), "video")
    revision = _integer(values["revision"], 16, "revision")
    version = _integer(values["version"], 16, "version")
    if (revision, version) != (2, 2):
        raise BootArgsError("this profile preserves only source Revision2/Version2")
    command = _command(values["cmdline"])
    payload = bytearray(WIRE_SIZE)
    struct.pack_into("<HH", payload, 0, revision, version)
    for key, offset in COMMON:
        struct.pack_into("<Q", payload, offset, _integer(values[key], 64, key))
    for i, key in enumerate(VIDEO_FIELDS):
        struct.pack_into("<Q", payload, 40 + i * 8,
                         _integer(values["video"][key], 64, "video." + key))
    struct.pack_into("<I", payload, 88, _integer(values["machine_type"], 32, "machine_type"))
    struct.pack_into("<I", payload, 104, _integer(values["devtree_size"], 32, "devtree_size"))
    payload[COMMAND_OFFSET:COMMAND_OFFSET + len(command)] = command
    struct.pack_into("<Q", payload, FLAGS_OFFSET, _integer(values["boot_flags"], 64, "boot_flags"))
    struct.pack_into("<Q", payload, ACTUAL_OFFSET,
                     _integer(values["mem_size_actual"], 64, "mem_size_actual"))
    return bytes(payload)


def decode(payload, *, wire_profile):
    """Strict canonical native1152 decode; no legacy length/revision fallback."""
    _profile(wire_profile)
    if type(payload) is not bytes or len(payload) != WIRE_SIZE:
        raise BootArgsError("exact immutable native1152 bytes required")
    for low, high in PADDING:
        if any(payload[low:high]):
            raise BootArgsError("reserved padding must be zero")
    revision, version = struct.unpack_from("<HH", payload, 0)
    if (revision, version) != (2, 2):
        raise BootArgsError("unexpected source Revision/Version")
    raw = payload[COMMAND_OFFSET:COMMAND_OFFSET + COMMAND_BYTES]
    stop = raw.find(b"\0")
    if stop < 0 or any(raw[stop + 1:]):
        raise BootArgsError("cmdline needs NUL followed only by zero fill")
    try:
        command = raw[:stop].decode("ascii")
    except UnicodeDecodeError as exc:
        raise BootArgsError("cmdline must be ASCII without embedded NUL") from exc
    _command(command)
    values = {"revision": revision, "version": version, "cmdline": command}
    for key, offset in COMMON:
        values[key] = struct.unpack_from("<Q", payload, offset)[0]
    values["video"] = {key: struct.unpack_from("<Q", payload, 40 + i * 8)[0]
                       for i, key in enumerate(VIDEO_FIELDS)}
    values["machine_type"] = struct.unpack_from("<I", payload, 88)[0]
    values["devtree_size"] = struct.unpack_from("<I", payload, 104)[0]
    values["boot_flags"] = struct.unpack_from("<Q", payload, FLAGS_OFFSET)[0]
    values["mem_size_actual"] = struct.unpack_from("<Q", payload, ACTUAL_OFFSET)[0]
    return values


@dataclass(frozen=True)
class WritePlan:
    wire_profile: str
    address: int
    end: int
    payload: bytes
    payload_sha256: str
    allocation_base: int
    allocation_bytes: int
    allocation_review_sha256: str

    def metadata(self):
        """Fresh JSON-compatible metadata, without exposing bytes or writing."""
        return {"wire_profile": self.wire_profile, "address": self.address,
                "end": self.end, "bytes": len(self.payload),
                "payload_sha256": self.payload_sha256,
                "allocation_base": self.allocation_base,
                "allocation_bytes": self.allocation_bytes,
                "allocation_review_sha256": self.allocation_review_sha256}


def plan_write(values, *, wire_profile, address, allocation):
    """Return a plan only inside an explicitly supplied reviewed BA allocation.

    Allocation is a plain {base, bytes, review_sha256} dict. A digest is a binding
    label, not authentication or proof of RAM/ownership. The caller must supply
    the actual separately reviewed guest-BootArgs allocation; this helper cannot
    establish that provenance. No target/backing object is accepted or touched.
    """
    _profile(wire_profile)
    _dict(allocation, frozenset(("base", "bytes", "review_sha256")), "allocation")
    base = _integer(allocation["base"], 64, "allocation.base")
    size = _integer(allocation["bytes"], 64, "allocation.bytes")
    address = _integer(address, 64, "address")
    digest = allocation["review_sha256"]
    if type(digest) is not str or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise BootArgsError("allocation review needs an exact lowercase SHA256 binding")
    if not base or not size or base + size > U64_LIMIT:
        raise BootArgsError("invalid reviewed allocation extent")
    if address % 8 or address < base or address + WIRE_SIZE > base + size:
        raise BootArgsError("native payload outside/alignment of reviewed allocation")
    payload = encode(values, wire_profile=wire_profile)
    return WritePlan(wire_profile, address, address + WIRE_SIZE, payload,
                     hashlib.sha256(payload).hexdigest(), base, size, digest)
