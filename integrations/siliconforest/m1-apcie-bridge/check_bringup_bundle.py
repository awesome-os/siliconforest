#!/usr/bin/env python3
"""Binary gate checks on the build-bringup-gate.nix output. Read-only, host only.

Usage: check_bringup_bundle.py <nix result dir>

Checks, on the SDK-built kernel-mode code:
  1. apb_shared_writes_compiled is exactly `mov w0, #0x0; ret`.
  2. In wr(), the branch taken when apb_region_shared() is true increments
     gated_writes and returns 1 (APB_GATED) with no call; wr() has one blr.
  3. The object has five indirect calls: rd, delay, wr, and mask16_lnkctl2 (two).
  4. Nothing in the bundle branches to, or takes the address of, apb_bringup_port0;
     the only 8-byte words holding its address are symbol-table entries.
  5. The bundle's bringup text has the same opcode sequence as bringup.c.o.
"""
import re
import struct
import subprocess
import sys
from pathlib import Path


def objdump(*args):
    return subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", *map(str, args)],
                          text=True, capture_output=True, check=True).stdout


def nm(path):
    out = subprocess.run(["llvm-nm", str(path)], text=True, capture_output=True, check=True).stdout
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3}


def insns(text):
    return [l.split("\t", 2)[1:] for l in text.splitlines() if re.match(r"^\s+[0-9a-f]+:\s", l)]


def symtab_ranges(data):
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off, out = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x2:  # LC_SYMTAB
            symoff, nsyms = struct.unpack_from("<II", data, off + 8)
            out.append((symoff, symoff + 16 * nsyms))
        off += size
    return out


def main():
    root = Path(sys.argv[1])
    bundle = root / "System/Library/Extensions/PDApcieT8103Bridge.kext/Contents/MacOS/PDApcieT8103Bridge"
    obj = root / "objects/bringup.c.o"
    report = []

    gate = [" ".join(" ".join(i).split(";")[0].split())
            for i in insns(objdump("--disassemble-symbols=_apb_shared_writes_compiled", bundle))]
    assert gate == ["mov w0, #0x0", "ret"], gate
    report.append("apb_shared_writes_compiled: mov w0, #0x0; ret")

    wr = objdump("-r", "--disassemble-symbols=_wr", obj)
    lines = wr.splitlines()
    k = next(n for n, l in enumerate(lines) if "_apb_region_shared" in l)
    branch = "\n".join(lines[k + 1:k + 12])
    assert re.search(r"cbz\s+w0", branch) and "blr" not in branch
    assert re.search(r"add\s+w9, w9, #0x1", branch) and "[x10, #0x1c]" in branch and "mov\tw8, #0x1" in branch
    assert wr.count("blr") == 1
    report.append("wr(): shared branch = gated_writes(+0x1c)++, return 1, no call; one blr in wr (port path)")

    calls = {}
    func = None
    for l in objdump(obj).splitlines():
        m = re.match(r"^[0-9a-f]+ <(_\w+)>:", l)
        if m:
            func = m.group(1)
        elif "\tblr\t" in l:
            calls[func] = calls.get(func, 0) + 1
    assert calls == {"_rd": 1, "_delay": 1, "_wr": 1, "_mask16_lnkctl2": 2}, calls
    report.append(f"indirect calls in object: {calls}")

    syms = nm(bundle)
    entry = syms["_apb_bringup_port0"]
    full = objdump(bundle)
    assert not re.search(r"<_apb_bringup_port0>$", full, re.M)
    assert not re.search(rf"\b{entry:#x}\b", full)
    data = bundle.read_bytes()
    hits = [i for i in range(0, len(data) - 7, 8) if struct.unpack_from("<Q", data, i)[0] == entry]
    tabs = symtab_ranges(data)
    assert all(any(a <= h < b for a, b in tabs) for h in hits)
    report.append(f"apb_bringup_port0 at {entry:#x}: no branch or address reference; "
                  f"{len(hits)} address-valued words, all in LC_SYMTAB")

    osyms = nm(obj)
    base = syms["_apb_region_size"] - osyms["_apb_region_size"]
    for name in ("_apb_bringup_port0", "_apb_parse_tunables", "_apb_resolve_link_speed",
                 "_apb_shared_writes_compiled", "_apb_region_shared"):
        assert syms[name] - osyms[name] == base, name
    end = osyms["ltmp1"]
    ops_o = [i[0] for i in insns(objdump(obj))]
    ops_b = [i[0] for i in insns(objdump(f"--start-address={base:#x}", f"--stop-address={base + end:#x}", bundle))]
    assert ops_o == ops_b and len(ops_o) > 1000
    report.append(f"bundle text {base:#x}..{base + end:#x}: same {len(ops_o)}-opcode sequence as bringup.c.o")

    print("\n".join(report))
    print("BUNDLE GATE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
