#!/usr/bin/env python3
"""Host-only checks for the port-0 link stage (linkup.c, slice 3). No hardware.

1. test_linkup.c in fake and gated (bundle-equivalent) modes under ASan/UBSan.
2. Gate proofs: -DKERNEL -DAPB_FAKE_REGISTER_FILE is refused; in the
   preprocessed kernel-mode source neither the shared-write branch of wr()
   nor pin() calls anything.
3. Static checks on linkup.c: no IOKit, no allocation, no mutable globals.
4. Optional --linux-ref <pcie-apple.c>: provenance check. No macro or function
   name defined there, and no run of six or more words from its comments,
   appears in the slice-3 files. Prints counts only, never its text.
5. Optional --adt <saved guest ADT>: builds the slice-3 plan from the J293 ADT
   (PERST# function, timings, MSI doorbell and vectors, absence of a power
   enable) and checks it with apl_plan_ok.
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

sys.dont_write_bytecode = True   # imported helpers must not leave caches in frozen leaves

HERE = Path(__file__).resolve().parent
SURVEY = HERE.parent / "m1-dart-survey"
SLICE3 = ["linkup.c", "linkup.h", "test_linkup.c", "test_linkup.py", "check_linkup_bundle.py",
          "LINKUP.md", "NOTICE-linkup", "CMakeLists-linkup.txt", "build-linkup-gate.nix"]


def run(args, check=True):
    r = subprocess.run(args, text=True, capture_output=True)
    if check and r.returncode:
        raise RuntimeError(" ".join(map(str, args)) + "\n" + r.stdout + r.stderr)
    return r


def c_tests(cc, tmp):
    out = {}
    for mode, extra in (("fake", ["-DAPB_FAKE_REGISTER_FILE"]), ("gated", [])):
        exe = tmp / f"l_{mode}"
        run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g", *extra, f"-I{HERE}",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
             str(HERE / "bringup.c"), str(HERE / "linkup.c"), str(HERE / "test_linkup.c"), "-o", str(exe)])
        text = run([str(exe)]).stdout.strip()
        assert text.startswith("HOST ONLY PASS"), text
        out[mode] = text
    return out


def body(src, start, end):
    i = src.index(start)
    return src[i:src.index(end, i)]


def gate_proofs(cc, tmp, here=None):
    here = here or HERE
    bad = run([cc, "-std=c11", "-DKERNEL", "-DAPB_FAKE_REGISTER_FILE", f"-I{here}", "-c",
               str(here / "linkup.c"), "-o", str(tmp / "k.o")], check=False)
    assert bad.returncode != 0 and "host-test only" in bad.stderr, "kernel+fake compile was not refused"
    pre = lambda *d: run([cc, "-std=c11", *d, f"-I{here}", "-E", "-P", str(here / "linkup.c")]).stdout
    kern, fake = pre("-DKERNEL"), pre("-DAPB_FAKE_REGISTER_FILE")
    kw, fw = body(kern, "static int wr(", "static int pin("), body(fake, "static int wr(", "static int pin(")
    shared_k = body(kw, "apl_region_shared(region)", "if (!c->io->write32) { r->gated_writes")
    shared_f = body(fw, "apl_region_shared(region)", "if (!c->io->write32) { r->gated_writes")
    assert "write32" not in shared_k and "gated_writes++" in shared_k and "return APB_GATED" in shared_k
    assert "write32(" in shared_f
    kp, fp = body(kern, "static int pin(", "static int set_bits("), body(fake, "static int pin(", "static int set_bits(")
    assert "perst" not in kp and "power" not in kp and "fn(" not in kp and "return APB_GATED" in kp
    assert "fn(c->pins->context" in fp
    assert "return 1;" not in body(kern, "int apl_shared_writes_compiled", "static int sid_slot")
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-DKERNEL", f"-I{here}", "-c",
         str(here / "linkup.c"), "-o", str(tmp / "kernel_mode.o")])
    return {"kernel_plus_fake_refused": True, "kernel_shared_branch_has_no_writer": True,
            "kernel_pin_has_no_call": True, "kernel_mode_compiles": True}


def static_checks():
    code = re.sub(r"/\*.*?\*/", "", (HERE / "linkup.c").read_text(), flags=re.S)
    assert "#include <IOKit" not in code and "malloc" not in code and "IOMalloc" not in code
    for line in code.splitlines():
        if line.startswith("static ") and "=" in line and "(" not in line.split("=")[0]:
            assert line.startswith("static const"), f"mutable global: {line}"
    assert code.count("c->io->write32(") == 2       # fake-only lane path + port path
    assert "write16" not in code and "read16" not in code
    return {"no_iokit_no_alloc_no_mutable_globals": True}


def provenance(ref):
    text = ref.read_text(errors="replace")
    names = set(re.findall(r"^\s*#\s*define\s+([A-Za-z_]\w*)", text, re.M))
    names |= {n for n in re.findall(r"^(?:static\s+)?(?:inline\s+)?[\w\s\*]+?\b([a-z_]\w*)\s*\(", text, re.M)
              if "_" in n}
    keywords = {"return", "sizeof", "switch", "default", "static", "unsigned", "struct", "const"}
    names = {n for n in names if len(n) >= 6 and n not in keywords}
    comments = " ".join(re.findall(r"/\*.*?\*/|//[^\n]*", text, re.S))
    words = re.findall(r"[a-z0-9]+", comments.lower())
    grams = {" ".join(words[i:i + 6]) for i in range(len(words) - 5)}
    hits_names, hits_grams = {}, 0
    for f in SLICE3:
        p = HERE / f
        if not p.exists():
            continue
        ours = p.read_text()
        tokens = set(re.findall(r"[A-Za-z_]\w*", ours))
        common = sorted(tokens & names)
        if common:
            hits_names[f] = common
        ow = re.findall(r"[a-z0-9]+", ours.lower())
        hits_grams += sum(" ".join(ow[i:i + 6]) in grams for i in range(len(ow) - 5))
    assert not hits_names, f"Linux identifiers in slice-3 files: {hits_names}"
    assert hits_grams == 0, f"{hits_grams} six-word runs from Linux comments"
    return {"linux_identifiers_checked": len(names), "comment_6grams_checked": len(grams),
            "identifier_hits": 0, "comment_6gram_hits": 0}


def adt_plan(cc, tmp, path):
    spec = importlib.util.spec_from_file_location("frozen_survey_test", SURVEY / "test_survey.py")
    survey = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(survey)
    nodes = survey.parse_adt(path.read_bytes())
    apcie, bridge = nodes["/arm-io/apcie"], nodes["/arm-io/apcie/pci-bridge0"]
    wlan = nodes.get("/arm-io/apcie/pci-bridge0/wlan", {})
    u32 = lambda d, k: struct.unpack_from("<I", d[k])[0]
    phandles = {struct.unpack_from("<I", n["AAPL,phandle"])[0]: p for p, n in nodes.items()
                if len(n.get("AAPL,phandle", b"")) == 4}
    perst = struct.unpack_from("<4I", bridge["function-perst"])
    assert perst[1] == 0x4750494f and phandles.get(perst[0]) == "/arm-io/gpio"
    power = [k for d in (bridge, wlan) for k in d if k.startswith("function-") and
             ("pwr" in k or "power" in k or "enable" in k)]
    doorbell = struct.unpack_from("<2I", apcie["msi-address"])
    assert doorbell[1] == 0
    plan = {"refclk_to_perst_us": u32(bridge, "t-refclk-to-perst"),
            "perst_to_config_ms": u32(bridge, "perst-to-config"),
            "power_enable": 1 if power else 0, "power_on_ms": 0,
            "msi_doorbell": doorbell[0], "msi_vectors": u32(bridge, "#msi-vectors"),
            "msi_vector_base": u32(bridge, "msi-vector-base"), "link_timeout_ms": 500}
    assert plan["msi_vector_base"] == 0

    lib_path = tmp / "linkup.so"
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", f"-I{HERE}",
         str(HERE / "bringup.c"), str(HERE / "linkup.c"), "-o", str(lib_path)])
    lib = ct.CDLL(str(lib_path))

    class Sid(ct.Structure):
        _fields_ = [("rid", ct.c_uint16), ("sid", ct.c_uint16)]

    class Plan(ct.Structure):
        _fields_ = [(n, ct.c_uint32) for n in ("refclk_to_perst_us", "perst_to_config_ms", "power_enable",
                                               "power_on_ms", "msi_doorbell", "msi_vectors",
                                               "link_timeout_ms", "sid_count")] + [("sids", Sid * 8)]

    lib.apl_plan_ok.argtypes = [ct.POINTER(Plan)]
    lib.apl_plan_ok.restype = ct.c_int
    p = Plan(*(plan[k] for k in ("refclk_to_perst_us", "perst_to_config_ms", "power_enable", "power_on_ms",
                                 "msi_doorbell", "msi_vectors", "link_timeout_ms")), 0)
    assert lib.apl_plan_ok(ct.byref(p)) == 1
    return {"plan_from_ADT": plan, "perst": {"controller": "/arm-io/gpio", "pin": perst[2], "flags": perst[3]},
            "power_enable_functions_found": power, "apl_plan_ok": True,
            "not_from_ADT": "link_timeout_ms 500 (Linux default); sids left to the DART mapper"}


# Each mutant is a small breaking edit to a copy of linkup.c. The named check must fail:
# "fake" or "gated" = that C test mode; "proof" = the preprocessor gate proofs.
MUTANTS = [
    ("pin gate removed", "gated", "#ifdef APB_FAKE_REGISTER_FILE\n    {\n        int (*fn)", "#if 1\n    {\n        int (*fn)"),
    ("pin gate removed (proof)", "proof", "#ifdef APB_FAKE_REGISTER_FILE\n    {\n        int (*fn)", "#if 1\n    {\n        int (*fn)"),
    ("lane gate removed", "proof",
     "    if (apl_region_shared(region)) {\n#ifdef APB_FAKE_REGISTER_FILE", "    if (apl_region_shared(region)) {\n#if 1"),
    ("slice-2 precondition skipped", "gated", "    if (!bringup_complete(bringup)) { e = APL_STATE; goto done; }\n", ""),
    ("lane opened before PERST#", "fake",
     "    STEP(APL_STEP_PERST_ASSERT);        CHECK(pin(&c, 0, 1)); DONE();\n",
     "    CHECK(set_bits(&c, APL_P0PHY, LANE_CTL, LANE_CTL_CONFIG));\n    STEP(APL_STEP_PERST_ASSERT);        CHECK(pin(&c, 0, 1)); DONE();\n"),
    ("refclk-to-PERST wait dropped", "fake", "    wait_us(&c, settle);\n", ""),
    ("PERST-to-config wait dropped", "fake", "wait_us(&c, p->perst_to_config_ms * 1000u);", "(void)0;"),
    ("pin released before port PERST off", "fake",
     "    CHECK(set_bits(&c, APB_PORT0, P_PERST, P_PERST_RELEASED));\n    CHECK(pin(&c, 0, 0));\n",
     "    CHECK(pin(&c, 0, 0));\n    CHECK(set_bits(&c, APB_PORT0, P_PERST, P_PERST_RELEASED));\n"),
    ("lane config access left open", "fake",
     "CHECK(clear_bits(&c, APL_P0PHY, LANE_CTL, LANE_CTL_CONFIG));", "(void)0;"),
    ("clock gating step dropped", "fake",
     "    CHECK(clear_bits(&c, APB_PORT0, P_APP_CLOCK, P_APP_CLOCK_NOGATE));\n", ""),
    ("interrupt status not cleared", "fake",
     "    CHECK(wr(&c, APB_PORT0, P_INT_STATUS, 0xffffffffu));\n", ""),
    ("MSI log2 off by one", "fake", "(log2_exact(p->msi_vectors) << P_MSI_LOG2_SHIFT)",
     "((log2_exact(p->msi_vectors) + 1u) << P_MSI_LOG2_SHIFT)"),
    ("table probe not cleared", "fake", "        if ((e = wr(c, APB_PORT0, off, 0))) return e;\n        if ((e = rd(c, APB_PORT0, off, &v))) return e;\n        if (v) return APL_STATE;\n", ""),
    ("table size unchecked", "fake", "    if (p->sid_count > c->rec->rid2sid_entries) return APL_STATE;\n", ""),
    ("link wait unbounded by plan", "fake", "p->link_timeout_ms * 1000u)", "4000000u)"),
    ("already-up link accepted", "fake", "    if (v & P_LINK_UP) { e = APL_STATE; goto done; }\n", ""),
    ("LTSSM started before table map", "fake",
     "    STEP(APL_STEP_RID2SID_MAP);         CHECK(sid_map(&c, p)); DONE();\n    STEP(APL_STEP_LINK_TRAINING);       CHECK(wr(&c, APB_PORT0, P_LTSSM_CTL, P_LTSSM_START)); DONE();\n",
     "    STEP(APL_STEP_LINK_TRAINING);       CHECK(wr(&c, APB_PORT0, P_LTSSM_CTL, P_LTSSM_START)); DONE();\n    STEP(APL_STEP_RID2SID_MAP);         CHECK(sid_map(&c, p)); DONE();\n"),
]


def mutants(cc, tmp):
    src = (HERE / "linkup.c").read_text()
    killed = []
    for name, mode, old, new in MUTANTS:
        assert src.count(old) == 1, f"mutant anchor not unique: {name}"
        d = tmp / f"m{len(killed)}"
        d.mkdir()
        for f in ("bringup.c", "bringup.h", "linkup.h", "test_linkup.c"):
            (d / f).write_bytes((HERE / f).read_bytes())
        (d / "linkup.c").write_text(src.replace(old, new))
        if mode == "proof":
            try:
                gate_proofs(cc, d, d)
                failed = False
            except (AssertionError, RuntimeError, ValueError):
                failed = True
            assert failed, f"mutant survived: {name}"
            killed.append(name)
            continue
        extra = ["-DAPB_FAKE_REGISTER_FILE"] if mode == "fake" else []
        b = run([cc, "-std=c11", "-O1", "-g", *extra, f"-I{d}", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", str(d / "bringup.c"), str(d / "linkup.c"), str(d / "test_linkup.c"),
                 "-o", str(d / "t")], check=False)
        failed = b.returncode != 0 or run([str(d / "t")], check=False).returncode != 0
        assert failed, f"mutant survived: {name}"
        killed.append(name)
    return {"killed": len(killed), "of": len(MUTANTS)}


def provenance_detects(ref, tmp):
    """The provenance check must flag a planted identifier and a planted comment run."""
    global HERE
    text = ref.read_text(errors="replace")
    ident = sorted(set(re.findall(r"^\s*#\s*define\s+([A-Z_][A-Z0-9_]{8,})", text, re.M)))[0]
    words = re.findall(r"[a-z0-9]+", " ".join(re.findall(r"/\*.*?\*/", text, re.S)).lower())
    run6 = " ".join(words[100:106])
    saved, flagged = HERE, []
    try:
        for plant in (f"int {ident};\n", f"/* {run6} */\n"):
            d = tmp / f"p{len(flagged)}"
            d.mkdir()
            for f in SLICE3:
                if (saved / f).exists():
                    (d / f).write_bytes((saved / f).read_bytes())
            (d / "linkup.c").write_text((saved / "linkup.c").read_text() + plant)
            HERE = d
            try:
                provenance(ref)
            except AssertionError:
                flagged.append(True)
    finally:
        HERE = saved
    assert len(flagged) == 2
    return {"planted_identifier_flagged": True, "planted_comment_run_flagged": True}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adt", type=Path)
    ap.add_argument("--linux-ref", type=Path)
    ap.add_argument("--mutants", action="store_true")
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
        if args.linux_ref:
            result["provenance"] = provenance(args.linux_ref)
            result["provenance_self_test"] = provenance_detects(args.linux_ref, tmp)
        if args.mutants:
            result["mutants"] = mutants(cc, tmp)
        if args.adt:
            result["saved_ADT"] = adt_plan(cc, tmp, args.adt)
    print(json.dumps(result, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
