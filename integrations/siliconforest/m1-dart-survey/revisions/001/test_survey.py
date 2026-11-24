#!/usr/bin/env python3
"""Host-only C checks, optional private saved-ADT validation; no transport imports."""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
MARKER = "#error DLS_TEST_REQUIRES_ASSERTIONS_NDEBUG_FORBIDDEN"


def call(args, **kw):
    return subprocess.run(args, text=True, capture_output=True, **kw)


def required(result):
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result


def source_checks():
    cpp = (HERE / "PDDARTSurvey.cpp").read_text()
    header = (HERE / "survey.h").read_text()
    assert 'PE_parse_boot_argn("pddartsurvey"' in cpp
    assert "OSCompareAndSwap(0,1,&gSurveyClaimed)" in cpp
    assert cpp.count("gSurveyClaimed=") == 1
    assert "kIODirectionIn);" in cpp
    assert "kIOMapAnywhere|kIOMapInhibitCache|kIOMapReadOnly" in cpp
    assert "volatile const uint32_t *reg" in cpp and "*out=*reg;" in cpp
    assert not re.search(r"(?m)^\s*\*reg\s*=", cpp)
    assert "dls_read_allowed(r,off)" in cpp
    assert cpp.index("dls_validate_board(property,this,&board)") < cpp.index("dls_sample(&board,&io,&snapshot)")
    assert "return KERN_FAILURE" in cpp
    assert "write32" not in header and "(*read32)" in header
    assert not re.search(r"IOMapper|IODMACommand|IOTimerEventSource|registerInterrupt|thread_call", cpp)
    info = plistlib.loads((HERE / "Info.plist").read_bytes())
    assert info["IOKitPersonalities"]["PDDARTSurvey"]["IOProviderClass"] == "IOResources"
    assert "IOMapper" not in repr(info)
    return "native source shape checked, not compiled or ABI-qualified"


def parse_adt(blob):
    """Bounded raw decoder, deliberately independent of m1n1 Python modules."""
    nodes = {}

    def parse(off, parent, depth):
        if depth > 64 or off + 8 > len(blob) or len(nodes) >= 8192:
            raise ValueError("ADT node bounds")
        np, nc = struct.unpack_from("<II", blob, off)
        off += 8
        if np > 2048 or nc > 2048:
            raise ValueError("ADT counts")
        props = {}
        for _ in range(np):
            if off + 36 > len(blob):
                raise ValueError("ADT property header")
            key = blob[off:off + 32].split(b"\0")[0].decode("ascii")
            size = struct.unpack_from("<I", blob, off + 32)[0] & 0x7fffffff
            off += 36
            end = off + ((size + 3) & ~3)
            if end > len(blob) or key in props:
                raise ValueError("ADT property bounds/duplicate")
            props[key] = blob[off:off + size]
            off = end
        name = props.get("name", b"").rstrip(b"\0").decode("ascii")
        if not name or "/" in name and depth:
            raise ValueError("ADT name")
        path = "/" if depth == 0 else parent.rstrip("/") + "/" + name
        if path in nodes:
            raise ValueError("ADT duplicate path")
        nodes[path] = props
        for _ in range(nc):
            off = parse(off, path, depth + 1)
        return off

    if parse(0, "", 0) != len(blob):
        raise ValueError("ADT trailing bytes")
    return nodes


