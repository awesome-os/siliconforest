"""Original pure source-facts assembly. No acquisition, target write or policy.

The caller supplies immutable fresh same-boot bytes and trusted selected-image
labels. Hash equality cannot authenticate that caller or establish quiescence.
Original736 and native1152 profiles are explicit source declarations, never
inferred solely from Revision/Version or indistinguishable zero-tail bytes.
"""
from dataclasses import dataclass
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys

PROFILE = "t8103-pinned-hv-source-facts-v1"
ORIGINAL_PROFILE = "pinned-m1n1-r2-736-v1"
HV_SHA256 = bytes.fromhex("18da80e496122233cbd3d69a287e5737ca78da083e8e36c8a33b01b35744c4a6")
HANDOFF_SHA256 = "087ece7919a22e5afbbd9246580bc981356ff1b1db9ea0736f46c08e364858ec"
INVENTORY_SHA256 = "41863f689ae54e9c5d0d625f196c3f4242bd24beb1644e1fb08782a0b2e84b7b"
PAGE = 1 << 14
PA_LIMIT = 1 << 36


class Refused(ValueError):
    """Missing source, unsupported profile or inconsistent copied facts."""


def _require(condition, message):
    if not condition:
        raise Refused(message)


def _module(relative, digest, name):
    _require(type(digest) is str and len(digest) == 64, "dependency not frozen")
    path = Path(__file__).resolve().parents[1] / relative
    with path.open("rb") as file:
        raw = file.read((128 << 10) + 1)
    _require(len(raw) <= 128 << 10 and hashlib.sha256(raw).hexdigest() == digest,
             "source dependency changed")
    spec = importlib.util.spec_from_loader(name, loader=None, origin=str(path))
    module = importlib.util.module_from_spec(spec)
    module.__file__ = str(path)
    sys.modules[name] = module
    exec(compile(raw, str(path), "exec"), module.__dict__)
    return module


handoff = _module("native_policy_handoff_002/handoff.py", HANDOFF_SHA256,
                  "_source_facts_handoff_002")
facts = handoff.facts
bootargs = handoff.bootargs


@dataclass(frozen=True)
class Context:
    nonce: bytes
    kernel_sha256: bytes
    ramdisk_sha256: bytes
    hv_source_sha256: bytes


@dataclass(frozen=True)
class Video:
    base: int
    display: int
    stride: int
    width: int
    height: int
    depth: int
    visible: object


@dataclass(frozen=True)
class Measurement:
    facts_bytes: int
    final_adt_bytes: int
    source_counts: tuple
    base_adt_sha256: str
    original_bootargs_sha256: str
    mcc_raw_sha256: str


@dataclass(frozen=True)
class Bundle:
    wire: bytes
    expected: object
    plan: object
    measurement: Measurement
    # Full original raw properties/segment fields remain in private facts.
    inventory: object

    def metadata(self):
        return dict(status="DERIVED_COPIED_SOURCE_FACTS_NOT_RUNTIME_OR_POLICY",
                    facts_sha256=hashlib.sha256(self.wire).hexdigest(),
                    facts_bytes=len(self.wire), source_counts=self.measurement.source_counts,
                    base_adt_sha256=self.measurement.base_adt_sha256,
                    original_bootargs_sha256=self.measurement.original_bootargs_sha256,
                    mcc_raw_sha256=self.measurement.mcc_raw_sha256,
                    handoff=self.plan.metadata(), raw_source_values_printed=False,
                    target_write_performed=False, global_exclusion_coverage=False)


def _context(context):
    _require(type(context) is Context, "context type")
    _require(type(context.nonce) is bytes and len(context.nonce) == 24 and any(context.nonce),
             "same-boot nonce")
    for digest in (context.kernel_sha256, context.ramdisk_sha256, context.hv_source_sha256):
        _require(type(digest) is bytes and len(digest) == 32 and any(digest), "image/source digest")
    _require(context.hv_source_sha256 == HV_SHA256, "unsupported HV source profile")


