"""Host-only synthetic new wire built from the actual saved source pair.

NOT a grade of the saved capture: the context labels are synthetic and the
final native BootArgs are a new synthetic wire (the saved one, re-encoded with
this producer's measured ADT size). Prints counts, hashes and status only.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import cdecoder
import oracle
import producer as p

GUEST = Path(__file__).resolve().parents[4] / "runs/m1-desktop-010/native-private-probe-attempt-001/guest"
FILES = {
    "original-adt.bin": "51de41357eab0c6ca61ec4b934f92cfb1315e53af208d2c9c5a0073d5711a7ea",
    "policy-base-adt.bin": "25397dee8acb74360055fc49a91865750b5c46ee2ce5670d37cbee1696c3305f",
    "original-bootargs.bin": "635dd91ff505f7e0ad7fa30268614c804acaef360e6a4bbea73332d48cdaac47",
    "policy-mcc-full384.bin": "582b52cf9b0080d4c568e2ff7fc839e0b74088ce1b298f6c94e63a2f64de9281",
    "guest-bootargs.bin": "80c2f2d9fcf062edb41e061108c12f11b235cf5c3ef5f84676b25f9bef0720bc",
}
TIGHT_RECORDS, TIGHT_BYTES, DESIGN_RECORDS, DESIGN_BYTES = 197, 7584, 209, 7968


def main():
    raw = {}
    for name, digest in FILES.items():
        with (GUEST / name).open("rb") as file:
            data = file.read((2 << 20) + 1)
        assert len(data) <= 2 << 20 and hashlib.sha256(data).hexdigest() == digest, name
        raw[name] = data
    original, prepared = raw["original-adt.bin"], raw["policy-base-adt.bin"]
    context = p.Context(bytes(range(0x41, 0x59)), hashlib.sha256(b"SYNTHETIC-KC").digest(),
                        hashlib.sha256(b"SYNTHETIC-RAMDISK").digest(), p.HV_SHA256)
    kw = dict(profile=p.PROFILE, original_profile=p.ORIGINAL_PROFILE, original_adt=original, source_iodev=4)
    measured = p.measure(prepared, raw["original-bootargs.bin"], raw["policy-mcc-full384.bin"], context, **kw)
    descriptor = p.handoff._parse(prepared)["/chosen/memory-map"].get("DeviceTree").value
    allocation = p.handoff.Allocation(*struct.unpack("<QQ", descriptor))
    values = p.bootargs.decode(raw["guest-bootargs.bin"], wire_profile=p.bootargs.WIRE_PROFILE)
    values["devtree_size"] = measured.final_adt_bytes
    native = p.bootargs.encode(values, wire_profile=p.bootargs.WIRE_PROFILE)
    bundle = p.produce(prepared, raw["original-bootargs.bin"], native, raw["policy-mcc-full384.bin"],
                       context, allocation, **kw)
    assert bundle.measurement == measured
    observed = p.facts.decode(bundle.wire, bundle.expected)
    f = observed.facts
    mapping, removed = oracle.projection(bundle.original_inventory, bundle.prepared_inventory)
    assert list(bundle.projection.segments) == mapping and list(bundle.projection.removed_segments) == removed
    receipt = measured.source_join_receipt
    assert receipt == oracle.receipt(bundle.original_inventory, bundle.prepared_inventory, mapping, removed,
                                     measured.source_counts, context, raw["original-bootargs.bin"],
                                     raw["policy-mcc-full384.bin"], oracle.product_rewrite(original, prepared))
    labels = oracle.expected_labels(f.records, receipt, bundle.original_inventory, bundle.prepared_inventory, mapping)
    assert all(f.sources[t - 1].declared_sha256 == labels[t] for t in (2, 3, 4, 5))
    physical = [r for r in f.records if r.tag == 4]
    assert [(r.span.base, r.span.bytes) for r in physical] == \
        [(s.span.base, s.span.bytes) for s in bundle.original_inventory.physical]
    envelope = f.envelope
    removed_below = sum(physical[o].span.base + physical[o].span.bytes <= envelope.base for o, _ in removed)
    records = len(f.records)
    assert measured.source_counts == (3, 109, 1, 41, 41, 1) and records == 196 and len(bundle.wire) == 7552
    assert records <= TIGHT_RECORDS and len(bundle.wire) <= TIGHT_BYTES
    assert (records, len(bundle.wire)) != (DESIGN_RECORDS, DESIGN_BYTES)
    assert len(observed.mcc_slots) <= 4 and len(observed.exclusion_union) <= 8 and len(receipt) <= 65536
    assert bundle.expected.identities[3] == hashlib.sha256(prepared).digest() != hashlib.sha256(original).digest()
    c = cdecoder.Decoder()
    try:
        status, out = c.decode(bundle.wire, bundle.expected)
        assert status == 0
        assert cdecoder.records(out) == [(r.tag, r.ordinal, r.flags, r.auxiliary, r.span.base, r.span.bytes)
                                         for r in f.records]
        assert cdecoder.identities(out) == bundle.expected.identities
        assert cdecoder.labels(out) == tuple(s.declared_sha256 for s in f.sources)
        assert out.union_count == len(observed.exclusion_union)
    finally:
        c.close()
    reasons = {}
    for _, reason in removed:
        reasons[reason] = reasons.get(reason, 0) + 1
    print(json.dumps(dict(
        status="HOST_ONLY_SYNTHETIC_NEW_WIRE_FROM_SAVED_SOURCE_PAIR_NOT_A_CAPTURE_GRADE",
        inputs_sha256=FILES, synthetic_context_labels=True, synthetic_native_bootargs=True,
        synthetic_native_bootargs_sha256=hashlib.sha256(native).hexdigest(),
        original_counts=p._counts(bundle.original_inventory), prepared_counts=p._counts(bundle.prepared_inventory),
        removed_segments_by_reason=reasons, removed_rows_emitted=len(removed),
        removed_rows_below_current_ram=removed_below, source_counts=list(measured.source_counts),
        records=records, facts_bytes=len(bundle.wire), mcc_enabled_slots=len(observed.mcc_slots),
        tight_ceiling=dict(records=TIGHT_RECORDS, bytes=TIGHT_BYTES),
        design_overestimate_not_produced=dict(records=DESIGN_RECORDS, bytes=DESIGN_BYTES),
        relevant_union_count=len(observed.exclusion_union), receipt_bytes=len(receipt),
        receipt_sha256=hashlib.sha256(receipt).hexdigest(), facts_sha256=hashlib.sha256(bundle.wire).hexdigest(),
        final_adt_bytes=measured.final_adt_bytes, allocation_bytes=allocation.bytes,
        prepared_identity_slot3=True, oracle_map_receipt_labels_match=True, c908e_decode=True,
        old_capture_regraded=False, hardware_or_target_write=False, raw_source_values_printed=False),
        sort_keys=True))


if __name__ == "__main__":
    main()
