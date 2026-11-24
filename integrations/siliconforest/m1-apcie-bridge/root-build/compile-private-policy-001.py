#!/usr/bin/env python3
"""Build the frozen native private-policy OBJECT closure; never run target code."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess

TUS = (
    "native_private_policy/private_policy.c",
    "native_policy_reader/policy_reader.c",
    "native_policy_reader/policy_reader_native.cpp",
    "native_policy_snapshot/policy_snapshot.c",
    "mcc_snapshot/mcc_snapshot.c",
    "owned_control/owned_control.c",
    "owned_control/owned_control_native.cpp",
    "native_owned/owned.c",
    "native_owned/native_owned.cpp",
    "private_lifecycle/lifecycle.c",
)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def object_sections(path):
    raw = path.read_bytes()
    require(len(raw) >= 32, "short object")
    magic, cpu, _, kind, count, size, _, _ = struct.unpack_from("<8I", raw)
    require((magic, cpu, kind) == (0xfeedfacf, 0x100000c, 1), "not ARM64 MH_OBJECT")
    require(size <= len(raw) - 32, "invalid load-command extent")
    at, end, names = 32, 32 + size, []
    for _ in range(count):
        require(at <= end - 8, "short load command")
        command, length = struct.unpack_from("<II", raw, at)
        require(length >= 8 and length <= end - at, "invalid load command")
        if command == 0x19:
            require(length >= 72, "short segment")
            sections = struct.unpack_from("<I", raw, at + 64)[0]
            require(sections <= (length - 72) // 80, "short sections")
            for i in range(sections):
                start = at + 72 + i * 80
                names.append(raw[start:start + 16].split(b"\0", 1)[0])
        at += length
    require(at == end, "load commands do not cover declared extent")
    require(not {b"__mod_init_func", b"__mod_term_func"}.intersection(names),
            "unexpected core initializer/finalizer")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "output", "inputs", "toolchain", "linker", "headers", "availability"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    inputs = json.loads(args.inputs.read_text())
    out = args.output.resolve()
    out.mkdir(exist_ok=False)
    for row in inputs["production_files"]:
        relative = Path(row["name"])
        require(not relative.is_absolute() and ".." not in relative.parts, "unsafe source name")
        require(sha(source / relative) == row["sha256"], "frozen input changed: " + str(relative))
    report = {"scope": "ARM64 object library only; no target execution or kext",
              "commands": [], "objects": {}, "dependencies": {}, "frames": []}

    def run(name, argv):
        result = subprocess.run(list(map(str, argv)), capture_output=True, timeout=180)
        log = out / (name + ".log")
        log.write_bytes(result.stdout + result.stderr)
        report["commands"].append({"name": name, "argv": list(map(str, argv)),
                                   "returncode": result.returncode, "log_sha256": sha(log)})
        (out / "build.json").write_text(json.dumps(report, indent=2) + "\n")
        require(result.returncode == 0, "failed command: " + name)
        return result.stdout.decode("utf-8")

    common = ["-ffreestanding", "-fno-builtin", "-DKERNEL", "-DKERNEL_PRIVATE",
              "-DTARGET_OS_OSX", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
              "-Wframe-larger-than=1024", "-fstack-usage", "-fno-stack-protector", "-fno-common",
              "-isystem", args.headers / "Headers", "-isystem", args.headers / "PrivateHeaders",
              "-isystem", args.availability, "-O2"]
    objects = []
    for name in TUS:
        path = source / name
        require(path.is_file(), "missing TU")
        stem = path.stem
        obj, dependency = out / (stem + ".o"), out / (stem + ".d")
        cpp = path.suffix == ".cpp"
        compiler = args.toolchain / ("arm64-apple-darwin20.4-clang++" if cpp else "arm64-apple-darwin20.4-clang")
        flags = ["-std=c++17", "-fno-exceptions", "-fno-rtti", "-fapple-kext"] if cpp else ["-std=c11"]
        if name == "owned_control/owned_control_native.cpp":
            flags += ["-include", args.headers / "PrivateHeaders/mach/vm_types.h",
                      "-include", args.headers / "PrivateHeaders/arm/locks.h"]
        run(stem, [compiler, *flags, *common, "-MD", "-MF", dependency, "-c", path, "-o", obj])
        object_sections(obj)
        objects.append(obj)
        report["objects"][obj.name] = sha(obj)
        for item in shlex.split(dependency.read_text().replace("\\\n", " ").split(":", 1)[1]):
            require("test_sdk" not in item and "combined_sdk" not in item, "test headers entered build")
            report["dependencies"][item] = sha(item)
        for line in obj.with_suffix(".su").read_text().splitlines():
            function, frame, mode = line.split("\t")
            require(mode == "static" and int(frame) <= 1024, "frame guard failed")
            report["frames"].append({"function": function, "bytes": int(frame)})
    linked = out / "private_policy_provider.o"
    run("link", [args.linker, "-r", "-arch", "arm64", "-Z", "-platform_version",
                 "macos", "26.5", "26.5", "-o", linked, *objects])
    object_sections(linked)
    report["objects"][linked.name] = sha(linked)
    nm = args.toolchain / "arm64-apple-darwin20.4-nm"
    undefined = run("undefined", [nm, "-u", linked])
    imports = sorted(line.split()[-1] for line in undefined.splitlines() if line.strip())
    require(imports == sorted(inputs["expected_imports"]), "unexpected object imports")
    defined = run("defined", [nm, "-g", "--defined-only", linked])
    symbols = {line.split()[-1] for line in defined.splitlines() if line.strip()}
    require({"_npp_native_configure_once", "_npr_read_native", "_boc_native_derive_and_configure",
             "_bno_native_acquire", "_bno_native_retire", "_bpl_init"} <= symbols, "join incomplete")
    disassembly = run("atomic-disassembly", [args.toolchain / "arm64-apple-darwin20.4-objdump",
                                            "--macho", "--disassemble", out / "private_policy.o"])
    atomic = {name: bool(re.search(r"\b" + name + r"\b", disassembly))
              for name in ("ldaxr", "stlxr", "stlr", "casal")}
    require((atomic["ldaxr"] and atomic["stlxr"] or atomic["casal"]) and atomic["stlr"],
            "inline atomic instructions missing")
    report.update(status="PASS_OBJECT_LIBRARY_ONLY", imports=imports, inline_atomics=atomic,
                  max_individual_frame=max(row["bytes"] for row in report["frames"]))
    (out / "build.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
