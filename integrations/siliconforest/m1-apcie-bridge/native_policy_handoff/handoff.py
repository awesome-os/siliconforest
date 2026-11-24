"""Original pure guest-ADT metadata insertion plan. No target writer or reader.

This validates supplied bytes and consistency only. The trusted producer must
derive genuine same-boot source inventories; this module cannot authenticate
them, approve memory, resize a live allocation, or authorize a boot.
"""
from dataclasses import dataclass
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys

PROPERTY = "darwin-lab-platform-facts"
ADT_LIMIT = 2 << 20
NODE_LIMIT = 4096
PROPERTY_LIMIT = 65536
DEPTH_LIMIT = 32
PA_LIMIT = 1 << 36
U64_MASK = (1 << 64) - 1
PAGE = 1 << 14
DEPENDENCIES = {
    "native_policy_snapshot/facts_wire.py": "473c1513110350a0b959500124a159dd726d2b7cc0cbcce79eae087d7684528a",
    "native_bootargs/native_bootargs.py": "f168d523eacf1c8fe1228ca48e467fb98fea47b4675bb942ca945aae63233fae",
}


class Refused(ValueError):
    """Malformed, inconsistent, stale or insufficiently allocated metadata."""


def _require(condition, message):
    if not condition:
        raise Refused(message)


def _dependency(relative, name):
    path = Path(__file__).resolve().parents[1] / relative
    raw = path.read_bytes()
    _require(len(raw) <= 128 << 10 and hashlib.sha256(raw).hexdigest() == DEPENDENCIES[relative],
             "source dependency changed")
    spec = importlib.util.spec_from_loader(name, loader=None, origin=str(path))
    module = importlib.util.module_from_spec(spec)
    module.__file__ = str(path)
    sys.modules[name] = module
    exec(compile(raw, str(path), "exec"), module.__dict__)
    return module


facts = _dependency("native_policy_snapshot/facts_wire.py", "_handoff_facts_v1")
bootargs = _dependency("native_bootargs/native_bootargs.py", "_handoff_bootargs_v1")
Expected = facts.Expected


def _check_dependencies():
    for relative, digest in DEPENDENCIES.items():
        raw = (Path(__file__).resolve().parents[1] / relative).read_bytes()
        _require(len(raw) <= 128 << 10 and hashlib.sha256(raw).hexdigest() == digest,
                 "source dependency changed")


@dataclass(frozen=True)
class Allocation:
    base: int
    bytes: int


@dataclass(frozen=True)
class _Property:
    key: str
    begin: int
    end: int
    value: bytes
    template: bool


@dataclass(frozen=True)
class _Node:
    path: str
    begin: int
    property_count: int
    properties_end: int
    properties: tuple

    def get(self, key):
        matches = [p for p in self.properties if p.key == key]
        _require(len(matches) == 1, "required property missing or ambiguous")
        return matches[0]


def _parse(raw):
    _require(type(raw) is bytes and 8 <= len(raw) <= ADT_LIMIT, "ADT bytes/size")
    nodes = {}
    properties = 0

    def integer(offset):
        _require(0 <= offset <= len(raw) - 4, "truncated ADT integer")
        return struct.unpack_from("<I", raw, offset)[0]

    def visit(offset, parent, depth):
        nonlocal properties
        _require(depth <= DEPTH_LIMIT and len(nodes) < NODE_LIMIT, "ADT tree limit")
        begin = offset
        count, children = integer(offset), integer(offset + 4)
        _require(1 <= count <= 4096 and children <= NODE_LIMIT, "ADT node counts")
        properties += count
        _require(properties <= PROPERTY_LIMIT, "ADT property limit")
        offset += 8
        props, keys = [], set()
        for _ in range(count):
            _require(offset <= len(raw) - 36, "truncated property header")
            key_field = raw[offset:offset + 32]
            _require(b"\0" in key_field, "property key must be NUL terminated")
            key_raw = key_field.split(b"\0", 1)[0]
            _require(key_raw and all(0x20 <= c < 0x7f for c in key_raw), "property key")
            key = key_raw.decode("ascii")
            _require(key not in keys, "duplicate property")
            keys.add(key)
            size_word = integer(offset + 32)
            # XNU DeviceTreeNodeProperty.length is the full uint32, not a
            # firmware/template bitfield. A high-bit length cannot fit our ADT.
            size = size_word
            data = offset + 36
            padded = (size + 3) & ~3
            _require(padded <= len(raw) - data, "truncated property payload")
            props.append(_Property(key, offset, data + padded, raw[data:data + size],
                                   bool(size_word >> 31)))
            offset = data + padded
        name_prop = [p for p in props if p.key == "name"]
        _require(len(name_prop) == 1, "node name missing")
        name_raw = name_prop[0].value
        _require(1 < len(name_raw) <= 256 and name_raw[-1:] == b"\0" and
                 b"\0" not in name_raw[:-1] and
                 all(0x20 <= c < 0x7f for c in name_raw[:-1]), "node name bytes")
        name = name_raw[:-1].decode("ascii")
        _require(parent is None or ("/" not in name and name not in (".", "..")),
                 "child node name")
        path = "" if parent is None else parent + "/" + name
        _require(path not in nodes, "duplicate node path")
        nodes[path] = _Node(path, begin, count, offset, tuple(props))
        for _ in range(children):
            offset = visit(offset, path, depth + 1)
        return offset

    end = visit(0, None, 0)
    _require(end == len(raw), "ADT trailing bytes")
    return nodes


