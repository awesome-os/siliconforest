"""Compiled Python guard-omission mutants must die through test assertions."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = (HERE / "handoff.py").read_text()
MUTANTS = {
    "base_ADT_binding_ignored": ('hashlib.sha256(base_adt).digest() == expected.identities[3]', 'True'),
    "native_wire_binding_ignored": ('hashlib.sha256(native_bootargs).digest() == expected.identities[5]', 'True'),
    "allocation_capacity_ignored": ('final_size <= allocation.bytes, "ADT allocation insufficient; no resize"', 'True, "ADT allocation insufficient; no resize"'),
    "ADT_past_actual_top": ('allocation.base + allocation.bytes <= values["top_of_kernel_data"]', 'True'),
    "BA_past_actual_top": ('ba_base + ba_bytes <= values["top_of_kernel_data"]', 'True'),
    "BA_ADT_alias_accepted": ('not _intersects(ba_base, ba_bytes, allocation)', 'True'),
    "physical_exclusion_ignored": ('if record.tag in (1, 2, 3, 4):', 'if False:'),
    "memory_map_descriptor_ignored": ('not dt.template and dt.value == struct.pack("<QQ", allocation.base, allocation.bytes)', 'True'),
    "chosen_DRAM_ignored": ('struct.unpack("<Q", prop.value)[0] == span', 'True'),
    "native_memsize_ignored": ('values["mem_size"] == f.envelope.bytes', 'True'),
    "retained_prefix_base_ignored": ('f.retained_boot.base == values["phys_base"]', 'True'),
    "actual_top_beyond_reserved_prefix": ('values["phys_base"] < values["top_of_kernel_data"] <= f.retained_boot.base + f.retained_boot.bytes', 'values["phys_base"] < values["top_of_kernel_data"]'),
    "native_ADT_size_ignored": ('values["devtree_size"] == final_size', 'True'),
    "native_ADT_address_ignored": ('values["devtree"] == ((allocation.base - values["phys_base"] + values["virt_base"]) & U64_MASK)', 'True'),
    "trailing_ADT_bytes_accepted": ('end == len(raw), "ADT trailing bytes"', 'True, "ADT trailing bytes"'),
    "duplicate_property_accepted": ('key not in keys, "duplicate property"', 'True, "duplicate property"'),
    "duplicate_node_accepted": ('path not in nodes, "duplicate node path"', 'True, "duplicate node path"'),
    "depth_limit_ignored": ('depth <= DEPTH_LIMIT and len(nodes) < NODE_LIMIT', 'len(nodes) < NODE_LIMIT'),
    "node_limit_ignored": ('depth <= DEPTH_LIMIT and len(nodes) < NODE_LIMIT', 'depth <= DEPTH_LIMIT'),
    "unterminated_property_key_accepted": ('b"\\0" in key_field, "property key must be NUL terminated"', 'True, "property key must be NUL terminated"'),
    "full_length_high_bit_masked": ('size = size_word\n', 'size = size_word & 0x7fffffff\n'),
    "additional_property_cap_ignored": ('nodes["/chosen"].property_count < 4096, "no room for additional property"', 'True, "no room for additional property"'),
    "dependency_import_hash_ignored": ('hashlib.sha256(raw).hexdigest() == DEPENDENCIES[relative]', 'True'),
    "dependency_late_drift_ignored": ('hashlib.sha256(raw).hexdigest() == digest', 'True'),
}


def main():
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    baseline = subprocess.run([sys.executable, "-B", "test_handoff.py"], cwd=HERE,
                              env=env, capture_output=True, text=True)
    if baseline.returncode:
        raise RuntimeError("baseline failed: " + baseline.stderr)
    results = []
    with tempfile.TemporaryDirectory(prefix="native-handoff-mutants-") as directory:
        bridge = Path(directory)
        leaf = bridge / "native_policy_handoff"
        leaf.mkdir()
        shutil.copyfile(HERE / "test_handoff.py", leaf / "test_handoff.py")
        for relative in ("native_policy_snapshot/facts_wire.py", "native_bootargs/native_bootargs.py"):
            destination = bridge / relative
            destination.parent.mkdir(exist_ok=True)
            shutil.copyfile(HERE.parent / relative, destination)
        for name, (old, new) in MUTANTS.items():
            if SOURCE.count(old) != 1:
                raise RuntimeError("mutation site count: " + name)
            changed = SOURCE.replace(old, new)
            compile(changed, "handoff.py", "exec")
            (leaf / "handoff.py").write_text(changed)
            result = subprocess.run([sys.executable, "-B", "test_handoff.py"], cwd=leaf,
                                    env=env, capture_output=True, text=True)
            text = result.stdout + result.stderr
            assertion_killed = result.returncode != 0 and "AssertionError" in text and "FAILED (failures=" in text and "ERROR:" not in text
            results.append(dict(name=name, compiled=True, assertion_killed=assertion_killed))
            if not assertion_killed:
                raise RuntimeError(name + " not assertion-killed: " + text)
    print(json.dumps(dict(status="PASS", groups=7, compiled_assertion_killed=len(results), mutants=results), sort_keys=True))


if __name__ == "__main__":
    main()