def captured_board(cc, tmp, path):
    data = path.read_bytes()
    nodes = parse_adt(data)
    libpath = tmp / "board.so"
    required(call([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared",
                   str(HERE / "board.c"), "-o", str(libpath)]))
    lib = ct.CDLL(str(libpath))
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

    class Board(ct.Structure):
        _fields_ = [("base", ct.c_uint64 * 5), ("size", ct.c_uint32 * 5)]

    lib.dls_validate_board.argtypes = [callback, ct.c_void_p, ct.POINTER(Board)]
    lib.dls_validate_board.restype = ct.c_int
    assert lib.dls_validate_board(get, None, ct.byref(Board())) == 0
    # Pinned HV.map_essential recursively hooks these UART/USB parents. Check
    # the saved graph, not a guessed assumption that every PMGR read is real.
    raw = nodes["/arm-io/pmgr"]["devices"]
    if len(raw) % 48:
        raise ValueError("PMGR stride")
    devices = {}
    for off in range(0, len(raw), 48):
        ident = struct.unpack_from("<H", raw, off + 26)[0]
        if ident in devices:
            raise ValueError("PMGR duplicate ID")
        devices[ident] = (raw[off + 32:off + 48].split(b"\0")[0].decode(),
                          struct.unpack_from("<HH", raw, off + 4))
    for port in (0, 1):
        names = {"UART0", f"ATC{port}_USB", f"ATC{port}_USB_AON"}
        roots = [i for i, (name, _) in devices.items() if name in names]
        assert len(roots) == 3
        seen = set()

        def walk(i, visiting):
            if not i:
                return
            if i in visiting:
                raise ValueError("PMGR cycle")
            if i in seen:
                return
            seen.add(i)
            for parent in devices[i][1]:
                walk(parent, visiting | {i})

        for i in roots:
            walk(i, set())
        assert not seen.intersection({29, 32, 77, 97})
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "board_guard": "PASS", "USB0_and_USB1_HV_shadow_overlap": False,
            "scope": "saved bytes only; no live registers or transport imported"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adt", type=Path)
    ap.add_argument("--mutations", action="store_true")
    args = ap.parse_args()
    cc = os.environ.get("CC", "cc")
    env = os.environ | {"ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1",
                        "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    flags = ["-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O1", "-g",
             "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]
    result = {"status": "HOST_ONLY_PASS", "compiler": required(call([cc, "--version"])).stdout.splitlines()[0],
              "flags": flags, "MMIO": False, "hardware": False, "SDK_build": False,
              "native_source_check": source_checks()}
    with tempfile.TemporaryDirectory(prefix=".host-test-", dir=HERE) as name:
        tmp = Path(name)
        for filename in ("survey.h", "survey.c", "board.c", "test_survey.c"):
            shutil.copyfile(HERE / filename, tmp / filename)
        negative = call([cc, "-std=c11", "-DNDEBUG", "-E", str(tmp / "test_survey.c")])
        assert negative.returncode and MARKER in negative.stderr
        result["NDEBUG_guard"] = "exact diagnostic rejected"

        def compile_test():
            return call([cc, *flags, str(tmp / "survey.c"), str(tmp / "board.c"),
                         str(tmp / "test_survey.c"), "-o", str(tmp / "test")])

        required(compile_test())
        result["tests"] = required(call([str(tmp / "test")], env=env)).stdout.strip()
        result["mutants_rejected"] = []
        if args.mutations:
            mutations = [
                ("no_power_clock_guard", "survey.c", "int e=guard(io,s);if (e) return e;", "int e=DLS_SAMPLED;"),
                ("allow_pm_reset", "survey.c", "0x90001c00", "0x10001c00"),
                ("allow_cold_actual", "survey.c", "(s->power[i]&0xff)!=0xff", "(s->power[i]&0x0f)!=0x0f"),
                ("allow_phy_reset", "survey.c", "(s->clocks[i]&0x8f)", "(s->clocks[i]&0x0f)"),
                ("allow_command_read", "survey.c", "return off==0||off==4", "return off==0x20||off==0||off==4"),
                ("ignore_config_lock", "survey.c", "s->samples[2].value&(1u<<15)", "s->samples[2].value&(1u<<14)"),
                ("skip_final_guard", "survey.c", "e=guard(io,s); /*", "e=DLS_SAMPLED; /*"),
                ("wrong_parent", "board.c", '{77,32,"APCIE"', '{77,33,"APCIE"'),
            ]
            for label, filename, before, after in mutations:
                original = (HERE / filename).read_text()
                assert original.count(before) == 1
                (tmp / filename).write_text(original.replace(before, after))
                required(compile_test())
                failed = call([str(tmp / "test")], env=env)
                assert failed.returncode != 0, label
                assert "Assertion" in failed.stderr or "assertion" in failed.stderr, (label, failed.stderr)
                result["mutants_rejected"].append(label)
                (tmp / filename).write_text(original)
        if args.adt:
            result["saved_ADT"] = captured_board(cc, tmp, args.adt)
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
