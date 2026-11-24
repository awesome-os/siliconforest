"""Original pure dual-source facts assembly, profile v2. Host only.

No acquisition, device access, target write, allocation or policy. The caller
supplies two separate immutable captures from one boot: the ORIGINAL ADT read
before the pinned HV edited it, and the PREPARED base ADT handed to the guest.
Over the frozen inventory's properties only, this proves that PREPARED is
ORIGINAL minus the pinned HV's USB0 subtree deletion and its first __OS_LOG row
removal, and nothing else. It then emits every ORIGINAL observation.

Hash equality cannot authenticate the caller, prove quiescence or establish
global exclusion coverage. Original736 and native1152 profiles are explicit
source declarations, never inferred from Revision/Version alone.
"""
from dataclasses import dataclass
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

PROFILE = "t8103-original-prepared-subset-source-facts-v2"
ORIGINAL_PROFILE = "pinned-m1n1-r2-736-v1"
HV_SHA256 = bytes.fromhex("18da80e496122233cbd3d69a287e5737ca78da083e8e36c8a33b01b35744c4a6")
HANDOFF_SHA256 = "087ece7919a22e5afbbd9246580bc981356ff1b1db9ea0736f46c08e364858ec"
INVENTORY_SHA256 = "41863f689ae54e9c5d0d625f196c3f4242bd24beb1644e1fb08782a0b2e84b7b"
SOURCE_IODEV = 4          # pinned proxy IODEV.USB0, normalized to a built-in int
RECEIPT_SCHEMA = 2
JOIN_RECEIPT_MAX = 65536  # refuse above this, never truncate
UNION_CAP = 8             # enforced by the frozen facts codec during encode
MCC_SLOT_MAX = 4
# Contract caps as stated in every receipt.
CAPS = dict(records=256, physical_union=UNION_CAP, receipt_bytes=65536, mcc_slots=MCC_SLOT_MAX)
PAGE = 1 << 14
PA_LIMIT = 1 << 36
OS_LOG = "__OS_LOG"

# Exact setup_adt templates from the pinned HV (atc-phy%d appears twice there;
# the second delete is a swallowed KeyError). USB0 uses index 0.
HV_USB_TEMPLATES = (
    "/arm-io/dart-usb%d", "/arm-io/atc-phy%d", "/arm-io/usb-drd%d",
    "/arm-io/acio%d", "/arm-io/acio-cpu%d", "/arm-io/dart-acio%d",
    "/arm-io/apciec%d", "/arm-io/dart-apciec%d", "/arm-io/apciec%d-piodma",
    "/arm-io/i2c0/hpmBusManager/hpm%d", "/arm-io/i2c0/hpmBusManager1/hpm%d",
    "/arm-io/i2c3/hpmBusManager0/hpm%d", "/arm-io/nub-spmi-a0/hpm%d",
    "/arm-io/atc%d-dpxbar", "/arm-io/atc%d-dpphy", "/arm-io/atc%d-dpin0",
    "/arm-io/atc%d-dpin1", "/arm-io/atc-phy%d",
)
SELECTED_PATHS = tuple(dict.fromkeys(t % (SOURCE_IODEV - 4) for t in HV_USB_TEMPLATES))
# Pinned setup_adt product rewrite, run once in HV.init before any load_raw.
HV_MARKER = " m1n1 hypervisor"
HV_SUFFIX = " on m1n1 hypervisor"
HV_SOC = (("PINNED_SETUP_ADT_PLAIN", "Virtual %s on m1n1 hypervisor"),
          ("PINNED_SETUP_ADT_CURSED_SPRR", "Virtual %s on cursed m1n1 hypervisor"))


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
                  "_source_facts_v2_handoff_002")
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
class Projection:
    # Prepared ordinal -> original ordinal, per class. Pmap is the identity.
    pmap: tuple
    segments: tuple
    removed_properties: tuple   # original property indices (inventory order)
    removed_segments: tuple     # (original segment ordinal, reason)
    rewritten_properties: tuple  # prepared property indices


