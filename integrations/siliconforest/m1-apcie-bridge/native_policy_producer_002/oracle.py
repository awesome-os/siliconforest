"""Independent recomputation of the producer002 projection, receipt and labels.

Imports nothing from producer.py. The USB0 deletion set is read from the
pinned HV source by AST; maps, receipt bytes and class digests are rebuilt
from the README spec. Host-only test helper.
"""
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

LEAF = Path(__file__).resolve().parent
LAB = LEAF.parents[3]
HV_SOURCE = LAB / "sources/m1n1-baseline/proxyclient/m1n1/hv/__init__.py"
HV_SOURCE_SHA256 = "18da80e496122233cbd3d69a287e5737ca78da083e8e36c8a33b01b35744c4a6"
INVENTORY = LEAF.parent / "adt_source_inventory/inventory.py"
INVENTORY_SHA256 = "41863f689ae54e9c5d0d625f196c3f4242bd24beb1644e1fb08782a0b2e84b7b"
HANDOFF = LEAF.parent / "native_policy_handoff_002/handoff.py"
HANDOFF_SHA256 = "087ece7919a22e5afbbd9246580bc981356ff1b1db9ea0736f46c08e364858ec"
PROFILE = "t8103-original-prepared-subset-source-facts-v2"


def _pinned(path, digest):
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == digest, path
    return raw


def inventory_module():
    raw = _pinned(INVENTORY, INVENTORY_SHA256)
    spec = importlib.util.spec_from_loader("_oracle_inventory", loader=None, origin=str(INVENTORY))
    module = importlib.util.module_from_spec(spec)
    module.__file__ = str(INVENTORY)
    sys.modules[spec.name] = module
    exec(compile(raw, str(INVENTORY), "exec"), module.__dict__)
    return module


def _exec(path, digest, name):
    raw = _pinned(path, digest)
    spec = importlib.util.spec_from_loader(name, loader=None, origin=str(path))
    module = importlib.util.module_from_spec(spec)
    module.__file__ = str(path)
    sys.modules[name] = module
    exec(compile(raw, str(path), "exec"), module.__dict__)
    return module


def product_rewrite(original_adt, prepared_adt):
    """setup_adt runs once in HV.init: original unmarked, prepared marked once."""
    parse = _exec(HANDOFF, HANDOFF_SHA256, "_oracle_handoff")._parse

    def strings(raw):
        node = parse(raw)["/product"]
        return [node.get(k).value.rstrip(b"\0").decode() for k in
                ("product-name", "product-description", "product-soc-name")]

    (name, desc, soc), (name2, desc2, soc2) = strings(original_adt), strings(prepared_adt)
    assert "m1n1 hypervisor" not in name + desc + soc
    assert (name2, desc2) == (name + " on m1n1 hypervisor", desc + " on m1n1 hypervisor")
    plain, cursed = "Virtual " + soc + " on m1n1 hypervisor", "Virtual " + soc + " on cursed m1n1 hypervisor"
    assert soc2 in (plain, cursed)
    return "PINNED_SETUP_ADT_PLAIN" if soc2 == plain else "PINNED_SETUP_ADT_CURSED_SPRR"


def hv_setup_templates():
    """The literal prefix tuple iterated in the pinned HV setup_adt()."""
    tree = ast.parse(_pinned(HV_SOURCE, HV_SOURCE_SHA256))
    setup = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == "setup_adt")
    loop = next(n for n in ast.walk(setup) if isinstance(n, ast.For) and
                isinstance(n.target, ast.Name) and n.target.id == "prefix")
    return tuple(ast.literal_eval(loop.iter))


def deleted_usb0():
    return {t % 0 for t in hv_setup_templates()}


def projection(original, prepared):
    """Replay pinned edits on original rows; return (map, removed) or raise."""
    deleted = deleted_usb0()
    by_path = {}
    for s in original.segments:
        by_path.setdefault(s.path, []).append(s)
    expected = {}
    removed = []
    for path, rows in by_path.items():
        if any(path == d or path.startswith(d + "/") for d in deleted):
            removed += [(r.ordinal, "USB0_SELECTED_SUBTREE") for r in rows]
            continue
        parts = path.split("/")
        visited = len(parts) in (3, 4) and parts[1] == "arm-io"   # /arm-io children and nubs
        labels = [r.name for r in rows]
        if visited and "__OS_LOG" in labels:
            index = labels.index("__OS_LOG")
            removed.append((rows[index].ordinal, "FIRST_OS_LOG"))
            rows = rows[:index] + rows[index + 1:]
        expected[path] = rows
    got = {}
    for s in prepared.segments:
        got.setdefault(s.path, []).append(s)
    assert sorted(got) == sorted(p for p, rows in expected.items() if rows), "path sets differ"
    mapping = []
    for path in sorted(got):
        want = expected[path]
        assert [(s.name, s.raw) for s in got[path]] == [(s.name, s.raw) for s in want], path
        mapping += [s.ordinal for s in want]
    return mapping, sorted(removed)


def _f(raw):
    return struct.pack("<Q", len(raw)) + raw


def _props(properties):
    out = struct.pack("<I", len(properties))
    for p in properties:
        out += _f(p.path.encode()) + _f(p.key.encode()) + _f(p.serialized)
    return out


