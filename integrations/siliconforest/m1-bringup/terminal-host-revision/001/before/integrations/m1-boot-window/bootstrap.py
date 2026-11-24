#!/usr/bin/env python3
"""Offline-by-default J293 window -> immutable exact121 RAM bootstrap. No recovery commands."""
from __future__ import annotations

import argparse
import copy
from functools import lru_cache
import hashlib
import importlib.util
import json
import multiprocessing
import os
from pathlib import Path
import struct
import sys
import traceback

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[1]
PIN = "121fcbf35d4501814b58ca5347926a4173b6a512"
WINDOW_TAG = PIN + "-darwin-lab-j293-window-v1"
WINDOW_SHA = "ba5bf4b396168ac540df852e3507c4342dd289eef7bf2c1fc29c50f11f0822bf"
TARGET_SHA = "05b36cc8288843211e97bd4f68adcf521c81e5e68afd98a4d094a6f03defb32c"
FROZEN_SHA = "a9f68695c6465bc5a3c1a9df04f67da6a72f99265e899cf3b89ba671ab6d4417"
STUB_SHA = "6cf0c6c7c88698af6d49ae7b40134e011bc7d38b43b908f0e210ebd0d2de5d92"
SOURCE_SHA = {
    "proxyclient/tools/chainload.py": "502c8a96a780c9551474fc35cb111007d0e07144c645bd31cceb25b5037ec996",
    "proxyclient/m1n1/bootmem.py": "6e3d7c3eec472b22defc8a801a94125d2c913a48ed2ff754e213da90a70afbcb",
}
BLOBS = ("SEPFW", "SEPPatches", "uStuff", "preoslog", "RTBuddySeg", "RAMDisk",
         "SPTMDebug", "ExclaveOSIntegrityCatalog", "ExclaveOSTrustCache", "TrustCache")
PAGE = 0x4000
MAX_FW = 256 << 20
CONFIRMATION = "J293_RECOVERY_READY"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align(value):
    return (value + PAGE - 1) & -PAGE


def integer(value):
    require(isinstance(value, int) and not isinstance(value, bool) and 0 <= value < 1 << 64,
            "Invalid unsigned 64-bit value")
    return int(value)


def span(addr, size, low, high, label):
    integer(addr); integer(size)
    require(size > 0 and 0 < low <= addr < addr + size <= high <= 1 << 64,
            label + " outside bounded DRAM")
    return (addr, addr + size)


def overlaps(a, b):
    return a[0] < b[1] and b[0] < a[1]