@dataclass(frozen=True)
class Measurement:
    # The six fields kept from profile v1; base_adt_sha256 is the PREPARED tree.
    facts_bytes: int
    final_adt_bytes: int
    source_counts: tuple
    base_adt_sha256: str
    original_bootargs_sha256: str
    mcc_raw_sha256: str
    # Added by profile v2.
    original_adt_sha256: str
    source_iodev: int
    source_join_receipt: bytes


@dataclass(frozen=True)
class Bundle:
    wire: bytes
    expected: object
    plan: object
    measurement: Measurement
    # Private caller data. The wire rows come from original_inventory only.
    original_inventory: object
    prepared_inventory: object
    projection: Projection

    def metadata(self):
        # Counts, hashes and status only; no raw addresses, paths, names or bytes.
        m = self.measurement
        return dict(status="DERIVED_DUAL_SOURCE_FACTS_NOT_RUNTIME_OR_POLICY", profile=PROFILE,
                    facts_sha256=hashlib.sha256(self.wire).hexdigest(), facts_bytes=len(self.wire),
                    source_counts=m.source_counts, base_adt_sha256=m.base_adt_sha256,
                    prepared_adt_identity_slot=3, original_adt_sha256=m.original_adt_sha256,
                    original_bootargs_sha256=m.original_bootargs_sha256,
                    mcc_raw_sha256=m.mcc_raw_sha256, source_iodev=m.source_iodev,
                    source_join_receipt_sha256=hashlib.sha256(m.source_join_receipt).hexdigest(),
                    source_join_receipt_bytes=len(m.source_join_receipt),
                    original_inventory_counts=_counts(self.original_inventory),
                    prepared_inventory_counts=_counts(self.prepared_inventory),
                    emitted_origin="ORIGINAL", handoff=self.plan.metadata(),
                    guest_origin_visibility="OPAQUE_COMPOSITE_LABELS_ONLY",
                    raw_source_values_printed=False, target_write_performed=False,
                    global_exclusion_coverage=False)


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


def _selected(path):
    return any(path == prefix or path.startswith(prefix + "/") for prefix in SELECTED_PATHS)


def _oslog_eligible(path):
    # Pinned load_raw visits /arm-io children and their children only.
    parts = path.split("/")
    return path.startswith("/arm-io/") and len(parts) in (3, 4)


def _product_strings(raw):
    nodes = handoff._parse(raw)
    _require("/product" in nodes, "product node missing")
    values = []
    for key in ("product-name", "product-description", "product-soc-name"):
        prop = nodes["/product"].get(key)
        value = prop.value
        _require(not prop.template and len(value) > 1 and value[-1:] == b"\0" and b"\0" not in value[:-1] and
                 all(0x20 <= c < 0x7f for c in value[:-1]), "product string must be one printable C string")
        values.append(value[:-1].decode("ascii"))
    return values


def _product_rewrite(original_adt, base_adt):
    """ORIGINAL must predate setup_adt; PREPARED must carry exactly one application."""
    name, description, soc = _product_strings(original_adt)
    new_name, new_description, new_soc = _product_strings(base_adt)
    _require(not any(HV_MARKER in value for value in (name, description, soc)),
             "original ADT already carries the pinned HV product rewrite")
    _require(new_name == name + HV_SUFFIX, "prepared product-name is not the single pinned HV rewrite")
    _require(new_description == description + HV_SUFFIX,
             "prepared product-description is not the single pinned HV rewrite")
    variants = [label for label, template in HV_SOC if new_soc == template % soc]
    _require(len(variants) == 1, "prepared product-soc-name is not the single pinned HV rewrite")
    return variants[0]


def _rebuilt(key, value):
    # Pinned m1n1 ADTPropertyStruct rebuild of a rewritten property: key
    # zero-padded to 32, u32 size, value, zero padding to 4 bytes.
    return key.encode("ascii").ljust(32, b"\0") + struct.pack("<I", len(value)) + value + b"\0" * ((-len(value)) & 3)