def _span(base, size, alignment=1):
    _require(type(base) is int and type(size) is int and base >= 0 and size > 0 and
             base < PA_LIMIT and size <= PA_LIMIT - base and
             base % alignment == 0 and size % alignment == 0, "source physical extent")
    return facts.Range(base, size)


def _contains(outer, inner):
    return outer.base <= inner.base and inner.base + inner.bytes <= outer.base + outer.bytes


def _original(raw, profile):
    _require(type(profile) is str and profile == ORIGINAL_PROFILE, "explicit original736 source profile")
    _require(type(raw) is bytes and len(raw) == 736 and struct.unpack_from("<HH", raw) == (2, 2),
             "original736 R2/V2 common layout")
    phys, size = struct.unpack_from("<QQ", raw, 16)
    envelope = _span(phys, size, PAGE)
    video_values = struct.unpack_from("<6Q", raw, 40)
    base, display, stride, width, height, depth = video_values
    _require(base > 0 and base % 4 == 0 and width > 0 and height > 0 and stride > 0 and stride % 4 == 0 and
             stride >= width * 4 and depth & 0xff == 30, "T8103 source video geometry")
    visible = _span(base, stride * height)
    return envelope, Video(*video_values, visible)


def _properties_digest(tag, properties):
    # Exact serialized source properties include original padding and headers.
    digest = hashlib.sha256(b"BCM_SOURCE_PROPERTIES_V1\0" + struct.pack("<I", tag))
    for prop in sorted(properties, key=lambda p: (p.path, p.key)):
        for raw in (prop.path.encode("ascii"), prop.key.encode("ascii"), prop.serialized):
            digest.update(struct.pack("<Q", len(raw)))
            digest.update(raw)
    return digest.digest()


def _sources(base_adt, original, mcc_raw, context, profile, original_profile):
    _require(type(profile) is str and profile == PROFILE, "explicit producer profile")
    _context(context)
    handoff._check_dependencies()
    inventory_module = _module("adt_source_inventory/inventory.py", INVENTORY_SHA256,
                               "_source_facts_adt_inventory")
    inventory = inventory_module.extract(base_adt, profile=inventory_module.PROFILE)
    dram = _span(inventory.dram.base, inventory.dram.bytes, 4096)
    _require(dram.base == facts.DRAM_BASE, "T8103 DRAM base")
    old_envelope, video = _original(original, original_profile)
    _require(_contains(dram, old_envelope), "original envelope outside chosen DRAM")
    vram = _span(inventory.vram.base, inventory.vram.bytes)
    _require(_contains(dram, vram), "named VRAM outside chosen DRAM profile")
    _require(_contains(vram, video.visible), "declared VRAM does not contain visible source footprint")
    # This independently source-pinned model matches frozen mcp_decode. It
    # validates all384 bytes; neither old80 lists nor equality-only capture pass.
    slots = facts._mcc(mcc_raw, dram)
    counts = (len(slots), len(inventory.pmap), 1, len(inventory.physical),
              len(inventory.remap), 1)
    _require(sum(counts) <= facts.MAX_RECORDS, "complete source records exceed256")
    measured = Measurement(facts.HEADER + facts.RECORD_BYTES * sum(counts),
                           handoff.planned_size(base_adt, facts.HEADER + facts.RECORD_BYTES * sum(counts)),
                           counts, inventory.base_adt_sha256, hashlib.sha256(original).hexdigest(),
                           hashlib.sha256(mcc_raw).hexdigest())
    return inventory, dram, old_envelope, video, vram, slots, measured


def measure(base_adt, original_bootargs, mcc_raw, context, *, profile, original_profile):
    """Determine size before final native BootArgs encoding; allocate nothing."""
    return _sources(base_adt, original_bootargs, mcc_raw, context, profile, original_profile)[-1]