@lru_cache(maxsize=1)
def frozen():
    path = LAB / "integrations/puredarwin/m1-firstboot/host_console.py"
    require(digest(path.read_bytes()) == FROZEN_SHA, "Frozen007 helper changed")
    spec = importlib.util.spec_from_file_location("darwin_bootstrap_frozen007", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def raw_image(data, sha, tag, size):
    require(len(data) == size and digest(data) == sha, "Raw image hash/size mismatch")
    require(data[0x808:0x810] == b"AURALOAD" and
            struct.unpack_from("<IIQQ", data, 0x810) == (1, 1, size, 0), "Unexpected raw load header")
    marker = ("##m1n1_ver##" + tag + "\0").encode()
    require(data.count(marker) == 1, "Raw image BUILD_TAG is not unique")
    return data.index(marker), marker


def trampoline(data, entry):
    require(len(data) == 104 and digest(data) == STUB_SHA, "Unreviewed trampoline bytes")
    integer(entry)
    require(entry % PAGE == 0x800, "Entry would change RVBAR page")
    require(data[96:] == struct.pack("<Q", 0x1122334455667788), "Trampoline literal changed")
    return data[:96] + struct.pack("<Q", entry)


def verify(args):
    f = frozen()
    inputs = f.verify_inputs(args.manifest, args.host_source, args.inventory)
    for name, expected in SOURCE_SHA.items():
        require(name in inputs.host_files and digest(inputs.host_files[name]) == expected,
                "Canonical host source mismatch: " + name)
    window = f.packet.artifact_bytes(args.window_image)
    raw_image(window, WINDOW_SHA, WINDOW_TAG, 0x420000)
    raw_image(inputs.contents["m1n1"], TARGET_SHA, PIN, 0x400000)
    stub_path = HERE / "bootstrap-trampoline.bin"
    require(stub_path.stat().st_size == 104, "Unexpected trampoline length")
    stub = stub_path.read_bytes()
    trampoline(stub, 0x800000800)
    return inputs, window, stub


def plan_memory(ba, base, heap, ba_addr, ba_size, dtaddr, dt_size, memory_map,
                raw_size=0x400000, live_size=0x420000):
    """Pure range policy. All source blobs stay on target; no unknown overwrite is allowed."""
    low = integer(ba["phys_base"])
    high = low + integer(ba["mem_size"])
    dram = low & ~0xffffffff
    require(0 < ba["mem_size"] <= ba["mem_size_actual"] <= 64 << 30 and high <= dram + ba["mem_size_actual"],
            "Invalid DRAM extent")
    require(base % PAGE == 0 and heap % 64 == 0, "Unaligned live base/heap")
    live = span(base, live_size, low, high, "Live variant")
    copy_size = align(raw_size + 4)  # zero terminator and zero padding, canonical raw contract
    dest = span(base, copy_size, low, high, "Image destination")
    require(dest[1] <= live[1], "Destination exceeds known live image footprint")
    require(live[1] <= ba["top_of_kernel_data"] <= heap < high, "Live image/top/heap ordering")
    protected = [("original BootArgs", span(ba_addr, ba_size, dram, high, "BootArgs")),
                 ("original ADT", span(dtaddr, dt_size, dram, high, "ADT"))]
    require(0 < dt_size <= 16 << 20 and 0 < ba_size <= PAGE, "Metadata too large")
    blobs = []
    fw_size = 0
    for name, pair in memory_map.items():
        require(isinstance(name, str) and isinstance(pair, (tuple, list)) and len(pair) == 2,
                "Invalid memory-map property")
        addr, size = map(integer, pair)
        if (addr, size) == ((1 << 64) - 1, (1 << 64) - 1) or size == 0:
            continue
        region = span(addr, size, dram, high, name)
        require(region[1] <= heap, name + " extends above live heap")
        # Existing kernel descriptors may describe precisely the image being replaced.
        replaceable = name.startswith("Kernel-") and live[0] <= region[0] < region[1] <= live[1]
        if not replaceable:
            protected.append((name, region))
        if name in BLOBS:
            require(size <= MAX_FW, "Firmware blob too large")
    for name in BLOBS:  # preserve canonical ordering
        if name not in memory_map:
            continue
        addr, size = memory_map[name]
        if size == 0 or (addr, size) == ((1 << 64) - 1, (1 << 64) - 1):
            continue
        blobs.append({"name": name, "src": addr, "size": size, "offset": fw_size})
        fw_size = align(fw_size + size)
    require({"SEPFW", "TrustCache"} <= {b["name"] for b in blobs}, "Required firmware absent")
    require(0 < fw_size <= MAX_FW, "Unbounded firmware retention block")
    for name, region in protected:
        require(region[1] <= heap and not overlaps(dest, region), "Destination overlaps " + name)
    for i, blob in enumerate(blobs):
        for other in blobs[:i]:
            require(not overlaps((blob["src"], blob["src"] + blob["size"]),
                                 (other["src"], other["src"] + other["size"])), "Aliased firmware blobs")
    stage = align(heap)
    stub = stage + copy_size
    fw = stub + PAGE
    ba_dest = fw + fw_size
    adt_dest = ba_dest + PAGE
    adt_capacity = align(dt_size + PAGE)
    end = adt_dest + adt_capacity
    reserve = span(heap, end - heap, low, high, "Staging/retention reservation")
    require(not overlaps(reserve, live), "Staging overlaps executing image")
    for name, region in protected:
        require(not overlaps(reserve, region), "Staging overlaps " + name)
    return {"base": base, "entry": base + 0x800, "heap": heap, "reserve_size": end - heap,
            "stage": stage, "copy_size": copy_size, "stub": stub, "fw": fw,
            "fw_size": fw_size, "bootargs": ba_dest, "adt": adt_dest,
            "adt_capacity": adt_capacity, "top": end, "blobs": blobs}


def connect(backend, device):
    # Constructor imports only; this is the first device operation, after verification.
    backend.iface = backend.proxy_module.UartInterface(device=device)
    dev = backend.iface.dev
    dev.write_timeout = 5
    original_write = dev.write
    def checked_write(data):
        count = original_write(data)
        require(type(count) is int and count == len(data), "Short serial write; stop without retry")
        return count
    dev.write = checked_write
    backend.iface.nop()  # one ordinary full checksum-valid NOP acquires the window
    backend.p = backend.proxy_module.M1N1Proxy(backend.iface)


def collect(backend, inputs, window, directory):
    f = frozen()
    p, iface = backend.p, backend.iface
    base = integer(p.get_base())
    # Bound this first identity read before trusting BootArgs obtained from the peer.
    require(0x800000000 <= base < 0x1800000000 - len(window) and base % PAGE == 0, "Invalid live base")
    tag_offset, tag = raw_image(window, WINDOW_SHA, WINDOW_TAG, 0x420000)
    require(iface.readmem(base + tag_offset, len(tag)) == tag, "Live proxy is not the window variant")
    require(backend.proxy_module.CPUFeatures.sizeof() == 20, "Host CPUFeatures size changed")
    iodev = p.iodev_whoami()
    require(iodev in (backend.proxy_module.IODEV.USB0, backend.proxy_module.IODEV.USB1), "Native USB0/1 required")
    address = p.get_bootargs()
    span(address, 4, 0x800000000, 0x1800000000, "Initial BootArgs revision read")
    revision = struct.unpack("<H", iface.readmem(address, 2))[0]
    require(revision in (1, 2, 3), "Unknown BootArgs revision")
    fmt = getattr(backend.types, "BootArgs_r" + str(revision))
    span(address, fmt.sizeof(), 0x800000000, 0x1800000000, "Initial BootArgs read")
    raw_ba = iface.readmem(address, fmt.sizeof())
    ba = fmt.parse(raw_ba)
    require(ba.revision == revision, "BootArgs revision changed")
    dtaddr = f.validate_bootargs(ba, address, fmt.sizeof())
    span(base, len(window), ba.phys_base, ba.phys_base + ba.mem_size, "Observed proxy")
    features = read_features(backend, ba)
    require("m1n1-debug" not in ba.cmdline.split(), "Debug carrier boot argument is unsupported")
    raw_adt = iface.readmem(dtaddr, ba.devtree_size)
    tree = backend.adt_module.load_adt(raw_adt)
    validate_identity(tree, p.get_chipid())
    # Preserve exact originals even if decoding the memory-map properties refuses.
    f.write_private(directory, "original-bootargs.bin", raw_ba)
    f.write_private(directory, "original-adt.bin", raw_adt)
    memory_map = parse_memory_map(backend.adt_module, tree["chosen"]["memory-map"])
    heap = integer(p.heapblock_alloc(0))
    f.write_private(directory, "original-observation.json", {
        "build_tag": WINDOW_TAG, "base": base, "iodev": int(iodev), "cpu_features_abi_bytes": 20,
        "chip_id": 0x8103, "target_type": tree.target_type, "model": tree.model,
        "bootargs_sha256": digest(raw_ba), "adt_sha256": digest(raw_adt),
        "inventory_sha256": inputs.inventory_sha256, "source_pin": PIN,
        "live_heap": heap,
        "memory_map": {name: {"addr": pair[0], "size": pair[1]} for name, pair in memory_map.items()},
        "runtime_after_vector_verified": False})
    layout = plan_memory(ba, base, heap, address, fmt.sizeof(), dtaddr, len(raw_adt), memory_map)
    return ba, fmt, tree, layout, features


def parse_memory_map(adt_module, mm):
    """Decode regions without removing structural properties from the original ADT."""
    memory_map = {}
    for name, value in mm._properties.items():
        if name == "name":
            continue
        raw = adt_module.build_prop(mm._path, name, value, t=mm._types.get(name, (None, False))[0])
        if name == "AAPL,phandle":
            require(len(raw) == 4, "Invalid memory-map AAPL,phandle encoding: expected 4 bytes")
            continue
        if name == "kernel-only":
            require(raw == b"true\0", "Invalid memory-map kernel-only encoding: expected true with NUL")
            continue
        require(len(raw) == 16, "Unknown memory-map encoding: " + name)
        memory_map[name] = struct.unpack("<QQ", raw)
    return memory_map


def validate_identity(tree, proxy_chip):
    require(tree.target_type == "J293" and tree.model == "MacBookPro17,1" and
            tree["chosen"].chip_id == proxy_chip == 0x8103 and
            tree["/arm-io/uart0"].get_reg(0)[0] == 0x235200000 and
            sum(cpu.state == "running" for cpu in tree["cpus"]) == 1,
            "Live board/chip/UART/single-running-CPU mismatch")


def read_features(backend, ba):
    # The canonical convenience getter reads the returned pointer immediately.
    # Keep the same 20-byte ABI request, but validate that pointer before reading it.
    fmt = backend.proxy_module.CPUFeatures
    require(fmt.sizeof() == 20, "Host CPUFeatures size changed")
    address = backend.p.request(backend.p.P_GET_CPU_FEATURES, 20)
    span(address, 20, ba.phys_base & ~0xffffffff, ba.phys_base + ba.mem_size, "CPUFeatures")
    return fmt.parse(backend.iface.readmem(address, 20))


def snapshot_adt(adt_module, tree):
    """Snapshot every encoded property/template flag and ordered child path before edits."""
    nodes = {}
    for node in tree.walk_tree():
        require(node._path not in nodes, "Ambiguous duplicate ADT node path")
        properties = {}
        for name, value in node._properties.items():
            kind, template = node._types.get(name, (None, False))
            properties[name] = (bool(template), bytes(adt_module.build_prop(node._path, name, value, t=kind)))
        nodes[node._path] = {"properties": properties, "children": tuple(child._path for child in node)}
    return nodes


def bootargs_fields(value):
    # Construct's _io is parser bookkeeping, not a BootArgs field.
    if isinstance(value, dict):
        return {key: bootargs_fields(item) for key, item in value.items() if key != "_io"}
    if isinstance(value, (tuple, list)):
        return [bootargs_fields(item) for item in value]
    return value


def prepare_metadata(ba, fmt, tree, layout, adt_module):
    # Retain all unrelated properties. Original target ADT bytes are never overwritten.
    from construct import Array, Int64ul
    expected_tree = snapshot_adt(adt_module, tree)
    expected_ba = copy.deepcopy(bootargs_fields(ba))
    mm = tree["chosen"]["memory-map"]
    for blob in layout["blobs"]:
        name = blob["name"]
        mm._properties[name] = (layout["fw"] + blob["offset"], blob["size"])
        mm._types[name] = (Array(2, Int64ul), False)
    for name, pair in {"BootArgs": (layout["bootargs"], PAGE), "DeviceTree": (layout["adt"], ba.devtree_size)}.items():
        mm._properties[name] = pair
        mm._types[name] = (Array(2, Int64ul), False)
    adt = tree.build()
    require(len(adt) <= layout["adt_capacity"], "ADT rebuild exceeds reserved capacity")
    mm._properties["DeviceTree"] = (layout["adt"], len(adt))
    adt = tree.build()
    require(mm._properties["DeviceTree"][1] == len(adt), "ADT size did not stabilize")
    tba = ba.copy()
    tba.top_of_kernel_data = layout["top"]
    tba.devtree = integer(layout["adt"] - ba.phys_base + ba.virt_base)
    tba.devtree_size = len(adt)
    raw = fmt.build(tba)
    require(len(raw) <= PAGE, "BootArgs overflow")
    expected_ba.update(top_of_kernel_data=layout["top"], devtree=tba.devtree, devtree_size=len(adt))
    require(bootargs_fields(fmt.parse(raw)) == expected_ba,
            "BootArgs round-trip changed fields beyond the three intended updates")
    intended = {blob["name"]: (layout["fw"] + blob["offset"], blob["size"]) for blob in layout["blobs"]}
    intended.update(BootArgs=(layout["bootargs"], PAGE), DeviceTree=(layout["adt"], len(adt)))
    for name, pair in intended.items():
        expected_tree[mm._path]["properties"][name] = (False, struct.pack("<QQ", *pair))
    rebuilt_tree = adt_module.load_adt(adt)
    require(snapshot_adt(adt_module, rebuilt_tree) == expected_tree,
            "ADT round-trip changed properties or topology beyond intended memory-map updates")
    return raw, adt


def stage_and_vector(backend, target, stub_template, layout, raw_ba, adt, directory):
    f = frozen()
    p, iface = backend.p, backend.iface
    image = target.ljust(layout["copy_size"], b"\0")
    require(len(image) == layout["copy_size"] and len(image) % 64 == 0, "Bad trampoline copy size")
    stub = trampoline(stub_template, layout["entry"])
    # One monotonic reservation instead of ProxyUtils' host heap or target malloc.
    require(p.heapblock_alloc(0) == layout["heap"], "Heap moved after planning")
    require(p.heapblock_alloc(layout["reserve_size"]) == layout["heap"], "Reservation base changed")
    require(p.heapblock_alloc(0) == layout["top"], "Reservation end changed")
    for blob in layout["blobs"]:
        p.memcpy8(layout["fw"] + blob["offset"], blob["src"], blob["size"])
    # Firmware never traverses the host interface. Only public raw image/metadata/stub do.
    for address, data in [(layout["stage"], image), (layout["bootargs"], raw_ba),
                          (layout["adt"], adt), (layout["stub"], stub)]:
        iface.writemem(address, data)
        require(iface.readmem(address, len(data)) == data, "Target RAM write/readback mismatch")
    p.dc_cvac(layout["stage"], layout["top"] - layout["stage"])
    p.ic_ivau(layout["stub"], len(stub))
    f.write_private(directory, "prepared-layout.json", {"layout": layout, "target_sha256": digest(target),
        "stub_sha256": digest(stub), "firmware_copy_acknowledged_not_byte_verified": True,
        "vector_attempts": 0, "runtime_verified": False})
    # No reload() fallback, P_CALL, wait_boot(), NOP retry or second vector after this point.
    f.write_private(directory, "vector-attempt.json", {"vector_attempts": 1, "runtime_verified": False})
    p.request(p.P_VECTOR, layout["stub"], layout["bootargs"], layout["stage"], layout["base"], layout["copy_size"])
    f.write_private(directory, "vector-ack.json", {"status": "VECTOR_ACKNOWLEDGED_RUNTIME_UNVERIFIED",
                                                  "runtime_verified": False, "hardware_boot_observed": False})


def authorize(args):
    require(args.execute and args.device and args.confirm_recovery == CONFIRMATION,
            "Execution requires explicit device and recovery confirmation")
    require(args.device.startswith("/dev/cu.usbmodem") and "/" not in args.device[len("/dev/"):] and
            ":" not in args.device and len(args.device) > len("/dev/cu.usbmodem"), "Explicit native CDC callout required")
    require(args.private_directory is not None and not args.private_directory.resolve().is_relative_to(LAB),
            "Private directory outside repository required")
    require(1 <= args.timeout <= 300, "Timeout outside 1..300 seconds")
    require(not any(name.startswith("M1N1") for name in os.environ), "Unexpected m1n1 environment override")


def run_worker(args, verified, factory=None):
    authorize(args)
    inputs, window, stub = verified
    f = frozen()
    backend = (factory or f.PinnedBackend)(inputs, args.private_directory)
    try:
        connect(backend, args.device)
        ba, fmt, tree, layout, features = collect(backend, inputs, window, args.private_directory)
        raw_ba, adt = prepare_metadata(ba, fmt, tree, layout, backend.adt_module)
        offset, marker = raw_image(window, WINDOW_SHA, WINDOW_TAG, 0x420000)
        require(backend.iface.readmem(layout["base"] + offset, len(marker)) == marker,
                "Window identity changed before staging")
        require(read_features(backend, ba) == features and backend.p.get_base() == layout["base"],
                "Live proxy changed before staging")
        stage_and_vector(backend, inputs.contents["m1n1"], stub, layout, raw_ba, adt, args.private_directory)
    finally:
        backend.close()  # Host descriptor only; autonomous target behavior remains unknown.


def worker_entry(args):
    os.umask(0o077)
    os.setsid()
    fd = os.open(args.private_directory / "worker.log", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.dup2(fd, 1); os.dup2(fd, 2); os.close(fd)
    sys.stdout = os.fdopen(1, "w", buffering=1, closefd=False)
    sys.stderr = os.fdopen(2, "w", buffering=1, closefd=False)
    try:
        run_worker(args, verify(args))
    except BaseException:
        traceback.print_exc()
    finally:
        sys.stdout.flush(); sys.stderr.flush()
    raise SystemExit(1)  # No automatic runtime/boot PASS, including after an acknowledged vector.


def supervise(args, process_factory=None):
    process = (process_factory or multiprocessing.get_context("spawn").Process)(target=worker_entry, args=(args,))
    process.start()
    status = "BOOTSTRAP_WORKER_EXITED_NO_RUNTIME_VERDICT"
    try:
        process.join(args.timeout)
        if process.is_alive():
            status = "BOOTSTRAP_WORKER_TIMEOUT"
    except KeyboardInterrupt:
        status = "BOOTSTRAP_OPERATOR_INTERRUPTED"
    if status != "BOOTSTRAP_WORKER_EXITED_NO_RUNTIME_VERDICT":
        try:
            process.kill()  # Only this host worker; never reset the target.
        except ProcessLookupError:
            pass
        process.join(5)
    return {"status": status, "worker_returncode": process.exitcode, "runtime_verified": False,
            "hardware_boot_observed": False, "target_state": "unknown; independent recovery required"}


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", type=Path)
    p.add_argument("--host-source", type=Path)
    p.add_argument("--window-image", type=Path)
    p.add_argument("--inventory", type=Path, default=LAB / "reports/sprint-007/ssh-target-inventory.json")
    p.add_argument("--execute", action="store_true")
    p.add_argument("--device")
    p.add_argument("--private-directory", type=Path)
    p.add_argument("--confirm-recovery", metavar=CONFIRMATION)
    p.add_argument("--timeout", type=int, default=120)
    return p


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.execute:
            authorize(args)
        supplied = [args.manifest, args.host_source, args.window_image]
        require(all(supplied) or not any(supplied), "Supply manifest, host source and window image together")
        require(not args.execute or all(supplied), "Execution requires all verified inputs")
        verified = verify(args) if all(supplied) else None
        if not args.execute:
            print(json.dumps({"status": "OFFLINE_PLAN_ONLY", "inputs_verified": verified is not None,
                "device_contacted": False, "from": WINDOW_TAG, "to": PIN,
                "entry": "same live base + 0x800", "planned_vector_attempts": 1, "runtime_verified": False,
                "firmware": "retained in target RAM only", "recovery": "independent external controller"}, indent=2))
            return 0
        args.private_directory = args.private_directory.resolve()
        args.private_directory.mkdir(mode=0o700, parents=False, exist_ok=False)
        result = supervise(args)
        frozen().write_private(args.private_directory, "host-result.json", result)
        print(json.dumps(result, indent=2))
        return 1
    except Exception as error:
        print(json.dumps({"status": "REJECTED", "error": str(error), "runtime_verified": False}))
        return 1


if __name__ == "__main__":
    sys.exit(main())