def _projection(original, prepared):
    """Exact restricted projection proof and deterministic occurrence maps."""
    for inventory in (original, prepared):
        identities = [(p.path, p.key) for p in inventory.properties]
        _require(len(identities) == len(set(identities)), "duplicate inventoried property identity")
    groups = {}
    for row in original.segments:
        groups.setdefault(row.path, []).append(row)
    kept, removed, rewritten = {}, [], {}
    for path, rows in groups.items():
        _require([r.slot for r in rows] == list(range(len(rows))), "original segment slots")
        if _selected(path):
            removed.extend((r.ordinal, "USB0_SELECTED_SUBTREE") for r in rows)
            continue
        first = None
        if _oslog_eligible(path):
            first = next((i for i, r in enumerate(rows) if r.name == OS_LOG), None)
        if first is None:
            kept[path] = rows
            continue
        removed.append((rows[first].ordinal, "FIRST_OS_LOG"))
        remain = rows[:first] + rows[first + 1:]
        _require(remain, "pinned removal would empty a segment property; unsupported")
        kept[path] = remain
        rewritten[(path, "segment-names")] = ";".join(r.name for r in remain).encode("ascii") + b"\0"
        rewritten[(path, "segment-ranges")] = b"".join(r.raw for r in remain)
    expected, removed_properties = [], []
    for index, prop in enumerate(original.properties):
        identity = (prop.path, prop.key)
        if _selected(prop.path):
            removed_properties.append(index)
        elif identity in rewritten:
            value = rewritten[identity]
            expected.append((identity, value, _rebuilt(prop.key, value)))
        else:
            # Unchanged properties stay byte-exact, header and padding included.
            expected.append((identity, prop.value, prop.serialized))
    actual = [((p.path, p.key), p.value, p.serialized) for p in prepared.properties]
    _require(actual == expected, "prepared tree is not the exact pinned USB0 projection of the original")
    rewritten_properties = tuple(i for i, p in enumerate(prepared.properties)
                                 if (p.path, p.key) in rewritten)
    # Everything below is implied by the equality above. It is kept as an
    # explicit statement of the map contract and is not mutation-counted.
    _require(original.dram == prepared.dram and original.vram == prepared.vram,
             "original/prepared DRAM or VRAM differs")
    _require(len(original.pmap) == len(prepared.pmap), "pmap occurrence count changed")
    pmap_map = []
    for index, (old, new) in enumerate(zip(original.pmap, prepared.pmap)):
        _require((old.path, old.slot, old.raw) == (new.path, new.slot, new.raw),
                 "prepared pmap row is not the same original occurrence")
        pmap_map.append(index)
    # Identity is node + slot position in the projected node + whole row + label.
    segment_map = []
    for row in prepared.segments:
        rows = kept.get(row.path)
        _require(rows is not None and row.slot < len(rows), "prepared segment has no original occurrence")
        source = rows[row.slot]
        _require((source.path, source.name, source.raw) == (row.path, row.name, row.raw),
                 "prepared segment changed an original whole row")
        segment_map.append(source.ordinal)
    kept_ordinals = sorted(r.ordinal for rows in kept.values() for r in rows)
    _require(segment_map == kept_ordinals, "prepared occurrence map incomplete, reordered or not injective")
    _require(sorted(segment_map + [o for o, _ in removed]) == list(range(len(original.segments))),
             "original segment occurrences not partitioned by the projection")
    # One segment map serves both derived observation classes.
    _require(len(prepared.physical) == len(prepared.remap) == len(segment_map),
             "prepared derived observation counts")
    for index, target in enumerate(segment_map):
        for new, old in ((prepared.physical[index], original.physical[target]),
                         (prepared.remap[index], original.remap[target])):
            _require(new.ordinal == index and old.ordinal == target and
                     (new.path, new.span, new.status) == (old.path, old.span, old.status),
                     "derived observation does not follow the segment occurrence map")
    return Projection(tuple(pmap_map), tuple(segment_map), tuple(removed_properties),
                      tuple(sorted(removed)), rewritten_properties)


