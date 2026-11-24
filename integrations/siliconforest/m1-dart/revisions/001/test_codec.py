#!/usr/bin/env python3
"""Compile/run original host tests only; never import or call a device backend."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
MUTATIONS = {
    "leaf_reserved_bits": ("|| (word & ~LEAF_ALLOWED)", "|| 0"),
    "ttbr_reserved_bits": ("(word & ~TTBR_ALLOWED)", "0"),
    "iova_truncation": ("if (iova >= DLD_IOVA_LIMIT)", "if (0)"),
    "read_permission": ("((word & NO_READ) ? 0 : DLD_READ)", "DLD_READ"),
    "msi_page": ("if (overlaps(r, (struct dld_range){DLD_MSI_PAGE, DLD_PAGE}))", "if (0)"),
    "physical_alias": ("if (scratch->leaf_pa[j] == leaf.pa)", "if (0)"),
    "high_root_entry": ("if (l1 >= 128)", "if (0)"),
    "orphan_table": ("if (!scratch->references[i])", "if (0)"),
    "stream_width": ("b[i].sid >= 16", "b[i].sid >= 32"),
}


def run(mutations=False):
    # Assertion mutants must not leave cores. ASan/UBSan remain active; this
    # environment's ptracer restriction makes LeakSanitizer unsuitable.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    cc = os.environ.get("CC", "cc")
    version = subprocess.run([cc, "--version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O1", "-g",
             "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
    original = (HERE / "dart_codec.c").read_text()
    tested = []
    with tempfile.TemporaryDirectory(prefix=".host-check-", dir=HERE) as tmp:
        tmp = Path(tmp)
        (tmp / "dart_codec.h").write_bytes((HERE / "dart_codec.h").read_bytes())
        (tmp / "test.c").write_bytes((HERE / "test_dart_codec.c").read_bytes())
        variants = [("baseline", original)]
        if mutations:
            for name, (before, after) in MUTATIONS.items():
                assert original.count(before) == 1, name
                variants.append((name, original.replace(before, after)))
        for name, source in variants:
            (tmp / "codec.c").write_text(source)
            compile_result = subprocess.run([cc, *flags, str(tmp / "codec.c"), str(tmp / "test.c"),
                                             "-o", str(tmp / "check")], capture_output=True, text=True)
            if compile_result.returncode:
                raise RuntimeError(f"{name}: compile failed (not a killed mutant):\n{compile_result.stderr}")
            result = subprocess.run([str(tmp / "check")], capture_output=True, text=True, env=env, timeout=30)
            if name == "baseline":
                if result.returncode:
                    raise RuntimeError(result.stdout + result.stderr)
                print(result.stdout.strip())
            else:
                if result.returncode == 0 or "Assertion" not in result.stderr:
                    raise RuntimeError(f"{name}: did not fail its semantic assertion: {result.returncode}\n{result.stderr}")
                tested.append(name)
    sources = {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
               for name in ("dart_codec.h", "dart_codec.c", "test_dart_codec.c", "test_codec.py")}
    return dict(status="HOST_SOFTWARE_CODEC_CHECKS_PASS", compiler=version, flags=flags,
                test_groups=11, seeded_roundtrips=4096, compiling_mutants_rejected=tested,
                source_sha256=sources, hardware_accessed=False, kernel_compiled=False,
                mmio=False, dma=False, initialized_DART=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutations", action="store_true")
    args = parser.parse_args()
    print(json.dumps(run(args.mutations), sort_keys=True))
