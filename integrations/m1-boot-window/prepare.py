#!/usr/bin/env python3
"""Verify, build offline, and package a local proposal. Never installs or opens a device."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SOURCE = ROOT / "sources/m1n1-boot-window"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked(condition, message):
    if not condition:
        raise ValueError(message)


def describe(path):
    data = path.read_bytes()
    return {"path": str(path.relative_to(ROOT)), "bytes": len(data), "sha256": sha(data)}


def verify_source(manifest):
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    checked(head == manifest["baseline_commit"], "checkout pin mismatch")
    checked(sha((HERE / "candidate.patch").read_bytes()) == manifest["patch_sha256"], "patch mismatch")
    for name, item in manifest["files"].items():
        checked(sha((SOURCE / name).read_bytes()) == item["candidate_sha256"], f"source mismatch: {name}")
    tracked = subprocess.check_output(["git", "diff", "--name-only", "HEAD"], cwd=SOURCE, text=True).splitlines()
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard"], cwd=SOURCE, text=True).splitlines()
    checked(set(tracked + untracked) == set(manifest["files"]), "unexpected checkout changes")
    checked(subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE / "artwork", text=True).strip()
            == "80d14f8b6f485b310e305a84b4b806361518ddd1", "artwork pin mismatch")
    checked(not subprocess.check_output(["git", "status", "--porcelain"], cwd=SOURCE / "artwork"), "dirty artwork")


def footprint(data):
    checked(data[0x808:0x810] == b"AURALOAD", "missing raw load header")
    version, flags, size, run_base = struct.unpack_from("<IIQQ", data, 0x810)
    checked(version == 1 and flags == 1 and run_base == 0, "unexpected load header")
    return size


def dtb_compatible(data):
    magic, size, off, strings, _, version, last, _, string_size, struct_size = struct.unpack_from(">10I", data)
    checked(magic == 0xd00dfeed and size == len(data), "invalid DTB size")
    checked(version >= 17 and last <= 17 and strings + string_size <= size and off + struct_size <= size,
            "invalid DTB bounds")
    depth = 0
    end = off + struct_size
    compat = []
    while off < end:
        token = struct.unpack_from(">I", data, off)[0]
        off += 4
        if token == 1:
            off = (data.index(b"\0", off, end) + 4) & ~3
            depth += 1
        elif token == 2:
            depth -= 1
        elif token == 3:
            length, nameoff = struct.unpack_from(">II", data, off)
            off += 8
            checked(nameoff < string_size and off + length <= end, "DTB property out of bounds")
            name = data[strings + nameoff:data.index(b"\0", strings + nameoff, strings + string_size)]
            if depth == 1 and name == b"compatible":
                compat = data[off:off + length].rstrip(b"\0").decode("ascii").split("\0")
            off = (off + length + 3) & ~3
        elif token == 4:
            pass
        elif token == 9:
            checked(depth == 0, "unbalanced DTB")
            return compat
        else:
            raise ValueError("unexpected DTB token")
    raise ValueError("missing DTB end token")


def inspect_trailer(trailer):
    offset = 0
    dtbs = []
    while trailer[offset:offset + 4] == bytes.fromhex("d00dfeed"):
        length = struct.unpack_from(">I", trailer, offset + 4)[0]
        checked(length >= 40 and offset + length <= len(trailer), "bad trailer DTB length")
        blob = trailer[offset:offset + length]
        dtbs.append({"offset": offset, "bytes": length, "sha256": sha(blob), "compatible": dtb_compatible(blob)})
        offset += length
    # This exact backup is only DTBs + one gzip U-Boot image. Refuse unknown additions.
    checked(trailer[offset:offset + 3] == b"\x1f\x8b\x08", "expected final gzip U-Boot")
    decoder = zlib.decompressobj(31)
    image = decoder.decompress(trailer[offset:], 16 * 1024 * 1024)
    checked(decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail, "unrecognized trailer data")
    checked(image[0x38:0x3c] == b"ARMd" and b"U-Boot" in image, "expected ARM64 U-Boot payload")
    j293 = [item for item in dtbs if "apple,j293" in item["compatible"]]
    checked(j293 and all("apple,t8103" in item["compatible"] for item in j293), "missing J293/T8103 DTB")
    return {"bytes": len(trailer), "sha256": sha(trailer), "dtb_count": len(dtbs), "j293_dtbs": j293,
            "gzip_offset": offset, "gzip_bytes": len(trailer) - offset,
            "uboot_uncompressed_bytes": len(image), "uboot_uncompressed_sha256": sha(image),
            "recognized_components_only": "DTBs followed by one gzip ARM64 U-Boot; no added firmware"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", required=True, type=int)
    args = parser.parse_args()
    checked(1 <= args.attempt <= 999, "attempt must be 1..999")
    manifest = json.loads((HERE / "manifest.json").read_text())
    verify_source(manifest)
    original_path = ROOT / manifest["original"]["path"]
    original = original_path.read_bytes()
    checked(sha(original) == manifest["original"]["sha256"] and len(original) == manifest["original"]["bytes"],
            "original backup mismatch")
    boundary = footprint(original)
    checked(boundary == manifest["original"]["payload_offset"], "original payload boundary mismatch")
    trailer = original[boundary:]
    trailer_info = inspect_trailer(trailer)
    log = ROOT / f"runs/m1-bringup-008/window-build-{args.attempt:03}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    checked(not log.exists(), "attempt log already exists; use a fresh attempt number")
    env = {**os.environ, "M1N1_VERSION_TAG": manifest["build_tag"], "CARGO_NET_OFFLINE": "true",
           "PYTHONDONTWRITEBYTECODE": "1"}
    for name in ("MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS"):
        env.pop(name, None)
    build_directory_existed = (SOURCE / "build").exists()
    with log.open("xb") as output:
        subprocess.run([sys.executable, str(HERE / "test_candidate.py")], stdout=output, stderr=subprocess.STDOUT,
                       env=env, check=True)
        result = subprocess.run(manifest["build_command"], cwd=SOURCE, stdout=output, stderr=subprocess.STDOUT, env=env)
    checked(result.returncode == 0, f"build failed; preserved {log}")
    build_lines = log.read_text().splitlines()
    compile_commands = [line for line in build_lines if line.startswith("clang ") and " -c " in line]
    link_commands = [line for line in build_lines if line.startswith("ld.lld ")]
    if not build_directory_existed:
        for name in ("src/main.c", "src/uartproxy.c", "src/m1_boot_window.c"):
            checked(any(line.endswith(" " + name) for line in compile_commands), f"missing compile: {name}")
        checked(len(link_commands) == 2, "missing actual raw/MachO ELF links")
    verify_source(manifest)
    raw_path = SOURCE / "build/m1n1.bin"
    raw = raw_path.read_bytes()
    checked(footprint(raw) == len(raw), "raw size is not its payload boundary")
    expected_tag = f'#define BUILD_TAG "{manifest["build_tag"]}"\n'
    checked((SOURCE / "build/build_tag.h").read_text() == expected_tag, "BUILD_TAG mismatch")
    checked(b"##m1n1_ver##" + manifest["build_tag"].encode() + b"\0" in raw, "linked variant marker missing")
    cfg = (SOURCE / "build/build_cfg.h").read_text()
    checked(set(cfg.splitlines()) == {"#define CHAINLOADING", "#define ESP_STAGE2", "#define M1_BOOT_WINDOW",
                                    "#define BUILTIN_LOGO bootlogo"}, "unexpected build selection")
    elf = (SOURCE / "build/m1n1-raw.elf").read_bytes()
    checked(elf[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", elf, 18)[0] == 183,
            "not ELF64 little-endian AArch64")
    checked(struct.unpack_from("<Q", elf, 24)[0] == 0x800, "unexpected entry point")
    symbols = subprocess.check_output(["llvm-nm", "-n", str(SOURCE / "build/m1n1-raw.elf")], text=True)
    symbol_map = {parts[2]: int(parts[0], 16) for line in symbols.splitlines()
                  if len(parts := line.split()) == 3 and parts[1].upper() != "U"}
    checked(symbol_map["_payload_start"] == len(raw) and symbol_map["_base"] == 0, "linked payload mismatch")
    for name in ("uartproxy_wait_m1_boot_window", "m1_boot_window_allowed", "m1_boot_window_feed", "window_request"):
        checked(name in symbol_map, f"candidate code missing: {name}")
    outdir = ROOT / f"artifacts/m1-unattended-008/window-candidate-{args.attempt:03}"
    outdir.mkdir(parents=True, exist_ok=False)
    proposal = outdir / "proposed-boot.bin"
    proposal.write_bytes(raw + trailer)
    (outdir / "m1n1-window.bin").write_bytes(raw)
    disassembly = subprocess.check_output([
        "llvm-objdump", "-d", "--no-show-raw-insn",
        "--disassemble-symbols=run_actions,uartproxy_wait_m1_boot_window,m1_boot_window_feed",
        str(SOURCE / "build/m1n1-raw.elf")], text=True)
    linked_run = disassembly.split("<run_actions>:", 1)[1].split("\n\n", 1)[0]
    for name in ("m1_boot_window_allowed", "usb_init", "usb_iodev_init", "uartproxy_wait_m1_boot_window", "payload_run", "usb_hpm_restore_irqs"):
        checked("<" + name + ">" in linked_run, f"missing linked call: {name}")
    checked(linked_run.index("<uartproxy_wait_m1_boot_window>") < linked_run.index("<payload_run>"),
            "linked window not before payload")
    checked(linked_run.index("<payload_run>") < linked_run.index("<usb_hpm_restore_irqs>"),
            "linked native IRQ restoration not after payload preparation")
    (outdir / "window-disassembly.txt").write_text(disassembly)
    checked(proposal.read_bytes()[len(raw):] == trailer, "trailer changed during packaging")
    checked(original_path.read_bytes() == original, "original backup changed")
    report = {"status": "OFFLINE_BUILT_AND_PACKAGED_NOT_INSTALLED_OR_HARDWARE_VALIDATED",
              "attempt": args.attempt, "manifest": describe(HERE / "manifest.json"),
              "patch": describe(HERE / "candidate.patch"), "source_commit": manifest["baseline_commit"],
              "build_tag": manifest["build_tag"], "source_files": manifest["files"],
              "build_command": manifest["build_command"], "build_environment": {
                  "M1N1_VERSION_TAG": manifest["build_tag"], "CARGO_NET_OFFLINE": "true"},
              "build_returncode": result.returncode, "build_log": describe(log),
              "build_scope": "INCREMENTAL" if build_directory_existed else "FRESH_BUILD_DIRECTORY",
              "actual_compile_commands": len(compile_commands), "actual_link_commands": len(link_commands),
              "linked_disassembly": describe(outdir / "window-disassembly.txt"),
              "linked_window_before_payload": True,
              "linked_native_irq_restore_after_payload": True,
              "artifacts": [describe(proposal), describe(outdir / "m1n1-window.bin"),
                            describe(SOURCE / "build/m1n1-raw.elf"), describe(SOURCE / "build/m1n1.macho")],
              "original": describe(original_path), "original_payload_offset": boundary,
              "candidate_payload_offset": len(raw), "trailer": trailer_info, "trailer_byte_equal": True,
              "build_cfg": cfg, "linked_entry": "0x800", "linked_file_end": hex(symbol_map["_file_end"]),
              "tests": "ASan/UBSan production pure C policy + source dispatcher/selection assertions; host-only",
              "leak_sanitizer": "disabled: allocation-free policy; process ptrace prevents LSan inspection",
              "tool_versions": {}, "physical_execution": False, "installed": False,
              "network_during_build": "Cargo --locked --offline; local clone and local artwork only",
              "limits": ["10-second acquisition deadline starts after USB initialization; not a hardware hang watchdog",
                         "New stage2 prefix replaces installed f5a source; Linux/USB/reset behavior unqualified",
                         "NOP is acquisition, not authentication; supervisor must stop probes before recovery",
                         "Patched bootstrap is not frozen007 exact proxy runtime; RAM-chainload and requalify exact121",
                         "Host tool versions observed, not a hermetic Nix toolchain pin"]}
    for tool in ("clang", "ld.lld", "llvm-objcopy", "rustc", "cargo", "make"):
        report["tool_versions"][tool] = subprocess.check_output([tool, "--version"], text=True).splitlines()[0]
    report_path = ROOT / "reports/sprint-008/window-candidate-build.json"
    encoded = json.dumps(report, indent=2) + "\n"
    (outdir / "build-receipt.json").write_text(encoded)
    report_path.write_text(encoded)
    print(json.dumps({"status": report["status"], "proposal": describe(proposal), "report": str(report_path)}, indent=2))


if __name__ == "__main__":
    main()