def canonical_base(raw):
    """Remove only the one fixed /chosen property, preserving all other bytes.

    This deterministic hash preimage avoids a self-referential final-ADT hash.
    It does not validate the removed payload or authenticate any source.
    """
    nodes = _parse(raw)
    _require("/chosen" in nodes, "chosen node missing")
    chosen = nodes["/chosen"]
    matches = [p for p in chosen.properties if p.key == PROPERTY]
    if not matches:
        return raw
    prop = matches[0]
    _require(not prop.template, "facts property cannot be a template")
    result = bytearray(raw[:prop.begin] + raw[prop.end:])
    struct.pack_into("<I", result, chosen.begin, chosen.property_count - 1)
    # Verify the exact splice is a complete tree, not a stale-offset patch.
    _parse(bytes(result))
    return bytes(result)


def planned_size(base_adt, facts_bytes):
    """Exact size before encoding final BootArgs; no hash fixed point needed."""
    nodes = _parse(base_adt)
    _require("/chosen" in nodes, "chosen node missing")
    _require(nodes["/chosen"].property_count < 4096, "no room for additional property")
    _require(not any(p.key == PROPERTY for p in nodes["/chosen"].properties),
             "facts property already present; no implicit replacement")
    _require(type(facts_bytes) is int and 1280 <= facts_bytes <= 9472 and
             (facts_bytes - 1280) % 32 == 0, "facts wire size")
    result = len(base_adt) + 36 + ((facts_bytes + 3) & ~3)
    _require(result <= ADT_LIMIT, "final ADT exceeds bound")
    return result


def _inside(base, size, span):
    return span.base <= base and base + size <= span.base + span.bytes


def _intersects(base, size, span):
    return max(base, span.base) < min(base + size, span.base + span.bytes)


@dataclass(frozen=True)
class HandoffPlan:
    payload: bytes
    base_adt_sha256: str
    final_adt_sha256: str
    facts_sha256: str
    final_bootargs_sha256: str
    allocation: Allocation

    def metadata(self):
        # Addresses/source ranges belong only in private caller records.
        return dict(base_adt_sha256=self.base_adt_sha256,
                    final_adt_sha256=self.final_adt_sha256,
                    facts_sha256=self.facts_sha256,
                    final_bootargs_sha256=self.final_bootargs_sha256,
                    bytes=len(self.payload), capacity=self.allocation.bytes,
                    property=PROPERTY, target_write_performed=False,
                    status="PURE_HANDOFF_PLAN_NOT_RUNTIME_OR_POLICY")


