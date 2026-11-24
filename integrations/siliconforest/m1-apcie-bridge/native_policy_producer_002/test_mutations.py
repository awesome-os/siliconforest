"""Compile deliberate producer002/selection faults; targeted tests must assert, not error.

Runs in a private temporary copy of the leaf, its frozen dependencies and the
pinned m1n1 sources. Redundant guards are not mutated or counted.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[3]
P = "test_producer.ProducerTests."
S = "test_source_selection.SelectionTests."
ROUND = P + "test_roundtrip_all_original_rows_and_identities"
REFUSE = P + "test_projection_refusals"
RECEIPT = P + "test_receipt_exact_schema_independent_of_native_and_capped"
LABELS = P + "test_class_labels_match_independent_oracle"
NATIVE = P + "test_native_profile_context_and_mcc_refusals"
PRODUCER = {
    "source_iodev_type_relaxed": (P + "test_source_iodev_exact_builtin_int4", (
        ("type(source_iodev) is int and source_iodev == SOURCE_IODEV", "source_iodev == SOURCE_IODEV"),)),
    "selected_template_dropped": (ROUND, (('"/arm-io/dart-usb%d", "/arm-io/atc-phy%d"', '"/arm-io/atc-phy%d"'),)),
    "selection_prefix_unanchored": (ROUND, (('path.startswith(prefix + "/")', "path.startswith(prefix)"),)),
    "selection_descendants_ignored": (ROUND, (('path == prefix or path.startswith(prefix + "/")', "path == prefix"),)),
    "oslog_last_removed": (ROUND, (("next((i for i, r in enumerate(rows) if r.name == OS_LOG), None)",
                                    "next((i for i, r in reversed(list(enumerate(rows))) if r.name == OS_LOG), None)"),)),
    "oslog_depth5_visited": (ROUND, (("len(parts) in (3, 4)", "len(parts) in (3, 4, 5)"),)),
    "oslog_nubs_not_visited": (ROUND, (("len(parts) in (3, 4)", "len(parts) in (3,)"),)),
    "oslog_outside_arm_io_visited": (ROUND, (('path.startswith("/arm-io/") and len(parts) in (3, 4)',
                                              "len(parts) in (3, 4)"),)),
    "unchanged_header_padding_ignored": (REFUSE, (("expected.append((identity, prop.value, prop.serialized))",
        "expected.append((identity, prop.value, next((q.serialized for q in prepared.properties if (q.path, q.key) == identity), None)))"),)),
    "rewrite_serialization_ignored": (REFUSE, (("expected.append((identity, value, _rebuilt(prop.key, value)))",
        "expected.append((identity, value, next((q.serialized for q in prepared.properties if (q.path, q.key) == identity), None)))"),)),
    "prepared_rows_emitted": (ROUND, (("emitted = _emission(original)", "emitted = _emission(prepared)"),
                                      ("pmap, physical, remap = _emission(original)", "pmap, physical, remap = _emission(prepared)"))),
    "rows_outside_DRAM_dropped": (ROUND, (("return original.pmap, original.physical, original.remap",
        "return original.pmap, tuple(r for r in original.physical if r.span.base >= facts.DRAM_BASE), original.remap"),)),
    "zero_remaps_dropped": (ROUND, (("return original.pmap, original.physical, original.remap",
        "return original.pmap, original.physical, tuple(r for r in original.remap if r.span.base)"),)),
    "segment_map_by_first_equal_row": (P + "test_occurrence_map_duplicates_cross_node_and_reasons", (
        ("source = rows[row.slot]",
         "source = next(r for r in original.segments if (r.path, r.name, r.raw) == (row.path, row.name, row.raw))"),)),
    "receipt_truncated": (RECEIPT, (('_require(len(raw) <= JOIN_RECEIPT_MAX, "source join receipt exceeds 65536 bytes; no truncation")',
                                     "raw = raw[:JOIN_RECEIPT_MAX]"),)),
    "receipt_cap_off_by_one": (RECEIPT, (("len(raw) <= JOIN_RECEIPT_MAX", "len(raw) < JOIN_RECEIPT_MAX"),)),
    "receipt_removed_rows_unrecorded": (RECEIPT, (("for o, r in projection.removed_segments]", "for o, r in ()]"),)),
    "label_roles_collapsed": (LABELS, (('(b"PREPARED", prepared, prepared_props)', '(b"ORIGINAL", prepared, prepared_props)'),)),
    "label_map_omitted": (LABELS, (("    for target in mapping:", "    for target in ():"),)),
    "label_receipt_binding_omitted": (LABELS, (("_frame(digest, hashlib.sha256(receipt).digest())", '_frame(digest, b"")'),)),
    "label_physical_remap_domain_conflated": (LABELS, (("_class_digest(5, receipt,", "_class_digest(4, receipt,"),)),
    "prepared_identity_replaced_by_original": (ROUND, (("context.hv_source_sha256, hashlib.sha256(base_adt).digest(),",
                                                        "context.hv_source_sha256, hashlib.sha256(original_adt).digest(),"),)),
    "product_rewrite_unchecked": (P + "test_original_must_predate_hv_setup_adt", (
        ("rewrite = _product_rewrite(original_adt, base_adt)", 'rewrite = "PINNED_SETUP_ADT_PLAIN"'),)),
    "original_marker_unchecked": (P + "test_original_must_predate_hv_setup_adt", (
        ("not any(HV_MARKER in value for value in (name, description, soc))", "True"),)),
    "product_name_rewrite_unchecked": (P + "test_prepared_product_rewrite_exact", (
        ("new_name == name + HV_SUFFIX", "True"),)),
    "product_description_rewrite_unchecked": (P + "test_prepared_product_rewrite_exact", (
        ("new_description == description + HV_SUFFIX", "True"),)),
    "product_soc_variant_ignored": (P + "test_prepared_product_rewrite_exact", (
        ("[label for label, template in HV_SOC if new_soc == template % soc]", "[HV_SOC[0][0]]"),)),
    "emptying_removal_accepted": (P + "test_selected_subtree_inside_eligible_nub_and_emptying_removal", (
        ('_require(remain, "pinned removal would empty a segment property; unsupported")', "pass"),)),
    "RAM_widened_to_DRAM": (ROUND, (("retained, (envelope,), sources,", "retained, (dram,), sources,"),)),
    "record_cap_not_enforced": (P + "test_capacity_256_257_counts_all_original_rows", (
        ("sum(counts) <= facts.MAX_RECORDS", "True"),)),
    "pmap_attributes_lost": (ROUND, (("r.wimg, r.signature,", "0, 0,"),)),
    "VRAM_allocation_replaced_by_footprint": (ROUND, (("3: [facts.Record(3, 0, 0, 0, vram)]",
                                                       "3: [facts.Record(3, 0, 0, 0, video.visible)]"),)),
    "boot_prefix_rounded_inward": (NATIVE, (("boot_end = (top + PAGE - 1) & ~(PAGE - 1)", "boot_end = top & ~(PAGE - 1)"),)),
    "HV_source_profile_ignored": (NATIVE, (("context.hv_source_sha256 == HV_SHA256", "True"),)),
    "producer_profile_ignored": (NATIVE, (("type(profile) is str and profile == PROFILE", "True"),)),
    "native_envelope_high_ignored": (NATIVE, (("envelope.base + envelope.bytes == old.base + old.bytes", "True"),)),
    "video_drift_ignored": (NATIVE, (('native["video"][key] == getattr(video, key)', "True"),)),
    "dependency_hash_ignored": (P + "test_dependency_binding_on_import_and_before_source_use", (
        ("hashlib.sha256(raw).hexdigest() == digest", "True"),)),
}
SELECTION = {
    "enum_witness_replaced_by_numeric": (S + "test_plain_bool_foreign_member_and_missing_refused", (
        ("type(witness) is source_type and witness is source_type.__members__[SELECTED_MEMBER] and",
         "isinstance(witness, int) and witness == SOURCE_IODEV and"),
        ('witness._name_ == SELECTED_MEMBER, "HV selection', 'True, "HV selection'),
        ("witness._name_, value), value", "SELECTED_MEMBER, value), value"))),
    "member_table_unchecked": (S + "test_plain_bool_foreign_member_and_missing_refused", (
        ("members == PINNED_IODEV", "True"),)),
    "class_name_unchecked": (S + "test_plain_bool_foreign_member_and_missing_refused", (
        ('source_type.__name__ == "IODEV"', "True"),)),
    "metaclass_unchecked": (S + "test_plain_bool_foreign_member_and_missing_refused", (
        ("type(source_type) is enum.EnumMeta", "isinstance(source_type, enum.EnumMeta)"),)),
    "module_binding_unchecked": (S + "test_plain_bool_foreign_member_and_missing_refused", (
        ('source_type.__module__ == getattr(proxy_module, "__name__", None)', "True"),)),
    "missing_witness_not_named": (S + "test_plain_bool_foreign_member_and_missing_refused", (
        ("witness is not _MISSING and witness is not None", "True"),)),
    "custom_int_conversion_used": (S + "test_custom_int_conversion_never_called", (
        ("value = int.__int__(witness)", "value = int(witness)"),)),
}
LEAF_FILES = ("producer.py", "source_selection.py", "fixtures.py", "oracle.py", "cdecoder.py",
              "test_producer.py", "test_source_selection.py", "test_c_decoder.py")
DEPENDENCIES = ("integrations/puredarwin/m1-apcie-bridge/adt_source_inventory/inventory.py",
                "integrations/puredarwin/m1-apcie-bridge/native_policy_handoff_002/handoff.py",
                "integrations/puredarwin/m1-apcie-bridge/native_policy_snapshot/facts_wire.py",
                "integrations/puredarwin/m1-apcie-bridge/native_policy_snapshot/policy_snapshot.c",
                "integrations/puredarwin/m1-apcie-bridge/native_policy_snapshot/policy_snapshot.h",
                "integrations/puredarwin/m1-apcie-bridge/mcc_snapshot/mcc_snapshot.c",
                "integrations/puredarwin/m1-apcie-bridge/mcc_snapshot/mcc_snapshot.h",
                "integrations/puredarwin/m1-apcie-bridge/native_bootargs/native_bootargs.py",
                "sources/m1n1-baseline/proxyclient/m1n1/hv/__init__.py",
                "sources/m1n1-baseline/proxyclient/m1n1/proxy.py",
                "sources/m1n1-baseline/proxyclient/m1n1/adt.py")


def prepare(name, source, edits):
    changed = source
    for old, new in edits:
        if changed.count(old) != 1:
            raise RuntimeError("mutation site count: " + name)
        changed = changed.replace(old, new)
    compile(changed, name, "exec")
    return changed


def main():
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    sources = {f: (HERE / f).read_text() for f in ("producer.py", "source_selection.py")}
    plan = [(name, "producer.py", test, prepare(name, sources["producer.py"], edits))
            for name, (test, edits) in PRODUCER.items()]
    plan += [(name, "source_selection.py", test, prepare(name, sources["source_selection.py"], edits))
             for name, (test, edits) in SELECTION.items()]
    results = []
    with tempfile.TemporaryDirectory(prefix="producer002-mutants-") as directory:
        root = Path(directory)
        leaf = root / HERE.relative_to(LAB)
        leaf.mkdir(parents=True)
        for name in LEAF_FILES:
            shutil.copyfile(HERE / name, leaf / name)
        for relative in DEPENDENCIES:
            (root / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(LAB / relative, root / relative)
        for module in ("test_producer", "test_source_selection", "test_c_decoder"):
            baseline = subprocess.run([sys.executable, "-B", "-m", "unittest", module], cwd=leaf,
                                      env=env, capture_output=True, text=True, timeout=600)
            if baseline.returncode:
                raise RuntimeError("baseline failed: " + module + "\n" + baseline.stderr)
        for name, target, test, changed in plan:
            (leaf / target).write_text(changed)
            result = subprocess.run([sys.executable, "-B", "-m", "unittest", test], cwd=leaf,
                                    env=env, capture_output=True, text=True, timeout=600)
            (leaf / target).write_text(sources[target])
            text = result.stdout + result.stderr
            killed = (result.returncode != 0 and "AssertionError" in text and
                      "FAILED (failures=" in text and "ERROR:" not in text)
            results.append(dict(name=name, file=target, compiled=True, assertion_killed=killed,
                                targeted_test=test.split(".", 1)[1]))
            if not killed:
                raise RuntimeError(name + " not assertion-killed:\n" + text[-3000:])
    print(json.dumps(dict(status="PASS", baseline_modules=3, compiled_assertion_killed=len(results),
                          producer_mutants=len(PRODUCER), selection_mutants=len(SELECTION),
                          error_only_kills=0, mutants=results), sort_keys=True))


if __name__ == "__main__":
    main()
