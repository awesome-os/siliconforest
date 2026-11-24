"""Original bounded base-ADT source extraction, not policy/authority/acquisition.

Raw values and paths belong in private caller records. No physical access,
registry, ADT writer, interval union, or native owner conversion exists here.
"""
from dataclasses import dataclass
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys

PROFILE = "t8103-base-adt-source-inventory-v1"
PARSER_SHA256 = "087ece7919a22e5afbbd9246580bc981356ff1b1db9ea0736f46c08e364858ec"
PARSER_MAX = 128 << 10
PA_LIMIT = 1 << 36
SOURCE_RECORD_LIMIT = 256

class Refused(ValueError):
    """Malformed, ambiguous, unsupported or over-capacity source facts."""

def _require(ok, why):
    if not ok:
        raise Refused(why)

def _digest(raw):
    return hashlib.sha256(raw).hexdigest()

def _parser():
    # Private _parse is deliberate and bound to these exact original bytes.
    path = Path(__file__).resolve().parents[1] / "native_policy_handoff_002/handoff.py"
    try:
        with path.open("rb") as source:
            raw = source.read(PARSER_MAX + 1)
        _require(len(raw) <= PARSER_MAX and _digest(raw) == PARSER_SHA256,
                 "frozen parser source changed")
        spec = importlib.util.spec_from_loader("_adt_inventory_handoff_002", loader=None, origin=str(path))
        module = importlib.util.module_from_spec(spec)
        module.__file__ = str(path)
        sys.modules[spec.name] = module
        exec(compile(raw, str(path), "exec"), module.__dict__)
        return module
    except Refused:
        raise
    except (OSError, ValueError, SyntaxError) as error:
        raise Refused("frozen parser dependency unavailable/invalid") from error

@dataclass(frozen=True)
class Span:
    base: int
    bytes: int

@dataclass(frozen=True)
class Property:
    path: str
    key: str
    value: bytes
    serialized: bytes
    value_sha256: str
    serialized_sha256: str

@dataclass(frozen=True)
class PmapRecord:
    path: str
    slot: int
    raw: bytes
    span: Span
    wimg: int
    signature: int

@dataclass(frozen=True)
class SegmentRecord:
    path: str
    slot: int
    ordinal: int
    name: str  # display label only: repeated labels are not identities
    raw: bytes
    phys: int
    iova_opaque: int  # never PA-validated or used in range arithmetic
    remap_numerical: int
    bytes: int
    opaque: int

@dataclass(frozen=True)
class SourceSpan:
    path: str
    slot: int
    ordinal: int
    span: Span
    status: str

@dataclass(frozen=True)
class Inventory:
    profile: str
    base_adt_sha256: str
    base_adt_bytes: int
    dram: Span
    vram: Span
    properties: tuple
    pmap: tuple
    segments: tuple
    physical: tuple
    remap: tuple

    def metadata(self):
        # No captured source ranges/paths/names/raw bytes in this public summary.
        return dict(status="EXTRACTED_SOURCE_FACTS_ONLY_NOT_POLICY_OR_RUNTIME",
                    profile=self.profile, base_adt_sha256=self.base_adt_sha256,
                    base_adt_bytes=self.base_adt_bytes, property_count=len(self.properties),
                    pmap_count=len(self.pmap),
                    pmap_observation_status="OBSERVED_NONEMPTY" if self.pmap else "OBSERVED_EMPTY",
                    segment_count=len(self.segments),
                    physical_count=len(self.physical), remap_count=len(self.remap),
                    remap_nonzero_count=sum(r.span.base != 0 for r in self.remap),
                    zero_remap_retained_count=sum(r.span.base == 0 for r in self.remap),
                    vram_count=1,
                    projected_source_records=len(self.pmap) + len(self.physical) + len(self.remap) + 1,
                    raw_device_values_printed=False, physical_access_performed=False,
                    source_completeness_or_policy_authority=False)

def _span(base, size):
    _require(size > 0 and base < PA_LIMIT and
             size <= PA_LIMIT - base, "source extent is zero/overflow/outside PA36")
    return Span(base, size)

def _property(base, path, prop):
    serialized = base[prop.begin:prop.end]
    return Property(path, prop.key, prop.value, serialized, _digest(prop.value), _digest(serialized))

def _get(node, key):
    matches = [p for p in node.properties if p.key == key]
    _require(len(matches) == 1, "required source property missing/ambiguous")
    return matches[0]

