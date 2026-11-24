#!/usr/bin/env python3
"""Host-only checks for the port-0 bring-up engine (bringup.c). No hardware.

1. test_bringup.c in both modes under ASan/UBSan: with -DAPB_FAKE_REGISTER_FILE
   against the simulated register file, and without it (the bundle's mode).
2. Gate proofs: a -DKERNEL -DAPB_FAKE_REGISTER_FILE compile must fail; in the
   preprocessed kernel-mode source the shared-write branch has no writer call.
3. Static checks: no IOKit, no allocation, no mutable globals in bringup.c.
4. Optional --adt <saved guest ADT>: every real J293 tunable property parses
   under the strict parser into its window, and the link speed resolves.
   Counts only; no tunable values are printed.
"""
import argparse
import ctypes as ct
import importlib.util
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SURVEY = HERE.parent / "m1-dart-survey"
REGION = {"PMGR": 0, "RC": 1, "PHY": 2, "PHYIP": 3, "AXI": 4, "FUSE": 5, "PORT0": 6, "ECAM0": 7}
ADT_SETS = [
    ("/arm-io/apcie", "apcie-axi2af-tunables", "AXI"),
    ("/arm-io/apcie", "apcie-common-tunables", "RC"),
    ("/arm-io/apcie", "apcie-phy-tunables", "PHY"),
    ("/arm-io/apcie", "apcie-phy-ip-pll-tunables", "PHYIP"),
    ("/arm-io/apcie", "apcie-phy-ip-auspma-tunables", "PHYIP"),
    ("/arm-io/apcie/pci-bridge0", "apcie-config-tunables", "PORT0"),
    ("/arm-io/apcie/pci-bridge0", "pcie-rc-tunables", "ECAM0"),
    ("/arm-io/apcie/pci-bridge0", "pcie-rc-gen3-shadow-tunables", "ECAM0"),
    ("/arm-io/apcie/pci-bridge0", "pcie-rc-gen4-shadow-tunables", "ECAM0"),
]


def run(args, check=True):
    r = subprocess.run(args, text=True, capture_output=True)
    if check and r.returncode:
        raise RuntimeError(" ".join(map(str, args)) + "\n" + r.stdout + r.stderr)
    return r


def c_tests(cc, tmp):
    out = {}
    for mode, extra in (("fake", ["-DAPB_FAKE_REGISTER_FILE"]), ("gated", [])):
        exe = tmp / f"t_{mode}"
        run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g", *extra, f"-I{HERE}",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             str(HERE / "bringup.c"), str(HERE / "test_bringup.c"), "-o", str(exe)])
        text = run([str(exe)]).stdout.strip()
        assert text.startswith("HOST ONLY PASS"), text
        out[mode] = text
    return out


def wr_body(preprocessed):
    start = preprocessed.index("static int wr(")
    end = preprocessed.index("static int mask32(", start)
    return preprocessed[start:end]


def gate_proofs(cc, tmp):
    bad = run([cc, "-std=c11", "-DKERNEL", "-DAPB_FAKE_REGISTER_FILE", f"-I{HERE}", "-c",
               str(HERE / "bringup.c"), "-o", str(tmp / "k.o")], check=False)
    assert bad.returncode != 0 and "host-test only" in bad.stderr, "kernel+fake compile was not refused"
    kern = run([cc, "-std=c11", "-DKERNEL", f"-I{HERE}", "-E", "-P", str(HERE / "bringup.c")]).stdout
    fake = run([cc, "-std=c11", "-DAPB_FAKE_REGISTER_FILE", f"-I{HERE}", "-E", "-P", str(HERE / "bringup.c")]).stdout
    kb, fb = wr_body(kern), wr_body(fake)
    shared_k = kb[kb.index("apb_region_shared(region)"):kb.index("if (!c->io->write32)")]
    shared_f = fb[fb.index("apb_region_shared(region)"):fb.index("if (!c->io->write32) { r->gated_writes")]
    assert "write32" not in shared_k and "gated_writes++" in shared_k and "return APB_GATED" in shared_k
    assert "write32(" in shared_f
    assert "return 1;" not in kern[kern.index("int apb_shared_writes_compiled"):kern.index("static int fixed_write_ok")]
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-DKERNEL", f"-I{HERE}", "-c",
         str(HERE / "bringup.c"), "-o", str(tmp / "kernel_mode.o")])
    return {"kernel_plus_fake_refused": True, "kernel_shared_branch_has_no_writer": True,
            "kernel_mode_compiles": True}


def static_checks():
    src = (HERE / "bringup.c").read_text()
    code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    assert "#include <IOKit" not in code and "malloc" not in code and "IOMalloc" not in code
    for line in code.splitlines():
        if line.startswith("static ") and "=" in line and "(" not in line.split("=")[0]:
            assert " const " in line or line.startswith("static const"), f"mutable global: {line}"
    assert code.count("c->io->write32(") == 2       # fake-only shared path + port path
    assert code.count("c->io->write16(") == 1 and "CAP_LNKCTL2" in code
    return {"no_iokit_no_alloc_no_mutable_globals": True}


def adt_check(cc, tmp, path):
    spec = importlib.util.spec_from_file_location("frozen_survey_test", SURVEY / "test_survey.py")
    survey = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(survey)
    nodes = survey.parse_adt(path.read_bytes())
    lib_path = tmp / "bringup.so"
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", f"-I{HERE}",
         str(HERE / "bringup.c"), "-o", str(lib_path)])
    lib = ct.CDLL(str(lib_path))

    class Tunable(ct.Structure):
        _fields_ = [("offset", ct.c_uint32), ("mask", ct.c_uint32), ("value", ct.c_uint32)]

    lib.apb_parse_tunables.argtypes = [ct.c_char_p, ct.c_size_t, ct.c_uint, ct.POINTER(Tunable),
                                       ct.c_uint32, ct.POINTER(ct.c_uint32)]
    lib.apb_parse_tunables.restype = ct.c_int
    lib.apb_resolve_link_speed.argtypes = [ct.c_uint32] * 3 + [ct.POINTER(ct.c_uint32)]
    lib.apb_resolve_link_speed.restype = ct.c_int
    counts = {}
    for node, prop, region in ADT_SETS:
        raw = nodes[node][prop]
        out = (Tunable * 256)()
        n = ct.c_uint32()
        rc = lib.apb_parse_tunables(raw, len(raw), REGION[region], out, 256, ct.byref(n))
        assert rc == 0, f"{prop}: parse rc {rc}"
        counts[prop] = {"window": region, "records": n.value}
    bridge = nodes["/arm-io/apcie/pci-bridge0"]
    child = nodes.get("/arm-io/apcie/pci-bridge0/wlan", {})
    get = lambda d, k: struct.unpack_from("<I", d[k])[0] if k in d and len(d[k]) == 4 else 0
    speed = ct.c_uint32()
    assert lib.apb_resolve_link_speed(get(bridge, "maximum-link-speed"), get(child, "target-link-speed"),
                                      get(child, "expected-link-speed"), ct.byref(speed)) == 0
    return {"tunable_sets": counts, "resolved_link_speed": speed.value,
            "scope": "saved bytes only; counts and windows, no values"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adt", type=Path)
    args = ap.parse_args()
    cc = shutil.which("cc") or shutil.which("clang")
    if not cc:
        print("no host C compiler", file=sys.stderr)
        return 2
    result = {"static": static_checks()}
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        result["c_tests"] = c_tests(cc, tmp)
        result["gate"] = gate_proofs(cc, tmp)
        if args.adt:
            result["saved_ADT"] = adt_check(cc, tmp, args.adt)
    print(json.dumps(result, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