def produce(base_adt, original_bootargs, native_bootargs, mcc_raw, context, allocation,
            *, profile, original_profile):
    """Derive all supplied source classes, encode facts and plan one insertion.

    The allocation/layout must already be prepared. Original/native envelopes
    follow the pinned HV's preserved high end; this is not a global VM/device
    exclusion census, nor independent proof of an observed allocation's owner.
    """
    inventory, dram, old, video, vram, slots, measured = _sources(
        base_adt, original_bootargs, mcc_raw, context, profile, original_profile)
    native = bootargs.decode(native_bootargs, wire_profile=bootargs.WIRE_PROFILE)
    envelope = _span(native["phys_base"], native["mem_size"], PAGE)
    _require(old.base <= envelope.base and envelope.base + envelope.bytes == old.base + old.bytes,
             "pinned HV must preserve original envelope high")
    _require(_contains(dram, envelope), "native envelope outside chosen DRAM")
    _require(native["devtree_size"] == measured.final_adt_bytes, "final native ADT size not measured")
    for key in ("base", "stride", "width", "height", "depth"):
        _require(native["video"][key] == getattr(video, key), "native/source video mismatch")
    # display may differ: the source paths select serial/text/display modes.
    top = native["top_of_kernel_data"]
    _require(envelope.base < top <= envelope.base + envelope.bytes, "native boot prefix bounds")
    boot_end = (top + PAGE - 1) & ~(PAGE - 1)
    retained = _span(envelope.base, boot_end - envelope.base, PAGE)
    _require(_contains(envelope, retained), "rounded boot prefix outside envelope")
    identity = facts.Expected(context.nonce, (context.kernel_sha256, context.ramdisk_sha256,
                              context.hv_source_sha256, hashlib.sha256(base_adt).digest(),
                              hashlib.sha256(original_bootargs).digest(), hashlib.sha256(native_bootargs).digest()))
    records = [facts.Record(1, slot, 0, 0, span) for slot, span in slots]
    records.extend(facts.Record(2, i, record.wimg, record.signature,
                               facts.Range(record.span.base, record.span.bytes))
                   for i, record in enumerate(inventory.pmap))
    records.append(facts.Record(3, 0, 0, 0, vram))
    records.extend(facts.Record(4, i, 0, 0, facts.Range(record.span.base, record.span.bytes))
                   for i, record in enumerate(inventory.physical))
    records.extend(facts.Record(5, i, 0, 0, facts.Range(record.span.base, record.span.bytes))
                   for i, record in enumerate(inventory.remap))
    records.append(facts.Record(6, 0, 0, 0, retained))
    selected = lambda keys: tuple(p for p in inventory.properties if (p.path, p.key) in keys)
    pmap_props = selected({("/defaults", "pmap-io-ranges")})
    segment_props = tuple(p for p in inventory.properties if p.key in ("segment-names", "segment-ranges"))
    vram_props = selected({("/vram", "reg"), ("/vram", "device_type"),
                           ("", "#address-cells"), ("", "#size-cells")})
    _require(len(pmap_props) == 1 and len(vram_props) == 4, "required source property provenance")
    digests = (hashlib.sha256(b"BCM_MCC_V1\0" + mcc_raw).digest(),
               _properties_digest(2, pmap_props), _properties_digest(3, vram_props),
               _properties_digest(4, segment_props), _properties_digest(5, segment_props),
               hashlib.sha256(b"BCM_BOOT_PREFIX_V1\0" + hashlib.sha256(native_bootargs).digest() +
                              struct.pack("<QQ", retained.base, retained.bytes)).digest())
    sources = tuple(facts.Source(tag, facts.OBSERVED_NONEMPTY if count else facts.OBSERVED_EMPTY,
                                count, digests[tag - 1]) for tag, count in enumerate(measured.source_counts, 1))
    snapshot = facts.Snapshot(identity, dram, envelope, retained, (envelope,), sources,
                              mcc_raw, tuple(records))
    wire = facts.encode(snapshot, identity)
    _require(len(wire) == measured.facts_bytes, "source measurement changed")
    planned = handoff.plan(base_adt, wire, identity, native_bootargs, allocation)
    return Bundle(wire, identity, planned, measured, inventory)