def extract(base_adt, *, profile):
    _require(type(profile) is str and profile == PROFILE, "explicit source profile required")
    _require(type(base_adt) is bytes, "immutable exact base ADT bytes required")
    parser = _parser()
    try:
        nodes = parser._parse(base_adt)
    except ValueError as error:
        raise Refused("bounded exact ADT parse refused") from error
    _require("/defaults" in nodes and "/chosen" in nodes and "/vram" in nodes, "required base nodes missing")
    _require(not any(p.key == parser.PROPERTY for p in nodes["/chosen"].properties),
             "input must be base ADT, not silently canonicalized inserted facts")
    _require(not any(p.key in ("ranges", "dma-ranges") for p in nodes[""].properties),
             "unsupported root address translation metadata")
    cells = [_get(nodes[""], key) for key in ("#address-cells", "#size-cells")]
    _require(all(len(p.value) == 4 and struct.unpack("<I", p.value)[0] == 2 for p in cells),
             "fixed root address/size cells must be exact2/2")
    dram_props = [_get(nodes["/chosen"], key) for key in ("dram-base", "dram-size")]
    _require(all(len(p.value) == 8 for p in dram_props), "chosen DRAM properties must be exact8 bytes")
    dram = _span(*(struct.unpack("<Q", p.value)[0] for p in dram_props))
    vram_type, vram_reg = [_get(nodes["/vram"], key) for key in ("device_type", "reg")]
    _require(vram_type.value == b"vram\0" and len(vram_reg.value) == 16,
             "fixed /vram type/reg schema")
    vram = _span(*struct.unpack("<QQ", vram_reg.value))
    properties = ([_property(base_adt, "", p) for p in cells] +
                  [_property(base_adt, "/chosen", p) for p in dram_props] +
                  [_property(base_adt, "/vram", p) for p in (vram_type, vram_reg)])
    pmap_properties = [(path, p) for path, node in nodes.items() for p in node.properties
                       if p.key == "pmap-io-ranges"]
    _require(len(pmap_properties) == 1 and pmap_properties[0][0] == "/defaults",
             "required unique /defaults pmap-io-ranges missing/ambiguous")
    _, prop = pmap_properties[0]
    _require(len(prop.value) % 24 == 0, "pmap record framing")
    _require(len(prop.value) // 24 + 1 <= SOURCE_RECORD_LIMIT, "source record capacity; never truncate")
    properties.append(_property(base_adt, "/defaults", prop))
    pmap = []
    for slot in range(len(prop.value) // 24):
        raw = prop.value[slot * 24:(slot + 1) * 24]
        base, size, wimg, signature = struct.unpack("<QQII", raw)
        pmap.append(PmapRecord("/defaults", slot, raw, _span(base, size), wimg, signature))
    segments, physical, remap = [], [], []
    for path, node in sorted(nodes.items()):
        candidates = {p.key: p for p in node.properties if p.key.startswith("segment-")}
        if not candidates:
            continue
        _require(set(candidates) == {"segment-ranges", "segment-names"},
                 "unknown/dangling segment metadata")
        ranges, names = candidates["segment-ranges"], candidates["segment-names"]
        _require(len(ranges.value) > 0 and len(ranges.value) % 32 == 0, "segment record framing")
        count = len(ranges.value) // 32
        _require(len(pmap) + 2 * (len(segments) + count) + 1 <= SOURCE_RECORD_LIMIT,
                 "projected pmap/physical/ALL-remap source capacity; never truncate")
        name_bytes = names.value
        _require(len(name_bytes) > 1 and name_bytes[-1:] == b"\0" and b"\0" not in name_bytes[:-1] and
                 all(0x20 <= value < 0x7f for value in name_bytes[:-1]), "segment names bytes")
        labels = name_bytes[:-1].split(b";")
        _require(len(labels) == count and all(labels), "segment names/ranges cardinality")
        properties.extend((_property(base_adt, path, ranges), _property(base_adt, path, names)))
        for slot, label in enumerate(labels):
            raw = ranges.value[slot * 32:(slot + 1) * 32]
            phys, iova, mapped, size, opaque = struct.unpack("<QQQII", raw)
            physical_span = _span(phys, size)
            remap_span = _span(mapped, size)
            ordinal = len(segments)
            segments.append(SegmentRecord(path, slot, ordinal, label.decode("ascii"), raw,
                                          phys, iova, mapped, size, opaque))
            physical.append(SourceSpan(path, slot, ordinal, physical_span, "PHYSICAL_SOURCE_OBSERVATION"))
            remap.append(SourceSpan(path, slot, ordinal, remap_span,
                                    "ZERO_NUMERICAL_VALUE_NOT_ABSENCE" if mapped == 0 else
                                    "NONZERO_NUMERICAL_SOURCE_ONLY"))
    # Repeated labels/full rows are legitimate distinct observations by path+slot.
    identities = [(s.path, s.slot) for s in segments]
    _require(len(identities) == len(set(identities)), "duplicate segment path/slot")
    return Inventory(profile, _digest(base_adt), len(base_adt), dram, vram,
                     tuple(sorted(properties, key=lambda p: (p.path, p.key))),
                     tuple(pmap), tuple(segments), tuple(physical), tuple(remap))
