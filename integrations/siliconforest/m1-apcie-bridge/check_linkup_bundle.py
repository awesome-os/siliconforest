#!/usr/bin/env python3
"""Binary gate checks on the build-linkup-gate.nix output. Read-only, host only.

Usage: check_linkup_bundle.py <nix result dir>

Runs the unchanged slice-2 checks (check_bringup_bundle.py) on the same bundle,
then checks the SDK-built kernel-mode linkup.c code:
  1. apl_shared_writes_compiled is exactly `mov w0, #0x0; ret`.
  2. pin() has no call at all; it increments gated_writes and returns 1.
  3. In wr(), the branch taken when apl_region_shared() is true increments
     gated_writes and returns 1 with no call; wr() has one blr (port path).
  4. The object has three indirect calls: rd (read32), wait_us (delay), wr.
  5. Nothing in the bundle branches to, or takes the address of,
     apl_link_port0; the only words holding its address are symbol-table entries.
  6. The bundle's linkup text has the same opcode sequence as linkup.c.o, and
     the bundle imports nothing that the slice-2 bundle did not.
"""
import importlib.util
import re
import struct
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True   # imported helpers must not leave caches in frozen leaves

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("slice2_check", HERE / "check_bringup_bundle.py")
s2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s2)
objdump, nm, insns, symtab_ranges = s2.objdump, s2.nm, s2.insns, s2.symtab_ranges


def main():
    root = Path(sys.argv[1])
    bundle = root / "System/Library/Extensions/PDApcieT8103Bridge.kext/Contents/MacOS/PDApcieT8103Bridge"
    obj = root / "objects/linkup.c.o"
    if s2.main() != 0:
        return 1
    report = []

    gate = [" ".join(" ".join(i).split(";")[0].split())
            for i in insns(objdump("--disassemble-symbols=_apl_shared_writes_compiled", bundle))]
    assert gate == ["mov w0, #0x0", "ret"], gate
    report.append("apl_shared_writes_compiled: mov w0, #0x0; ret")

    pin = objdump("-r", "--disassemble-symbols=_pin", obj)
    ops = [i[0].strip() for i in insns(pin)]
    assert not any(o in ("bl", "blr", "br") for o in ops), ops
    assert re.search(r"add\s+w9, w9, #0x1", pin) and "[x10, #0x1c]" in pin and "mov\tw8, #0x1" in pin
    report.append("pin(): no call; gated_writes(+0x1c)++, return 1")

    wr = objdump("-r", "--disassemble-symbols=_wr", obj)
    lines = wr.splitlines()
    k = next(n for n, l in enumerate(lines) if "_apl_region_shared" in l)
    branch = "\n".join(lines[k + 1:k + 12])
    assert re.search(r"cbz\s+w0", branch) and "blr" not in branch
    assert re.search(r"add\s+w9, w9, #0x1", branch) and "[x10, #0x1c]" in branch and "mov\tw8, #0x1" in branch
    assert wr.count("blr") == 1
    report.append("wr(): shared branch = gated_writes(+0x1c)++, return 1, no call; one blr (port path)")

    calls, func = {}, None
    for l in objdump(obj).splitlines():
        m = re.match(r"^[0-9a-f]+ <(_\w+)>:", l)
        if m:
            func = m.group(1)
        elif "\tblr\t" in l:
            calls[func] = calls.get(func, 0) + 1
    assert calls == {"_rd": 1, "_wait_us": 1, "_wr": 1}, calls
    report.append(f"indirect calls in linkup object: {calls}")

    syms = nm(bundle)
    entry = syms["_apl_link_port0"]
    full = objdump(bundle)
    assert not re.search(r"<_apl_link_port0>$", full, re.M)
    assert not re.search(rf"\b{entry:#x}\b", full)
    data = bundle.read_bytes()
    hits = [i for i in range(0, len(data) - 7, 8) if struct.unpack_from("<Q", data, i)[0] == entry]
    tabs = symtab_ranges(data)
    assert all(any(a <= h < b for a, b in tabs) for h in hits)
    report.append(f"apl_link_port0 at {entry:#x}: no branch or address reference; "
                  f"{len(hits)} address-valued words, all in LC_SYMTAB")

    osyms = nm(obj)
    base = syms["_apl_region_size"] - osyms["_apl_region_size"]
    for name in ("_apl_link_port0", "_apl_plan_ok", "_apl_shared_writes_compiled", "_apl_region_shared"):
        assert syms[name] - osyms[name] == base, name
    head = subprocess.run(["llvm-objdump", "-h", str(obj)], text=True, capture_output=True, check=True).stdout
    end = int(re.search(r"^\s*\d+\s+__text\s+([0-9a-f]+)", head, re.M).group(1), 16)
    ops_o = [i[0] for i in insns(objdump(obj))]
    ops_b = [i[0] for i in insns(objdump(f"--start-address={base:#x}", f"--stop-address={base + end:#x}", bundle))]
    assert ops_o == ops_b and len(ops_o) > 1000
    report.append(f"bundle text {base:#x}..{base + end:#x}: same {len(ops_o)}-opcode sequence as linkup.c.o")

    undef = lambda p: set(subprocess.run(["llvm-nm", "-u", str(p)], text=True, capture_output=True,
                                         check=True).stdout.split())
    slice2 = set((HERE / "validation/sdk-bringup-undefined.txt").read_text().split())
    extra = undef(bundle) - slice2
    assert not extra, extra
    assert undef(obj) <= {"_apb_region_shared", "_apb_region_size", "_memset"}
    report.append("bundle imports: same set as the slice-2 bundle; linkup.c.o needs only "
                  "apb_region_shared, apb_region_size, memset")

    print("\n".join(report))
    print("LINKUP BUNDLE GATE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