def plan(base_adt, facts_wire, expected, native_bootargs, allocation):
    """Plan one insertion in a supplied, already prepared guest ADT copy.

    Caller must first finalize layout/allocation and all native BootArgs fields,
    using planned_size(). This helper never resizes allocations, modifies a
    loader object, acquires sources, or performs any I/O to a device.
    """
    _check_dependencies()
    nodes = _parse(base_adt)
    final_size = planned_size(base_adt, len(facts_wire)) if type(facts_wire) is bytes else None
    _require(final_size is not None, "facts must be immutable bytes")
    observed = facts.decode(facts_wire, expected)
    values = bootargs.decode(native_bootargs, wire_profile=bootargs.WIRE_PROFILE)
    _require(hashlib.sha256(base_adt).digest() == expected.identities[3], "base ADT identity")
    _require(hashlib.sha256(native_bootargs).digest() == expected.identities[5], "final BootArgs identity")
    _require(type(allocation) is Allocation and type(allocation.base) is int and
             type(allocation.bytes) is int and allocation.base > 0 and allocation.bytes > 0 and
             allocation.base < PA_LIMIT and allocation.bytes <= PA_LIMIT - allocation.base and
             allocation.base % PAGE == 0 and allocation.bytes % PAGE == 0,
             "ADT allocation profile")
    _require(final_size <= allocation.bytes, "ADT allocation insufficient; no resize")
    f = observed.facts
    _require(values["phys_base"] == f.envelope.base and values["mem_size"] == f.envelope.bytes and
             f.retained_boot.base == values["phys_base"] and
             values["phys_base"] < values["top_of_kernel_data"] <= f.retained_boot.base + f.retained_boot.bytes,
             "native envelope/retained boot mismatch")
    _require(_inside(allocation.base, allocation.bytes, f.retained_boot) and
             any(_inside(allocation.base, allocation.bytes, r) for r in f.ram) and
             allocation.base + allocation.bytes <= values["top_of_kernel_data"],
             "ADT allocation outside retained original RAM")
    _require(values["devtree_size"] == final_size and
             values["devtree"] == ((allocation.base - values["phys_base"] + values["virt_base"]) & U64_MASK),
             "native ADT address/size mismatch")
    chosen = nodes["/chosen"]
    for key, span in (("dram-base", f.dram.base), ("dram-size", f.dram.bytes)):
        prop = chosen.get(key)
        _require(not prop.template and len(prop.value) == 8 and
                 struct.unpack("<Q", prop.value)[0] == span, "chosen DRAM mismatch")
    _require("/chosen/memory-map" in nodes, "chosen memory-map missing")
    mm = nodes["/chosen/memory-map"]
    dt = mm.get("DeviceTree")
    _require(not dt.template and dt.value == struct.pack("<QQ", allocation.base, allocation.bytes),
             "DeviceTree allocation descriptor mismatch")
    ba = mm.get("BootArgs")
    _require(not ba.template and len(ba.value) == 16, "BootArgs allocation descriptor")
    ba_base, ba_bytes = struct.unpack("<QQ", ba.value)
    _require(ba_bytes == PAGE and ba_base % PAGE == 0 and
             _inside(ba_base, ba_bytes, f.retained_boot) and
             any(_inside(ba_base, ba_bytes, r) for r in f.ram) and
             ba_base + ba_bytes <= values["top_of_kernel_data"] and
             not _intersects(ba_base, ba_bytes, allocation), "BootArgs allocation bounds/alias")
    for record in f.records:
        if record.tag in (1, 2, 3, 4):
            _require(not _intersects(allocation.base, allocation.bytes, record.span) and
                     not _intersects(ba_base, ba_bytes, record.span), "handoff allocation intersects known exclusion")
    key = PROPERTY.encode("ascii").ljust(32, b"\0")
    prop = key + struct.pack("<I", len(facts_wire)) + facts_wire
    prop += b"\0" * ((-len(facts_wire)) & 3)
    offset = chosen.properties_end
    result = bytearray(base_adt[:offset] + prop + base_adt[offset:])
    struct.pack_into("<I", result, chosen.begin, chosen.property_count + 1)
    payload = bytes(result)
    _require(len(payload) == final_size and canonical_base(payload) == base_adt and
             _parse(payload)["/chosen"].get(PROPERTY).value == facts_wire,
             "ADT insertion roundtrip mismatch")
    return HandoffPlan(payload, hashlib.sha256(base_adt).hexdigest(),
                       hashlib.sha256(payload).hexdigest(), hashlib.sha256(facts_wire).hexdigest(),
                       hashlib.sha256(native_bootargs).hexdigest(), allocation)
