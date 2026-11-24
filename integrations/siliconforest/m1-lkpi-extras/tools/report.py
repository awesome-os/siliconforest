#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Before/after table for the kernel-crate modules of this ticket.

  report.py <targets.json> <before matrix.json> <after matrix.json>

The two matrix.json files are what ../m1-rust-kext/lkpi/tools/matrix.py writes: one for that
directory as it is, one with this directory's additions. A module "compiles" only when rustc
wrote the rlib. "own" errors are in the module's own files; the rest are in modules it pulled
in (named). rustc stops after name resolution when that fails, so an error count is a lower
bound, never a distance to "compiles".
"""
import json
import sys

spec = json.load(open(sys.argv[1]))
before = {r["name"]: r for r in json.load(open(sys.argv[2]))}
after = {r["name"]: r for r in json.load(open(sys.argv[3]))}


def status(r):
    if r is None:
        return "not run"
    if r["built"]:
        return "compiles"
    return f"errors: {r['own_errors']} own, {r['errors'] - r['own_errors']} elsewhere"


def blockers(r):
    if r is None or r["built"]:
        return ""
    parts = []
    for kind in ("function", "type", "value", "macro"):
        names = r["missing"].get(kind)
        if names:
            shown = " ".join(names[:6]) + (f" (+{len(names) - 6})" if len(names) > 6 else "")
            parts.append(f"{len(names)} {kind}s missing in bindings: {shown}")
    if r["fields"]:
        parts.append("fields: " + " ".join(r["fields"][:5]) + (f" (+{len(r['fields']) - 5})" if len(r["fields"]) > 5 else ""))
    if r["other"]:
        parts.append("other: " + "; ".join(f"{m} x{n}" for m, n in r["other"][:3]))
    if r["dep_errors"]:
        parts.append("blocked by: " + ", ".join(f"{m} ({n})" for m, n in r["dep_errors"].items()))
    return "<br>".join(parts)


print("| module | G13 path uses it | before | after | pulled in | what is left |")
print("|---|---|---|---|---|---|")
for t in spec["targets"]:
    b, a = before.get(t["name"]), after.get(t["name"])
    print(f"| {t['name']} | {'yes' if t['used'] else 'no'} | {status(b)} | {status(a)} | "
          f"{' '.join(a['pulled']) if a else ''} | {blockers(a)} |")
n_used = [t for t in spec["targets"] if t["used"]]
ok = [t for t in n_used if after.get(t["name"], {}).get("built")]
print()
print(f"Used by the G13 path: {len(ok)} of {len(n_used)} targets compile with the additions "
      f"({sum(1 for t in n_used if before.get(t['name'], {}).get('built'))} before).")
