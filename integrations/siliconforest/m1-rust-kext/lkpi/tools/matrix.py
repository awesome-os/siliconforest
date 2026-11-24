#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Compile-only status of kernel-crate modules on top of the core tier.

  matrix.py <targets.json> <out dir> -- <rustc argv for the kernel crate, without --cfg sf_mod_*>

targets.json: {"core": [...], "gated": [all gate names], "targets": [{"name", "modules": [gate
names], "cfgs": [extra --cfg]}]}. For each target the crate is compiled with core + its
modules. When rustc reports an unresolved path into a module that is switched off, that
module is switched on and the compile repeated: the result is the smallest set that resolves,
and the errors that remain. Errors are attributed to modules by file.

A target "compiles" only when rustc finishes with no error and writes the rlib. Otherwise the
table lists what rustc could not find in `bindings` (C API absent from LinuxKPI/drm-kmod, or
present only as a macro/inline), struct fields that differ, and other errors. rustc stops
after name resolution when that fails, so type-level differences behind missing names are
not visible until the names exist: "blocked", never "almost".
"""
import collections
import json
import os
import re
import subprocess
import sys

cut = sys.argv.index("--")
spec = json.load(open(sys.argv[1]))
outdir = sys.argv[2]
rustc = sys.argv[cut + 1:]
core, gated = spec["core"], set(spec["gated"])
os.makedirs(outdir + "/errors", exist_ok=True)

ERR = re.compile(r"^(?P<file>[^:\s]+\.rs):(?P<line>\d+):\d+: error(\[E\d+\])?: (?P<msg>.*)$")


def file_module(path):
    """kernel/drm/sched.rs -> drm_sched ; kernel/sync/lock.rs -> sync"""
    rel = path.split("/kernel/", 1)[-1]
    parts = rel[:-3].split("/")
    if parts[:2] == ["time", "hrtimer"]:
        return "hrtimer"
    if parts[0] == "drm" and len(parts) > 1 and parts[1] != "mod":
        return "drm_" + parts[1]
    if parts[:2] == ["soc", "apple"] and len(parts) > 2 and parts[2] != "mod":
        return "soc_apple_" + parts[2]
    return parts[0]


def compile_with(modules, cfgs, emit):
    cmd = list(rustc)
    for m in modules:
        cmd += ["--cfg", "sf_mod_" + m]
    for c in cfgs:
        cmd += ["--cfg", c]
    cmd += ["--out-dir", emit]
    p = subprocess.run(cmd, capture_output=True, text=True)
    errs = []
    for l in p.stderr.splitlines():
        m = ERR.match(l)
        if m:
            errs.append((m.group("file"), int(m.group("line")), m.group("msg")))
    return p.returncode, errs, p.stderr


def wanted(errs, on):
    """gated-off modules that unresolved paths point into"""
    add = set()
    for f, _, msg in errs:
        if not ("unresolved import" in msg or "could not find" in msg or "failed to resolve" in msg
                or re.search(r"no `\w+` in ", msg) or "cannot find" in msg and "in the crate root" in msg):
            continue
        ids = re.findall(r"`([A-Za-z0-9_:{}, ]+)`", msg)
        here = file_module(f)
        for chunk in ids:
            for path in re.split(r"[,{} ]+", chunk):
                segs = [s for s in path.split("::") if s and s not in ("crate", "super", "self", "kernel")]
                for i, s in enumerate(segs):
                    cands = [s]
                    if "drm" in segs or here.startswith("drm") or " `drm`" in msg:
                        cands.append("drm_" + s)
                    if "apple" in segs or here.startswith("soc"):
                        cands.append("soc_apple_" + s)
                    for c in cands:
                        if c in gated and c not in on:
                            add.add(c)
    return add


def summarize(errs):
    names = collections.defaultdict(set)
    fields, other = set(), collections.Counter()
    for _, _, msg in errs:
        m = re.match(r"cannot find (function|type|value|struct, variant or union type|function, tuple struct or tuple variant|macro) `(\w+)` in (crate|module) `[\w:]*(bindings|uapi)`", msg)
        if m:
            kind = {"function, tuple struct or tuple variant": "function", "struct, variant or union type": "type"}.get(m.group(1), m.group(1))
            names[kind].add(m.group(2))
            continue
        m = re.match(r"unresolved imports? `(?:crate::)?(?:bindings|uapi)::(\w+)`", msg)
        if m:
            names["type"].add(m.group(1))
            continue
        m = re.match(r"(?:no field `(\w+)` on type `&?(?:\w+::)*(\w+)`|struct `(?:\w+::)*(\w+)` has no field named `(\w+)`)", msg)
        if m:
            fields.add(f"{m.group(2) or m.group(3)}.{m.group(1) or m.group(4)}")
            continue
        other[re.sub(r"`[^`]*`", "`..`", msg.split(":")[0])[:70]] += 1
    return names, fields, other


rows = []
for t in spec["targets"]:
    on = list(dict.fromkeys(core + t["modules"]))
    emit = f"{outdir}/rlib-{t['name']}"
    os.makedirs(emit, exist_ok=True)
    pulled = []
    for _ in range(12):
        rc, errs, raw = compile_with(on, t.get("cfgs", []), emit)
        more = wanted(errs, set(on))
        if not more:
            break
        pulled += sorted(more)
        on += sorted(more)
    open(f"{outdir}/errors/{t['name']}.txt", "w").write(raw)
    built = rc == 0 and any(f.endswith(".rlib") for f in os.listdir(emit))
    for f in os.listdir(emit):
        os.unlink(f"{emit}/{f}")
    os.rmdir(emit)
    by_mod = collections.defaultdict(list)
    for e in errs:
        by_mod[file_module(e[0])].append(e)
    own = [e for m in t["modules"] for e in by_mod.get(m, [])]
    deps = {m: v for m, v in by_mod.items() if m not in t["modules"]}
    names, fields, other = summarize(own)
    rows.append({
        "name": t["name"], "built": built, "pulled": pulled, "errors": len(errs), "own_errors": len(own),
        "missing": {k: sorted(v) for k, v in names.items()}, "fields": sorted(fields),
        "other": other.most_common(6),
        "dep_errors": {m: len(v) for m, v in sorted(deps.items())},
        "dep_missing": {m: sorted(set().union(*summarize(v)[0].values())) for m, v in sorted(deps.items()) if summarize(v)[0]},
    })
    print(f"{t['name']:<22} {'compiles' if built else 'blocked':<9} errors={len(errs):<4} own={len(own):<4} pulled={','.join(pulled)}", flush=True)

json.dump(rows, open(outdir + "/matrix.json", "w"), indent=1)
with open(outdir + "/matrix.md", "w") as f:
    f.write("| module | status | also needs (switched on to resolve) | blocked by |\n|---|---|---|---|\n")
    for r in rows:
        why = []
        for kind in ("function", "type", "value", "macro"):
            if r["missing"].get(kind):
                why.append(f"{len(r['missing'][kind])} {kind}s missing in bindings: " + " ".join(r["missing"][kind]))
        if r["fields"]:
            why.append("fields: " + " ".join(r["fields"]))
        if r["other"]:
            why.append("other: " + "; ".join(f"{m} x{n}" for m, n in r["other"]))
        if r["dep_errors"]:
            why.append("in dependencies: " + ", ".join(f"{m} ({n})" for m, n in r["dep_errors"].items()))
        status = "compiles" if r["built"] else f"blocked ({r['own_errors']} own / {r['errors']} errors)"
        f.write(f"| {r['name']} | {status} | {' '.join(r['pulled'])} | {'<br>'.join(why)} |\n")