def _counts(inventory):
    return dict(properties=len(inventory.properties), pmap=len(inventory.pmap),
                segments=len(inventory.segments), physical=len(inventory.physical),
                numerical_remap=len(inventory.remap),
                zero_numerical_remap=sum(r.span.base == 0 for r in inventory.remap), vram=1)


def _frame(digest, raw):
    digest.update(struct.pack("<Q", len(raw)))
    digest.update(raw)


def _frame_properties(digest, properties):
    digest.update(struct.pack("<I", len(properties)))
    for prop in properties:
        for raw in (prop.path.encode("ascii"), prop.key.encode("ascii"), prop.serialized):
            _frame(digest, raw)


def _properties_sha256(inventory):
    digest = hashlib.sha256(b"BCM_SOURCE_PROPERTIES_V2\0")
    _frame_properties(digest, inventory.properties)
    return digest.hexdigest()


def _rows_sha256(inventory):
    digest = hashlib.sha256(b"BCM_SOURCE_ROWS_V2\0")
    digest.update(struct.pack("<I", len(inventory.pmap)))
    for row in inventory.pmap:
        _frame(digest, row.path.encode("ascii"))
        digest.update(struct.pack("<I", row.slot))
        _frame(digest, row.raw)
    digest.update(struct.pack("<I", len(inventory.segments)))
    for row in inventory.segments:
        _frame(digest, row.path.encode("ascii"))
        digest.update(struct.pack("<II", row.slot, row.ordinal))
        _frame(digest, row.name.encode("ascii"))
        _frame(digest, row.raw)
    return digest.hexdigest()


def _origin(inventory):
    return dict(adt_sha256=inventory.base_adt_sha256, adt_bytes=inventory.base_adt_bytes,
                counts=_counts(inventory), properties_sha256=_properties_sha256(inventory),
                rows_sha256=_rows_sha256(inventory))


def _receipt(original, prepared, projection, counts, original_bootargs, mcc_raw, context, source_iodev,
             rewrite):
    """Canonical private join receipt. Independent of final native BootArgs."""
    records = sum(counts)
    value = dict(
        schema_version=RECEIPT_SCHEMA, profile=PROFILE,
        status="EXACT_PROJECTED_PREPARED_SUBSET_EMITTING_ALL_ORIGINAL_FACTS",
        selection=dict(source_iodev=source_iodev, selector="PINNED_HV_IODEV_USB0_ONLY",
                       hv_source_sha256=context.hv_source_sha256.hex()),
        context=dict(nonce=context.nonce.hex(), kernel_sha256=context.kernel_sha256.hex(),
                     ramdisk_sha256=context.ramdisk_sha256.hex(),
                     original_bootargs_sha256=hashlib.sha256(original_bootargs).hexdigest(),
                     mcc_raw_sha256=hashlib.sha256(mcc_raw).hexdigest()),
        original=_origin(original), prepared=_origin(prepared),
        projection=dict(hv_product_rewrite=rewrite,
                        removed_original_properties=list(projection.removed_properties),
                        removed_original_segments=[dict(ordinal=o, reason=r)
                                                   for o, r in projection.removed_segments],
                        rewritten_prepared_properties=list(projection.rewritten_properties)),
        occurrence_map=dict(pmap=list(projection.pmap), segments=list(projection.segments)),
        emitted=dict(origin="ORIGINAL_ALL_ROWS_UNCHANGED_ORDER_AND_MULTIPLICITY",
                     counts=list(counts), records=records,
                     facts_bytes=facts.HEADER + facts.RECORD_BYTES * records),
        caps=dict(CAPS),
        global_exclusion_coverage=False, guest_independent_origin_visibility=False)
    raw = (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
                      allow_nan=False) + "\n").encode("ascii")
    _require(len(raw) <= JOIN_RECEIPT_MAX, "source join receipt exceeds 65536 bytes; no truncation")
    return raw


def _emission(original):
    # Every ORIGINAL row in inventory order, including rows the prepared tree
    # lacks and rows outside the current RAM window. No filtering of any kind.
    return original.pmap, original.physical, original.remap


