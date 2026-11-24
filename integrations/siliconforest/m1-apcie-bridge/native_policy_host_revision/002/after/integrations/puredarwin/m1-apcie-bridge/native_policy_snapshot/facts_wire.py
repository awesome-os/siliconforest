"""Original bounded platform-facts codec; no hardware or policy authority.

Expected identities originate in a separately trusted boot context. Equality
here cannot authenticate an untrusted producer. Source digests are declared
labels, not recomputed hashes of source bytes absent from this format.
"""
from dataclasses import dataclass
import struct

HEADER = 1280
RECORD_BYTES = 32
MAX_RECORDS = 256
MAX_BYTES = HEADER + RECORD_BYTES * MAX_RECORDS
PA_LIMIT = 1 << 36
DRAM_BASE = 0x800000000
CPU_PAGE = 1 << 14
SOURCE_TAGS = (1, 2, 3, 4, 5, 6)
MCC, PMAP_IO, FRAMEBUFFER, DEVICE_PHYS, REMAP_SOURCE_ONLY, RETAINED_BOOT = SOURCE_TAGS
OBSERVED_EMPTY, OBSERVED_NONEMPTY = 1, 2
MAGIC = b"BCMFCT01"


class Refused(ValueError):
    """Malformed, inconsistent, changed-identity or over-capacity facts."""


@dataclass(frozen=True)
class Range:
    base: int
    bytes: int


@dataclass(frozen=True)
class Expected:
    nonce: bytes
    # KC, ramdisk, HV, base ADT, original BootArgs, final native BootArgs.
    identities: tuple


@dataclass(frozen=True)
class Source:
    tag: int
    status: int
    count: int
    declared_sha256: bytes


@dataclass(frozen=True)
class Record:
    tag: int
    ordinal: int
    flags: int
    auxiliary: int
    span: Range


@dataclass(frozen=True)
class Snapshot:
    identity: Expected
    dram: Range
    envelope: Range
    retained_boot: Range
    ram: tuple
    sources: tuple
    mcc_raw: bytes
    records: tuple


@dataclass(frozen=True)
class Observation:
    facts: Snapshot
    # Full original enabled MCC slot intervals, not the merged form.
    mcc_slots: tuple
    # Derived full-boundary union of relevant physical-source intervals.
    exclusion_union: tuple
    status: str = "STRUCTURALLY_VALID_FACTS_NOT_POLICY"


def _require(ok, why):
    if not ok:
        raise Refused(why)


def _uint(value, bits, label):
    _require(type(value) is int and 0 <= value < (1 << bits), label)
    return value


def _bytes(value, size, label):
    _require(type(value) is bytes and len(value) == size and any(value), label)


def _expected(value):
    _require(type(value) is Expected, "expected identity type")
    _bytes(value.nonce, 24, "nonce")
    _require(type(value.identities) is tuple and len(value.identities) == 6,
             "six identity digests required")
    for digest in value.identities:
        _bytes(digest, 32, "identity digest")


def _range(value, alignment=1):
    _require(type(value) is Range, "range type")
    _uint(value.base, 64, "range base")
    _uint(value.bytes, 64, "range bytes")
    _require(value.bytes > 0 and value.base < PA_LIMIT and
             value.bytes <= PA_LIMIT - value.base, "physical range")
    _require(value.base % alignment == 0 and value.bytes % alignment == 0,
             "range alignment")


def _inside(inner, outer):
    return outer.base <= inner.base and inner.base + inner.bytes <= outer.base + outer.bytes


def _intersects(a, b):
    return max(a.base, b.base) < min(a.base + a.bytes, b.base + b.bytes)