def receipt(original, prepared, mapping, removed, counts, context, original_bootargs, mcc_raw, rewrite):
    def counts_of(inv):
        return dict(properties=len(inv.properties), pmap=len(inv.pmap), segments=len(inv.segments),
                    physical=len(inv.physical), numerical_remap=len(inv.remap),
                    zero_numerical_remap=sum(r.span.base == 0 for r in inv.remap), vram=1)

    def rows(inv):
        out = b"BCM_SOURCE_ROWS_V2\0" + struct.pack("<I", len(inv.pmap))
        for r in inv.pmap:
            out += _f(r.path.encode()) + struct.pack("<I", r.slot) + _f(r.raw)
        out += struct.pack("<I", len(inv.segments))
        for s in inv.segments:
            out += _f(s.path.encode()) + struct.pack("<II", s.slot, s.ordinal) + _f(s.name.encode()) + _f(s.raw)
        return hashlib.sha256(out).hexdigest()

    def origin(inv):
        return dict(adt_sha256=inv.base_adt_sha256, adt_bytes=inv.base_adt_bytes, counts=counts_of(inv),
                    properties_sha256=hashlib.sha256(b"BCM_SOURCE_PROPERTIES_V2\0" + _props(inv.properties)).hexdigest(),
                    rows_sha256=rows(inv))

    deleted = deleted_usb0()
    removed_props = [i for i, p in enumerate(original.properties)
                     if any(p.path == d or p.path.startswith(d + "/") for d in deleted)]
    removed_paths = {original.segments[o].path for o, why in removed if why == "FIRST_OS_LOG"}
    rewritten = [i for i, p in enumerate(prepared.properties)
                 if p.path in removed_paths and p.key in ("segment-names", "segment-ranges")]
    total = sum(counts)
    value = {
        "schema_version": 2, "profile": PROFILE,
        "status": "EXACT_PROJECTED_PREPARED_SUBSET_EMITTING_ALL_ORIGINAL_FACTS",
        "selection": {"source_iodev": 4, "selector": "PINNED_HV_IODEV_USB0_ONLY",
                      "hv_source_sha256": HV_SOURCE_SHA256},
        "context": {"nonce": context.nonce.hex(), "kernel_sha256": context.kernel_sha256.hex(),
                    "ramdisk_sha256": context.ramdisk_sha256.hex(),
                    "original_bootargs_sha256": hashlib.sha256(original_bootargs).hexdigest(),
                    "mcc_raw_sha256": hashlib.sha256(mcc_raw).hexdigest()},
        "original": origin(original), "prepared": origin(prepared),
        "projection": {"hv_product_rewrite": rewrite, "removed_original_properties": removed_props,
                       "removed_original_segments": [{"ordinal": o, "reason": r} for o, r in removed],
                       "rewritten_prepared_properties": rewritten},
        "occurrence_map": {"pmap": list(range(len(prepared.pmap))), "segments": mapping},
        "emitted": {"origin": "ORIGINAL_ALL_ROWS_UNCHANGED_ORDER_AND_MULTIPLICITY", "counts": list(counts),
                    "records": total, "facts_bytes": 1280 + 32 * total},
        "caps": {"records": 256, "physical_union": 8, "receipt_bytes": 65536, "mcc_slots": 4},
        "global_exclusion_coverage": False, "guest_independent_origin_visibility": False,
    }
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def class_digest(tag, receipt_bytes, original, prepared, keys, mapping, emitted):
    """Spec framing; keys selects relevant properties, emitted are wire records."""
    def chosen(inv):
        return [p for p in inv.properties if keys(p)]
    card = {2: lambda inv: len(inv.pmap), 3: lambda inv: 1,
            4: lambda inv: len(inv.physical), 5: lambda inv: len(inv.remap)}[tag]
    out = b"BCM_DUAL_ORIGIN_CLASS_V2\0" + struct.pack("<I", tag) + _f(PROFILE.encode())
    out += _f(hashlib.sha256(receipt_bytes).digest())
    for role, inv in ((b"ORIGINAL", original), (b"PREPARED", prepared)):
        out += _f(role) + _f(bytes.fromhex(inv.base_adt_sha256))
        out += struct.pack("<QI", inv.base_adt_bytes, card(inv)) + _props(chosen(inv))
    out += struct.pack("<I", len(mapping)) + b"".join(struct.pack("<I", t) for t in mapping)
    out += struct.pack("<I", len(emitted))
    for r in emitted:
        out += struct.pack("<IIIIQQ", r.tag, r.ordinal, r.flags, r.auxiliary, r.span.base, r.span.bytes)
    return hashlib.sha256(out).digest()


KEYS = {
    2: lambda p: (p.path, p.key) == ("/defaults", "pmap-io-ranges"),
    3: lambda p: (p.path, p.key) in {("/vram", "reg"), ("/vram", "device_type"),
                                     ("", "#address-cells"), ("", "#size-cells")},
    4: lambda p: p.key in ("segment-names", "segment-ranges"),
    5: lambda p: p.key in ("segment-names", "segment-ranges"),
}


def expected_labels(wire_records, receipt_bytes, original, prepared, mapping):
    """Class 2..5 labels for the decoded wire records."""
    labels = {}
    for tag in (2, 3, 4, 5):
        emitted = [r for r in wire_records if r.tag == tag]
        maps = {2: list(range(len(prepared.pmap))), 3: [], 4: mapping, 5: mapping}[tag]
        labels[tag] = class_digest(tag, receipt_bytes, original, prepared, KEYS[tag], maps, emitted)
    return labels