def _class_digest(tag, receipt, original, prepared, original_props, prepared_props,
                  cardinality, mapping, emitted):
    digest = hashlib.sha256(b"BCM_DUAL_ORIGIN_CLASS_V2\0" + struct.pack("<I", tag))
    _frame(digest, PROFILE.encode("ascii"))
    _frame(digest, hashlib.sha256(receipt).digest())
    for role, inventory, props in ((b"ORIGINAL", original, original_props),
                                   (b"PREPARED", prepared, prepared_props)):
        _frame(digest, role)
        _frame(digest, bytes.fromhex(inventory.base_adt_sha256))
        digest.update(struct.pack("<QI", inventory.base_adt_bytes, cardinality(inventory)))
        _frame_properties(digest, props)
    digest.update(struct.pack("<I", len(mapping)))
    for target in mapping:
        digest.update(struct.pack("<I", target))
    digest.update(struct.pack("<I", len(emitted)))
    for record in emitted:
        digest.update(struct.pack("<IIIIQQ", record.tag, record.ordinal, record.flags,
                                  record.auxiliary, record.span.base, record.span.bytes))
    return digest.digest()


def _sources(base_adt, original_bootargs, mcc_raw, context, profile, original_profile,
             original_adt, source_iodev):
    _require(type(profile) is str and profile == PROFILE, "explicit producer002 profile")
    _require(type(source_iodev) is int and source_iodev == SOURCE_IODEV,
             "exact built-in int 4 from the pinned HV IODEV.USB0 selection required")
    _require(type(original_adt) is bytes and type(base_adt) is bytes, "immutable original and prepared ADT bytes")
    _context(context)
    handoff._check_dependencies()
    # Rejects an ORIGINAL captured after (any part of) the HV edits, and identical bytes.
    rewrite = _product_rewrite(original_adt, base_adt)
    inventory_module = _module("adt_source_inventory/inventory.py", INVENTORY_SHA256,
                               "_source_facts_v2_inventory")
    original = inventory_module.extract(original_adt, profile=inventory_module.PROFILE)
    prepared = inventory_module.extract(base_adt, profile=inventory_module.PROFILE)
    projection = _projection(original, prepared)
    dram = _span(original.dram.base, original.dram.bytes, 4096)
    _require(dram.base == facts.DRAM_BASE, "T8103 DRAM base")
    old_envelope, video = _original(original_bootargs, original_profile)
    _require(_contains(dram, old_envelope), "original envelope outside chosen DRAM")
    vram = _span(original.vram.base, original.vram.bytes)
    _require(_contains(dram, vram), "named VRAM outside chosen DRAM profile")
    _require(_contains(vram, video.visible), "declared VRAM does not contain visible source footprint")
    slots = facts._mcc(mcc_raw, dram)
    _require(len(slots) <= MCC_SLOT_MAX, "MCC slot count")
    emitted = _emission(original)
    counts = (len(slots), len(emitted[0]), 1, len(emitted[1]), len(emitted[2]), 1)
    _require(sum(counts) <= facts.MAX_RECORDS, "complete original records exceed 256; no truncation")
    receipt = _receipt(original, prepared, projection, counts, original_bootargs, mcc_raw, context,
                       source_iodev, rewrite)
    facts_bytes = facts.HEADER + facts.RECORD_BYTES * sum(counts)
    measured = Measurement(facts_bytes, handoff.planned_size(base_adt, facts_bytes), counts,
                           prepared.base_adt_sha256, hashlib.sha256(original_bootargs).hexdigest(),
                           hashlib.sha256(mcc_raw).hexdigest(), original.base_adt_sha256,
                           source_iodev, receipt)
    return original, prepared, projection, dram, old_envelope, video, vram, slots, measured


def measure(base_adt, original_bootargs, mcc_raw, context, *, profile, original_profile,
            original_adt, source_iodev):
    """Validate both sources and size the facts before native BootArgs exist."""
    return _sources(base_adt, original_bootargs, mcc_raw, context, profile, original_profile,
                    original_adt, source_iodev)[-1]