def _mcc(raw, dram):
    """Independent numerical model of frozen mcp_decode's declared profile."""
    _require(type(raw) is bytes and len(raw) == 384, "MCC requires exact384 bytes")
    _require(raw[:192] == raw[192:], "MCC changed between passes")
    words = struct.unpack("<96I", raw)
    for plane in range(1, 4):
        _require(words[:12] == words[plane * 12:(plane + 1) * 12],
                 "MCC plane disagreement")
    result = []
    for slot in range(4):
        start, end, enabled = words[slot * 3:slot * 3 + 3]
        if not enabled:
            continue
        _require(start != 0 and end > start, "MCC enabled bounds")
        low, high = start << 12, (end + 1) << 12
        _require(low < PA_LIMIT and high <= PA_LIMIT, "MCC raw PA width")
        low, high = low | DRAM_BASE, high | DRAM_BASE
        _require(low >= dram.base and low < high <= dram.base + dram.bytes and
                 high <= PA_LIMIT, "MCC decoded bounds")
        result.append((slot, Range(low, high - low)))
    return tuple(result)


def _union(records, ram):
    spans = sorted((r.span for r in records if r.tag != REMAP_SOURCE_ONLY and
                    any(_intersects(r.span, interval) for interval in ram)),
                   key=lambda r: (r.base, r.bytes))
    result = []
    for span in spans:
        if result and span.base <= result[-1].base + result[-1].bytes:
            previous = result[-1]
            result[-1] = Range(previous.base,
                               max(previous.base + previous.bytes,
                                   span.base + span.bytes) - previous.base)
        else:
            result.append(span)
    _require(len(result) <= 8, "derived union exceeds8; no truncation")
    return tuple(result)


def _validate(facts, expected):
    _require(type(facts) is Snapshot, "snapshot type")
    _expected(expected)
    _expected(facts.identity)
    _require(facts.identity == expected, "same-boot identity mismatch")
    _range(facts.dram, 4096)
    _require(facts.dram.base == DRAM_BASE, "T8103 DRAM base")
    _range(facts.envelope, CPU_PAGE)
    _require(_inside(facts.envelope, facts.dram), "envelope outside DRAM")
    _range(facts.retained_boot, CPU_PAGE)
    _require(type(facts.ram) is tuple and 1 <= len(facts.ram) <= 8,
             "original RAM count")
    previous_end = 0
    for interval in facts.ram:
        _range(interval, CPU_PAGE)
        _require(_inside(interval, facts.envelope) and interval.base >= previous_end,
                 "original RAM bounds/order")
        previous_end = interval.base + interval.bytes
    _require(any(_inside(facts.retained_boot, r) for r in facts.ram),
             "boot reservation crosses original RAM boundary")
    _require(type(facts.sources) is tuple and len(facts.sources) == 6,
             "required source descriptors")
    _require(type(facts.records) is tuple and len(facts.records) <= MAX_RECORDS,
             "source records capacity")
    by_tag = {tag: [] for tag in SOURCE_TAGS}
    previous_key = (0, -1)
    for record in facts.records:
        _require(type(record) is Record, "record type")
        for value in (record.tag, record.ordinal, record.flags, record.auxiliary):
            _uint(value, 32, "record field")
        _require(record.tag in by_tag and (record.tag, record.ordinal) > previous_key,
                 "source record order/duplicate")
        previous_key = (record.tag, record.ordinal)
        _range(record.span)
        _require(record.tag == PMAP_IO or (record.flags == 0 and record.auxiliary == 0),
                 "unsupported source flags")
        by_tag[record.tag].append(record)
    for tag, source in zip(SOURCE_TAGS, facts.sources):
        _require(type(source) is Source, "source type")
        for value in (source.tag, source.status, source.count):
            _uint(value, 32, "source field")
        _require(source.tag == tag and source.count == len(by_tag[tag]),
                 "source tag/count mismatch")
        _require(source.status == (OBSERVED_NONEMPTY if source.count else OBSERVED_EMPTY),
                 "unobserved/unsupported source")
        _bytes(source.declared_sha256, 32, "declared source digest")
        if tag != MCC:
            _require([r.ordinal for r in by_tag[tag]] == list(range(source.count)),
                     "source ordinals must remain dense")
    slots = _mcc(facts.mcc_raw, facts.dram)
    _require([(r.ordinal, r.span) for r in by_tag[MCC]] == list(slots),
             "MCC slot records differ from complete raw snapshot")
    _require(len(by_tag[FRAMEBUFFER]) == 1, "one framebuffer allocation required")
    _require(len(by_tag[RETAINED_BOOT]) == 1 and
             by_tag[RETAINED_BOOT][0].span == facts.retained_boot,
             "retained boot source mismatch")
    return Observation(facts, slots, _union(facts.records, facts.ram))


