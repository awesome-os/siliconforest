"""Compile deliberate producer faults; targeted tests must assert, not error."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = (HERE / "producer.py").read_text()
ORIGINAL = "test_original_profile_envelopes_and_video_fields"
ROUNDTRIP = "test_roundtrip_actual_sources_and_all_identities"
MEASURE = "test_measure_refuses_bad_sources_and_full_record_capacity"
DIGEST = "test_source_digest_is_recomputed_and_domain_separated"
MUTANTS = {
    "HV_source_profile_ignored": ("test_bounds_context_and_plan_refusals", (("context.hv_source_sha256 == HV_SHA256", "True"),)),
    "original_profile_ignored": (ORIGINAL, (("type(profile) is str and profile == ORIGINAL_PROFILE", "True"),)),
    "producer_profile_ignored": (ORIGINAL, (("type(profile) is str and profile == PROFILE", "True"),)),
    "original_envelope_outside_DRAM": (MEASURE, (("_contains(dram, old_envelope)", "True"),)),
    "new_phys_below_original": (ORIGINAL, (("old.base <= envelope.base", "True"),)),
    "original_high_not_preserved": (ORIGINAL, (("envelope.base + envelope.bytes == old.base + old.bytes", "True"),)),
    "video_source_drift_ignored": (ORIGINAL, (('native["video"][key] == getattr(video, key)', "True"),)),
    "visible_not_inside_named_VRAM": (MEASURE, (("_contains(vram, video.visible)", "True"),)),
    "source_depth_profile_ignored": (MEASURE, (("depth & 0xff == 30", "True"),)),
    "row_stride_does_not_fit_width": (MEASURE, (("stride >= width * 4", "True"),)),
    "misaligned_framebuffer_source_accepted": (MEASURE, (("base % 4 == 0", "True"),)),
    "VRAM_outside_RAM_profile_accepted": (MEASURE, (("_contains(dram, vram)", "True"),)),
    "source_hash_domain_conflated": (DIGEST, (("_properties_digest(5, segment_props)", "_properties_digest(4, segment_props)"),)),
    "original_identity_hashes_only_prefix": (ROUNDTRIP, (("hashlib.sha256(original_bootargs).digest()", "hashlib.sha256(original_bootargs[:88]).digest()"),)),
    "native_identity_hashes_only_prefix": (ROUNDTRIP, (("hashlib.sha256(original_bootargs).digest(), hashlib.sha256(native_bootargs).digest()", "hashlib.sha256(original_bootargs).digest(), hashlib.sha256(native_bootargs[:40]).digest()"),)),
    "selected_kernel_label_wrong": (ROUNDTRIP, (("identity = facts.Expected(context.nonce, (context.kernel_sha256, context.ramdisk_sha256,", "identity = facts.Expected(context.nonce, (context.ramdisk_sha256, context.ramdisk_sha256,"),)),
    "pmap_attributes_lost": (ROUNDTRIP, (("record.wimg, record.signature,", "0, 0,"),)),
    "VRAM_allocation_replaced_by_footprint": (ROUNDTRIP, (("records.append(facts.Record(3, 0, 0, 0, vram))", "records.append(facts.Record(3, 0, 0, 0, video.visible))"),)),
    "zero_remaps_coherently_dropped": ("test_zero_remap_duplicate_rows_and_opaque_IOVA_survive", (
        ("len(inventory.remap), 1)", "sum(r.span.base != 0 for r in inventory.remap), 1)"),
        ("for i, record in enumerate(inventory.remap))", "for i, record in enumerate(r for r in inventory.remap if r.span.base != 0))"))),
    "duplicate_physical_rows_coherently_dropped": ("test_zero_remap_duplicate_rows_and_opaque_IOVA_survive", (
        ("len(inventory.physical),", "len({(r.span.base, r.span.bytes) for r in inventory.physical}),"),
        ("for i, record in enumerate(inventory.physical))", "for i, record in enumerate({(r.span.base, r.span.bytes): r for r in inventory.physical}.values()))"))),
    "boot_prefix_rounded_inward": (ORIGINAL, (("boot_end = (top + PAGE - 1) & ~(PAGE - 1)", "boot_end = top & ~(PAGE - 1)"),)),
    "MCC_hashes_only_old_compact_length": (DIGEST, (("hashlib.sha256(b\"BCM_MCC_V1\\0\" + mcc_raw).digest()", "hashlib.sha256(b\"BCM_MCC_V1\\0\" + mcc_raw[:80]).digest()"),)),
    "RAM_widened_to_all_DRAM": (ROUNDTRIP, (("retained, (envelope,), sources,", "retained, (dram,), sources,"),)),
    "boot_prefix_shifted_past_start": (ROUNDTRIP, (("retained = _span(envelope.base, boot_end - envelope.base, PAGE)", "retained = _span(envelope.base + PAGE, boot_end - envelope.base - PAGE, PAGE)"),)),
    "source_dependency_hash_ignored": ("test_dependency_binding_on_import_and_before_source_use", (("hashlib.sha256(raw).hexdigest() == digest", "True"),)),
}


def main():
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    prepared = []
    for name, (test, edits) in MUTANTS.items():
        changed = SOURCE
        for old, new in edits:
            if changed.count(old) != 1:
                raise RuntimeError("mutation site count: " + name)
            changed = changed.replace(old, new)
        compile(changed, "producer.py", "exec")
        prepared.append((name, test, changed))
    baseline = subprocess.run([sys.executable, "-B", "test_producer.py"], cwd=HERE,
                              env=env, capture_output=True, text=True)
    if baseline.returncode:
        raise RuntimeError("baseline failed: " + baseline.stderr)
    results = []
    with tempfile.TemporaryDirectory(prefix="native-producer-mutants-") as directory:
        bridge = Path(directory)
        leaf = bridge / "native_policy_producer"
        leaf.mkdir()
        shutil.copyfile(HERE / "test_producer.py", leaf / "test_producer.py")
        dependencies = ("adt_source_inventory/inventory.py", "native_policy_handoff_002/handoff.py",
                        "native_policy_snapshot/facts_wire.py", "native_bootargs/native_bootargs.py")
        for relative in dependencies:
            target = bridge / relative
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(HERE.parent / relative, target)
        for name, test, changed in prepared:
            (leaf / "producer.py").write_text(changed)
            result = subprocess.run([sys.executable, "-B", "-m", "unittest",
                                     "test_producer.ProducerTests." + test], cwd=leaf,
                                    env=env, capture_output=True, text=True)
            text = result.stdout + result.stderr
            killed = result.returncode != 0 and "AssertionError" in text and "FAILED (failures=" in text and "ERROR:" not in text
            results.append(dict(name=name, compiled=True, assertion_killed=killed, targeted_test=test))
            if not killed:
                raise RuntimeError(name + " not assertion-killed: " + text)
    print(json.dumps(dict(status="PASS", baseline_groups=8,
                          compiled_assertion_killed=len(results), mutants=results), sort_keys=True))


if __name__ == "__main__":
    main()
