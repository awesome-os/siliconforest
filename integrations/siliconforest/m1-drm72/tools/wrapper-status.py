#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""wrapper-status.py <matrix out dir>

Per wrapper module: did the kernel crate build with it switched on, how many of rustc's
errors are in the wrapper's own source files, and where the others are. Reads what
../m1-rust-kext/lkpi/tools/matrix.py left in the directory (matrix.json, errors/<module>.txt).

rustc resolves names and type-checks every function body before it gives up, so "0 own"
means: every C function, type, constant and struct field the wrapper names exists in the
bindings with the type the wrapper expects. It does not mean the module compiled: borrow
checking and code generation only run once the whole crate is free of errors.
"""
import collections
import json
import re
import sys

out = sys.argv[1]
OWN = {
    "dma_fence": ["dma_fence.rs"],
    "drm::gem": ["drm/gem/mod.rs"],
    "drm::gem::shmem": ["drm/gem/shmem.rs"],
    "drm::gpuvm": ["drm/gpuvm/"],
}
ERR = re.compile(r"^\S*?/kernel/(\S+?\.rs):(\d+):\d+: error(?:\[(E\d+)\])?: (.*)$")


def module_of(path):
    parts = path[:-3].split("/")
    if parts[0] == "drm":
        return "drm::" + ("gem::shmem" if parts[1:] == ["gem", "shmem"] else parts[1])
    return parts[0]


print("| wrapper | crate builds | errors in the wrapper's files | errors elsewhere (module: count) |")
print("|---|---|---|---|")
for row in json.load(open(out + "/matrix.json")):
    name = row["name"]
    own_files = OWN.get(name, ["drm/" + name.split("::")[1] + ".rs"] if name.startswith("drm::") else [name + ".rs"])
    own, other = [], collections.Counter()
    for line in open(f"{out}/errors/{name}.txt"):
        m = ERR.match(line)
        if not m:
            continue
        path = m.group(1)
        if any(path == f or (f.endswith("/") and path.startswith(f)) for f in own_files):
            own.append(f"{path}:{m.group(2)} {m.group(4).split(':')[0]}")
        else:
            other[module_of(path)] += 1
    own_s = "0" if not own else f"{len(own)}: " + "; ".join(own)
    other_s = ", ".join(f"{k}: {v}" for k, v in sorted(other.items())) or "none"
    print(f"| {name} | {'yes' if row['built'] else 'no'} | {own_s} | {other_s} |")
