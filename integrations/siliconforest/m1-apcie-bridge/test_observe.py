#!/usr/bin/env python3
"""Host-only checks for the read-only apcie,t8103 port-0 bridge skeleton.

1. Compiles and runs test_observe.c with ASan/UBSan.
2. Static source checks: no write path in the core or the IOKit adapter.
3. Optional --adt <saved guest ADT>: the frozen m1-dart-survey board profile and
   the port-0 binding must both accept this J293's saved device tree. Reuses the
   frozen survey's ADT parser and board check unchanged. Saved bytes only.
No hardware, transport or kernel collection is involved.
"""
import argparse
import ctypes as ct
import hashlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SURVEY = HERE.parent / "m1-dart-survey"
FORBIDDEN = [
    r"\bwrite32\b", r"\bset32\b", r"\bclear32\b", r"\bmask32\b", r"\bpoll32\b",
    r"\bIOSleep\b", r"\bIODelay\b", r"\bIOTimerEventSource\b", r"\bregisterInterrupt\b",
    r"\bpmgr_", r"\bpcie_init\b", r"kIODirectionOut", r"kIODirectionInOut",
    r"\bIOMapper\b", r"\bIOPCIDevice\b", r"\bregisterService\s*\(",
]


def run(args, **kw):
    r = subprocess.run(args, text=True, capture_output=True, **kw)
    if r.returncode:
        raise RuntimeError(" ".join(map(str, args)) + "\n" + r.stdout + r.stderr)
    return r


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def source_checks():
    core = strip_comments((HERE / "observe.c").read_text())
    cpp = strip_comments((HERE / "PDApcieT8103Bridge.cpp").read_text())
    for name, text in (("observe.c", core), ("PDApcieT8103Bridge.cpp", cpp)):
        for pat in FORBIDDEN:
            assert not re.search(pat, text), f"{name}: forbidden {pat}"
        # Every volatile pointer is const and is only dereferenced on the right.
        assert not re.search(r"volatile\s+uint32_t", text), f"{name}: non-const volatile"
        assert not re.search(r"(?m)(^|[;{}])\s*\*\s*reg\s*=", text), f"{name}: store through reg"
    assert "io->read32(" in core and "write" not in core
    assert 'PE_parse_boot_argn("pdapciebridge"' in cpp
    assert "OSCompareAndSwap(0, 1, &gBridgeClaimed)" in cpp and cpp.count("gBridgeClaimed =") == 1
    assert "kIODirectionIn)" in cpp
    assert "kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly" in cpp
    assert "volatile const uint32_t *reg" in cpp and "*out = *reg;" in cpp
    assert "apo_read_allowed(r, off)" in cpp
    assert "r != DLS_PMGR && r != DLS_RC && r != DLS_PHY && r != DLS_PORT" in cpp
    assert "return KERN_FAILURE" in cpp                      # unload refused
    plist = (HERE / "Info.plist").read_text()
    assert "<string>IOResources</string>" in plist and "IOPCI" not in plist
    return {"forbidden_patterns": len(FORBIDDEN), "result": "PASS"}


def c_tests(cc, tmp):
    exe = tmp / "t"
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-O1", "-g",
         f"-I{HERE}", f"-I{SURVEY}", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         str(HERE / "observe.c"), str(HERE / "test_observe.c"), "-o", str(exe)])
    out = run([str(exe)]).stdout.strip()
    assert out.startswith("HOST ONLY PASS"), out
    return out


def saved_adt(cc, tmp, path):
    spec = importlib.util.spec_from_file_location("frozen_survey_test", SURVEY / "test_survey.py")
    survey = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(survey)
    frozen = survey.captured_board(cc, tmp, path)     # frozen board.c profile; asserts PASS
    nodes = survey.parse_adt(path.read_bytes())
    lib_path = tmp / "observe.so"
    run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", f"-I{HERE}", f"-I{SURVEY}",
         str(HERE / "observe.c"), "-o", str(lib_path)])
    lib = ct.CDLL(str(lib_path))
    pbyte = ct.POINTER(ct.c_uint8)
    callback = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_char_p, ct.c_char_p,
                           ct.POINTER(pbyte), ct.POINTER(ct.c_size_t))
    keep = []

    @callback
    def get(_ctx, path_raw, key_raw, out, size):
        value = nodes.get(path_raw.decode(), {}).get(key_raw.decode())
        if value is None:
            return -1
        buf = (ct.c_uint8 * len(value)).from_buffer_copy(value)
        keep.append(buf)
        out[0] = ct.cast(buf, pbyte)
        size[0] = len(value)
        return 0

    lib.apo_validate_bridge.argtypes = [callback, ct.c_void_p]
    lib.apo_validate_bridge.restype = ct.c_int
    rc = lib.apo_validate_bridge(get, None)
    assert rc == 0, f"apo_validate_bridge returned {rc}"
    return {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "frozen_board_profile": frozen["board_guard"],
            "port0_bridge_binding": "PASS",
            "scope": "saved bytes only; node names and compatible strings checked, no values reproduced"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adt", type=Path, help="saved guest ADT (private, read-only)")
    args = ap.parse_args()
    cc = shutil.which("cc") or shutil.which("clang")
    if not cc:
        print("no host C compiler", file=sys.stderr)
        return 2
    result = {"source_checks": source_checks()}
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        result["c_tests"] = c_tests(cc, tmp)
        if args.adt:
            result["saved_ADT"] = saved_adt(cc, tmp, args.adt)
    print(json.dumps(result, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