def encode(facts, expected):
    """Encode only validated immutable values. No source acquisition occurs."""
    _validate(facts, expected)
    raw = bytearray(HEADER + RECORD_BYTES * len(facts.records))
    raw[:8] = MAGIC
    struct.pack_into("<11I", raw, 8, 1, len(raw), 1, 14, 36, 1,
                     len(facts.ram), len(facts.records), 6, 384, 1)
    raw[64:88] = facts.identity.nonce
    for index, digest in enumerate(facts.identity.identities):
        raw[88 + index * 32:120 + index * 32] = digest
    for offset, span in ((320, facts.dram), (336, facts.envelope),
                         (352, facts.retained_boot)):
        struct.pack_into("<QQ", raw, offset, span.base, span.bytes)
    for index, span in enumerate(facts.ram):
        struct.pack_into("<QQ", raw, 368 + index * 16, span.base, span.bytes)
    for index, source in enumerate(facts.sources):
        offset = 496 + index * 48
        struct.pack_into("<HHI", raw, offset, source.tag, source.status, source.count)
        raw[offset + 8:offset + 40] = source.declared_sha256
    raw[784:1168] = facts.mcc_raw
    for index, record in enumerate(facts.records):
        struct.pack_into("<IIIIQQ", raw, HEADER + index * RECORD_BYTES,
                         record.tag, record.ordinal, record.flags, record.auxiliary,
                         record.span.base, record.span.bytes)
    return bytes(raw)


def decode(raw, expected):
    """Reference decoder. Valid facts never constitute native policy approval."""
    _expected(expected)
    _require(type(raw) is bytes and HEADER <= len(raw) <= MAX_BYTES, "wire bounds/type")
    _require(raw[:8] == MAGIC, "wire magic")
    values = struct.unpack_from("<11I", raw, 8)
    version, total, profile, shift, pa_bits, wire, ram_count, record_count, classes, mcc_bytes, coverage = values
    _require((version, total, profile, shift, pa_bits, wire, classes, mcc_bytes, coverage) ==
             (1, len(raw), 1, 14, 36, 1, 6, 384, 1), "wire profile/size")
    _require(1 <= ram_count <= 8 and record_count <= MAX_RECORDS and
             len(raw) == HEADER + RECORD_BYTES * record_count, "wire counts")
    _require(not any(raw[52:64] + raw[280:320] + raw[1168:1280]),
             "reserved header bytes")
    identity = Expected(raw[64:88], tuple(raw[88 + i * 32:120 + i * 32] for i in range(6)))
    ranges = [Range(*struct.unpack_from("<QQ", raw, offset)) for offset in (320, 336, 352)]
    ram = tuple(Range(*struct.unpack_from("<QQ", raw, 368 + i * 16)) for i in range(ram_count))
    _require(not any(raw[368 + ram_count * 16:496]), "unused RAM slots")
    sources = []
    for index in range(6):
        offset = 496 + index * 48
        _require(not any(raw[offset + 40:offset + 48]), "source reserved bytes")
        tag, status, count = struct.unpack_from("<HHI", raw, offset)
        sources.append(Source(tag, status, count, raw[offset + 8:offset + 40]))
    records = []
    for index in range(record_count):
        tag, ordinal, flags, auxiliary, base, size = struct.unpack_from("<IIIIQQ", raw, HEADER + index * RECORD_BYTES)
        records.append(Record(tag, ordinal, flags, auxiliary, Range(base, size)))
    facts = Snapshot(identity, *ranges, ram, tuple(sources), raw[784:1168], tuple(records))
    return _validate(facts, expected)