def produce(base_adt, original_bootargs, native_bootargs, mcc_raw, context, allocation,
            *, profile, original_profile, original_adt, source_iodev):
    """Repeat every check, emit all-original facts and plan one insertion.

    The allocation/layout must already be prepared. The caller compares the
    returned measurement with measure()'s, field for field, before any use.
    This is not a global VM/device exclusion census or proof of ownership.
    """
    original, prepared, projection, dram, old, video, vram, slots, measured = _sources(
        base_adt, original_bootargs, mcc_raw, context, profile, original_profile,
        original_adt, source_iodev)
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
    # Identity slot 3 stays the PREPARED base ADT actually handed to the guest.
    identity = facts.Expected(context.nonce, (context.kernel_sha256, context.ramdisk_sha256,
                              context.hv_source_sha256, hashlib.sha256(base_adt).digest(),
                              hashlib.sha256(original_bootargs).digest(), hashlib.sha256(native_bootargs).digest()))
    pmap, physical, remap = _emission(original)
    by_tag = {
        1: [facts.Record(1, slot, 0, 0, span) for slot, span in slots],
        2: [facts.Record(2, i, r.wimg, r.signature, facts.Range(r.span.base, r.span.bytes))
            for i, r in enumerate(pmap)],
        3: [facts.Record(3, 0, 0, 0, vram)],
        4: [facts.Record(4, i, 0, 0, facts.Range(r.span.base, r.span.bytes)) for i, r in enumerate(physical)],
        5: [facts.Record(5, i, 0, 0, facts.Range(r.span.base, r.span.bytes)) for i, r in enumerate(remap)],
        6: [facts.Record(6, 0, 0, 0, retained)],
    }
    records = tuple(r for tag in sorted(by_tag) for r in by_tag[tag])

    def chosen(inventory, keys):
        return tuple(p for p in inventory.properties if (p.path, p.key) in keys)

    def segment_props(inventory):
        return tuple(p for p in inventory.properties if p.key in ("segment-names", "segment-ranges"))

    pmap_keys = {("/defaults", "pmap-io-ranges")}
    vram_keys = {("/vram", "reg"), ("/vram", "device_type"), ("", "#address-cells"), ("", "#size-cells")}
    _require(all(len(chosen(inv, pmap_keys)) == 1 and len(chosen(inv, vram_keys)) == 4
                 for inv in (original, prepared)), "required dual source property provenance")
    receipt = measured.source_join_receipt
    digests = (
        hashlib.sha256(b"BCM_MCC_V1\0" + mcc_raw).digest(),
        _class_digest(2, receipt, original, prepared, chosen(original, pmap_keys), chosen(prepared, pmap_keys),
                      lambda inv: len(inv.pmap), projection.pmap, by_tag[2]),
        _class_digest(3, receipt, original, prepared, chosen(original, vram_keys), chosen(prepared, vram_keys),
                      lambda inv: 1, (), by_tag[3]),
        _class_digest(4, receipt, original, prepared, segment_props(original), segment_props(prepared),
                      lambda inv: len(inv.physical), projection.segments, by_tag[4]),
        _class_digest(5, receipt, original, prepared, segment_props(original), segment_props(prepared),
                      lambda inv: len(inv.remap), projection.segments, by_tag[5]),
        hashlib.sha256(b"BCM_BOOT_PREFIX_V1\0" + hashlib.sha256(native_bootargs).digest() +
                       struct.pack("<QQ", retained.base, retained.bytes)).digest())
    sources = tuple(facts.Source(tag, facts.OBSERVED_NONEMPTY if count else facts.OBSERVED_EMPTY,
                                count, digests[tag - 1]) for tag, count in enumerate(measured.source_counts, 1))
    snapshot = facts.Snapshot(identity, dram, envelope, retained, (envelope,), sources,
                              mcc_raw, records)
    # encode() validates and refuses a relevant physical union above eight.
    wire = facts.encode(snapshot, identity)
    _require(len(wire) == measured.facts_bytes, "source measurement changed")
    planned = handoff.plan(base_adt, wire, identity, native_bootargs, allocation)
    return Bundle(wire, identity, planned, measured, original, prepared, projection)
