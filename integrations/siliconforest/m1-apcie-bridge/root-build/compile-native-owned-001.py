#!/usr/bin/env python3
"""Compile frozen original native-owner/core sources; never execute target code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[4]
LEAF = ROOT / "integrations/puredarwin/m1-apcie-bridge"
TC = Path("/nix/store/v0x4y2cjbasgmrqsbka2bp8allw8j9ph-darwin-cross-toolchain-nix/bin")
LD = Path("/nix/store/n19v2yzd05pirxz38xjhghknchx3fg1i-puredarwin-native-ld-0.1/bin/ld")
SDK = Path("/nix/store/6zpwyx6p9swad42f0vx6rl4z0dp2kj1g-puredarwin-oss-sdk-26.5/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")
KH = Path("/nix/store/rxi8m8ki35zs9pdj2vnis26wjb1cdfcb-puredarwin-xnu-headers-arm64-0.1/System/Library/Frameworks/Kernel.framework/Versions/A")
KERNEL = Path("/nix/store/1ys1x1hzkq3v0l8bc14v2bq1jm5i46zn-puredarwin-kernel-arm64-t8103-bringup-0.1/System/Library/Kernels/kernel.debug.t8103")
PIN = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"
XNU = ROOT / "sources/puredarwin-baseline/src/Kernel/xnu"
FROZEN = {"private_lifecycle": "c585fd8b80479975a46ad19c83b7610a53f3a4a1a420241cbe0022a2d1fb82fb"}
PRODUCTION = {
    "private_lifecycle": ("lifecycle.c", "lifecycle.h"),
    "native_owned": ("owned.c", "owned.h", "owned_types.h", "native_owned.cpp", "native_owned.h", "lifecycle_bridge.h"),
}
UNITS = (("private_lifecycle", "lifecycle.c"), ("native_owned", "owned.c"), ("native_owned", "native_owned.cpp"))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def strict_pairs(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError("duplicate JSON key: " + key)
        out[key] = value
    return out


def load(path):
    return json.loads(Path(path).read_text(), object_pairs_hook=strict_pairs)


def write(path, value):
    with Path(path).open("x") as f:
        json.dump(value, f, indent=2, sort_keys=True)
        f.write("\n")


def macho(path):
    raw = path.read_bytes()
    magic, cpu, _, kind, count, size, _, _ = struct.unpack_from("<8I", raw)
    assert (magic, cpu, kind) == (0xfeedfacf, 0x100000c, 1), "ARM64 MH_OBJECT required"
    off, sections, versions = 32, [], []
    for _ in range(count):
        cmd, length = struct.unpack_from("<II", raw, off)
        assert length >= 8 and off + length <= 32 + size
        if cmd == 0x19:
            for i in range(struct.unpack_from("<I", raw, off + 64)[0]):
                sections.append(raw[off + 72 + 80*i:off + 88 + 80*i].split(b"\0", 1)[0].decode())
        if cmd == 0x32:
            versions.append(struct.unpack_from("<4I", raw, off + 8))
        assert cmd not in (0xc, 0xe, 0x80000035), "no dylib, loader or fileset"
        off += length
    assert off == 32 + size
    assert not {"__mod_init_func", "__mod_term_func"} & set(sections)
    return {"sections": sections, "build_versions": versions, "sha256": sha(path), "bytes": len(raw)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt-sha256", required=True)
    parser.add_argument("--owner-receipt", default="source-validation-001.json")
    parser.add_argument("--run-dir", required=True, type=Path)
    args = parser.parse_args()
    assert len(args.receipt_sha256) == 64 and all(c in "0123456789abcdef" for c in args.receipt_sha256)
    assert Path(args.owner_receipt).name == args.owner_receipt
    assert args.owner_receipt.startswith("source-validation-") and args.owner_receipt.endswith(".json")
    receipts, sources = {}, {}
    for leaf, names in PRODUCTION.items():
        path = LEAF / leaf / (args.owner_receipt if leaf == "native_owned" else "source-validation-001.json")
        expected = args.receipt_sha256 if leaf == "native_owned" else FROZEN[leaf]
        assert sha(path) == expected, f"unfrozen receipt: {leaf}"
        receipt = load(path)
        receipts[str(path.relative_to(ROOT))] = expected
        for name in names:
            src = LEAF / leaf / name
            digest = receipt["files_sha256"][name]
            assert sha(src) == digest, str(src)
            sources[f"{leaf}/{name}"] = digest
    actual_pin = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT / "sources/puredarwin-baseline", text=True).strip()
    assert actual_pin == PIN
    run = args.run_dir.resolve()
    assert run.is_relative_to(ROOT / "runs/m1-desktop-010")
    run.mkdir(exist_ok=False)
    (run / "build-driver.py").write_bytes(Path(__file__).read_bytes())
    record = {"schema_version": 1, "recorded_utc": datetime.now(timezone.utc).isoformat(),
              "status": "BUILD_STARTED", "script_sha256": sha(__file__), "receipts_sha256": receipts,
              "production_files_sha256": sources, "pin": PIN, "commands": [], "compilations": [], "links": [],
              "target_executed": False, "module_image_packet_built": False}
    record["toolchain_sha256"] = {str(p): sha(p) for p in (
        TC / "arm64-apple-darwin20.4-clang", TC / "arm64-apple-darwin20.4-clang++",
        TC / "arm64-apple-darwin20.4-nm", LD)}
    env = dict(os.environ, DARWIN_SDK_ROOT=str(SDK))

    def command(argv, name):
        write(run / (name + "-command.json"), argv)
        result = subprocess.run([str(x) for x in argv], env=env, cwd=run, capture_output=True)
        log = run / (name + ".log")
        with log.open("xb") as f:
            f.write(result.stdout + result.stderr)
        record["commands"].append({"name": name, "returncode": result.returncode, "log_sha256": sha(log)})
        assert result.returncode == 0, f"{name}: see {log}"
        return result.stdout.decode()

    try:
        for name, digest in sources.items():
            src, dst = LEAF / name, run / "src" / name
            raw = src.read_bytes()
            assert hashlib.sha256(raw).hexdigest() == digest
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(raw)
        common = ["-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wframe-larger-than=1024", "-fstack-usage",
                  "-fno-stack-protector", "-fno-common", "-DKERNEL", "-DKERNEL_PRIVATE", "-DTARGET_OS_OSX",
                  "-isystem", str(KH / "Headers"), "-isystem", str(KH / "PrivateHeaders"), "-isystem",
                  str(ROOT / "sources/puredarwin-baseline/src/Libraries/AvailabilityVersions/include")]
        for opt in ("O0", "O2"):
            objects = []
            for leaf, name in UNITS:
                tag = Path(name).stem + "-" + opt
                cpp = name.endswith(".cpp")
                cc = TC / ("arm64-apple-darwin20.4-clang++" if cpp else "arm64-apple-darwin20.4-clang")
                flags = ["-std=c++17", "-fno-exceptions", "-fno-rtti", "-fapple-kext"] if cpp else ["-std=c11", "-ffreestanding", "-fno-builtin"]
                obj = run / (tag + ".o")
                command([str(cc), *flags, *common, "-" + opt, "-MD", "-MF", str(run / (tag + ".d")),
                         "-c", str(run / "src" / leaf / name), "-o", str(obj)], tag)
                frames = []
                for line in obj.with_suffix(".su").read_text().splitlines():
                    function, frame, mode = line.split("\t")
                    assert mode == "static" and int(frame) <= 1024, line
                    frames.append({"function": function, "bytes": int(frame)})
                dep = obj.with_suffix(".d").read_text().replace("\\\n", " ").split(":", 1)[1]
                dependencies = {str(Path(p)): sha(p) for p in shlex.split(dep)}
                assert not any("/test_sdk/" in p for p in dependencies)
                record["compilations"].append({"name": tag, "object": macho(obj), "frames": frames,
                                               "dependencies_sha256": dependencies})
                objects.append(str(obj))
            linked = run / ("linked-" + opt + ".o")
            command([str(LD), "-r", "-arch", "arm64", "-Z", "-platform_version", "macos", "26.5", "26.5", "-o", str(linked), *objects], "link-" + opt)
            undefined = command([str(TC / "arm64-apple-darwin20.4-nm"), "-u", str(linked)], "undefined-" + opt)
            symbols = sorted({line.split()[-1] for line in undefined.splitlines() if line.strip()})
            assert not any(s.startswith(("_bpl_", "_bno_")) for s in symbols), symbols
            record["links"].append({"optimization": opt, "object": macho(linked), "undefined": symbols})
        exports = {}
        for base in ("IOKit", "Libkern", "Mach", "Unsupported"):
            for suffix in (".exports", ".arm64.exports"):
                path = XNU / "config" / (base + suffix)
                if not path.exists():
                    continue
                rel = str(path.relative_to(ROOT / "sources/puredarwin-baseline"))
                pinned = subprocess.check_output(["git", "show", PIN + ":" + rel], cwd=ROOT / "sources/puredarwin-baseline")
                assert path.read_bytes() == pinned
                for number, line in enumerate(path.read_text().splitlines(), 1):
                    text = line.split("#", 1)[0].strip()
                    if not text:
                        continue
                    alias, _, target = text.partition(":")
                    exports.setdefault(alias.strip(), []).append({"file": str(path.relative_to(ROOT)), "line": number,
                                                                  "sha256": sha(path), "target": target.strip() or alias.strip()})
        kernel_text = command([str(TC / "arm64-apple-darwin20.4-nm"), "--defined-only", str(KERNEL)], "kernel-defined")
        kernel_symbols = {line.split()[-1] for line in kernel_text.splitlines() if line.strip()}
        coverage = {}
        for symbol in sorted({s for link in record["links"] for s in link["undefined"]}):
            candidates = exports.get(symbol, [])
            matching = [item for item in candidates if item["target"] in kernel_symbols]
            assert matching, f"uncovered import: {symbol}"
            coverage[symbol] = matching
        record["imports"] = coverage
        record["kernel"] = {"path": str(KERNEL), "sha256": sha(KERNEL)}
        for name, digest in sources.items():
            assert sha(LEAF / name) == digest, "source changed during build"
        for name, digest in receipts.items():
            assert sha(ROOT / name) == digest, "receipt changed during build"
        record["status"] = "FROZEN_REAL_SDK_ARM64_OBJECTS_MIXED_LINKED_IMPORTS_COVERED_NO_RUNTIME"
        write(run / "build.json", record)
        print(json.dumps({"status": record["status"], "build": str(run / "build.json"), "sha256": sha(run / "build.json")}))
    except BaseException as exc:
        record["status"], record["error"] = "BUILD_FAILED_PRESERVED_NO_RUNTIME", repr(exc)
        write(run / "failure.json", record)
        raise


if __name__ == "__main__":
    main()
