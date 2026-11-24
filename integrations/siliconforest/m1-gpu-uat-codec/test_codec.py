#!/usr/bin/env python3
"""Offline host checks; references are read as bytes, never imported/executed."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PIN = "121fcbf35d4501814b58ca5347926a4173b6a512"
REF = "proxyclient/m1n1/hw/uat.py"
REF_HASH = "4a4d0300cd3a4ed4fb3de250866993b0d1e00ec3249f93077a871e69c94f0838"

# These deliberately break independent safety properties. Compilation failure
# does not count as detection; each mutant must run and fail a host assertion.
MUTATIONS = {
    "truncate_pa": ("p < UAT_PA_LIMIT &&", "1 &&"),
    "accept_noncanonical_va": ("if (!canonical(va)) return UAT_ADDRESS;", "if (0 && !canonical(va)) return UAT_ADDRESS;"),
    "range_crosses_ttbr_half": ("size > HALF_SIZE - (va & (HALF_SIZE - 1))", "size > HALF_SIZE"),
    "accept_invalid_nonzero": ("if (!(raw & 1)) return UAT_DESCRIPTOR;", "if (!(raw & 1)) { *out = d; return UAT_OK; }"),
    "accept_block_as_table": ("if (!(raw & 2)) return UAT_UNSUPPORTED;", "if (0 && !(raw & 2)) return UAT_UNSUPPORTED;"),
    "accept_unknown_bit5": ("if (raw & ~(ADDRESS_MASK | UAT_PAGE_FLAGS | UINT64_C(3)))", "if (raw & ~(ADDRESS_MASK | UAT_PAGE_FLAGS | UINT64_C(35)))"),
    "accept_table_flags": ("if (level < 3 && (raw & UAT_PAGE_FLAGS))", "if (level < 0u && (raw & UAT_PAGE_FLAGS))"),
    "accept_partial_page": ("UAT_PAGE_SIZE <= r[i].size - (p - r[i].start)", "UINT64_C(1) <= r[i].size - (p - r[i].start)"),
    "accept_shared_table": ("index == s->table_count || seen[index]", "index == s->table_count"),
    "accept_unreachable_node": ("if (sum.tables != s->table_count) return UAT_GRAPH;", "if (sum.tables > s->table_count) return UAT_GRAPH;"),
    "accept_table_data_overlap": ("if (overlap(s->table_ranges[i], s->data_ranges[j]))", "if (0 && overlap(s->table_ranges[i], s->data_ranges[j]))"),
    "wrong_leaf_index": ("indices[2] = parts.l3;", "indices[2] = 0;"),
}


def run(command, **kwargs):
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=90, **kwargs)


def require_success(result, label):
    if result.returncode:
        raise RuntimeError(f"{label}: {result.returncode}\n{result.stdout}\n{result.stderr}")


def main():
    ref_repo = ROOT / "sources/m1n1-baseline"
    # Compare raw git bytes, avoiding newline normalization.
    pinned = subprocess.check_output(["git", "-C", str(ref_repo), "show", f"{PIN}:{REF}"])
    actual = (ref_repo / REF).read_bytes()
    if pinned != actual or hashlib.sha256(actual).hexdigest() != REF_HASH:
        raise RuntimeError("Pinned UAT bytes do not match the inspected reference")
    results = {"scope": "synthetic host bytes only", "reference_matches_pin": True, "builds": [], "mutations": []}
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-I", str(HERE)]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    with tempfile.TemporaryDirectory(prefix="uat-codec-") as tmp:
        tmp = Path(tmp)
        for name, cc, options in [
            ("clang-asan-ubsan", "clang", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]),
            ("cc-ndebug", "cc", ["-O2", "-DNDEBUG"]),
        ]:
            binary = tmp / name
            require_success(run([cc, *flags, *options, str(HERE/"uat_codec.c"), str(HERE/"test_codec.c"), "-o", str(binary)]), name+" compile")
            executed = run([str(binary)], env=env)
            require_success(executed, name+" execute")
            results["builds"].append({"name": name, "stdout": executed.stdout.strip(), "stderr": executed.stderr.strip()})
        target_object = tmp / "uat_codec-aarch64.o"
        require_success(run(["clang", *flags, "--target=aarch64-none-elf", "-ffreestanding", "-O2",
                             "-c", str(HERE/"uat_codec.c"), "-o", str(target_object)]), "AArch64 compile only")
        results["builds"].append({"name": "aarch64-freestanding-compile-only", "executed": False,
                                  "sha256": hashlib.sha256(target_object.read_bytes()).hexdigest()})
        original = (HERE / "uat_codec.c").read_text()
        for name, (old, new) in MUTATIONS.items():
            if old not in original:
                raise RuntimeError(f"Mutation location missing: {name}")
            source, binary = tmp / (name+".c"), tmp / name
            source.write_text(original.replace(old, new))
            # Permit deliberate unsigned comparison warning in the table-flags mutant.
            compile_flags = [f for f in flags if f != "-Werror"]
            built = run(["clang", *compile_flags, "-O1", str(source), str(HERE/"test_codec.c"), "-o", str(binary)])
            require_success(built, name+" mutant compile")
            executed = run([str(binary)], env=env)
            if executed.returncode != 1 or "test_codec.c:" not in executed.stderr:
                raise RuntimeError(f"Mutant did not fail a test assertion: {name}: {executed.returncode}\n{executed.stderr}")
            results["mutations"].append({"name": name, "detected": True, "assertion": executed.stderr.strip()})
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
