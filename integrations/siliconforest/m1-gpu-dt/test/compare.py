#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compares the library's output with the properties Linux saw on the same machine.

usage: compare.py <harness> <ground-truth.txt> [<more-ground-truth.txt> ...]

The ground-truth file has a line "== <node path>" per node, then one line per
property: "name length hexbytes". Values are printed as hex numbers only, and
only for the GPU's own nodes: /chosen holds device identifiers, so for it this
script prints names and lengths of what it did not produce, never values.

Exit status is 1 when a property differs, when a property the driver requires
is not produced, or when a ground-truth property is neither produced nor
accounted for below.
"""
import subprocess
import sys

def node_path(gt_name):
    """ground-truth node name -> node path in the library's tree"""
    name = gt_name[2:] if gt_name.startswith("./") else gt_name
    return name[4:] if name.startswith("soc/") else name


# Differences from Linux that are intended: (node, property) -> why
EXPECTED_DIFFERENCE = {}
BLOB = ("m1n1 allocates this region at boot and fills it with its own init data; "
        "Darwin leaves the node disabled, the driver then skips a debug comparison")
for _n in ("hw-cal-a", "hw-cal-b", "globals"):
    EXPECTED_DIFFERENCE[("reserved-memory/" + _n, "status")] = BLOB

# Ground-truth properties the library does not produce on purpose.
NOT_PRODUCED = {
    ("gpu@206400000", "name"): "node name as the kernel's /proc/device-tree shows it, not a property of the FDT",
    ("reserved-memory", "name"): "node name, not a property of the FDT",
    ("opp-table-gpu", "name"): "node name, not a property of the FDT",
    ("chosen", "name"): "node name, not a property of the FDT",
    ("gpu@206400000", "debug,globals-size"): "size of an init-data blob m1n1 builds at boot; debug comparison only, driver default 0",
    ("gpu@206400000", "debug,hw-cal-a-size"): "size of an init-data blob m1n1 builds at boot; debug comparison only, driver default 0",
    ("gpu@206400000", "debug,hw-cal-b-size"): "size of an init-data blob m1n1 builds at boot; debug comparison only, driver default 0",
}
CHOSEN_NOTE = "not read by the GPU driver (boot loader, EFI or identity data)"
for _n in ("hw-cal-a", "hw-cal-b", "globals"):
    NOT_PRODUCED[("reserved-memory/" + _n, "reg")] = BLOB


def fnv1a(h, data):
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h

# Properties of the GPU node without which the T8103 path of the driver fails to probe.
REQUIRED = """compatible reg reg-names memory-region memory-region-names operating-points-v2
apple,firmware-compat apple,core-leak-coef apple,sram-leak-coef apple,power-sample-period
apple,min-sram-microvolt apple,avg-power-filter-tc-ms apple,avg-power-ki-only apple,avg-power-kp
apple,avg-power-min-duty-cycle apple,avg-power-target-filter-tc apple,fast-die0-integral-gain
apple,fast-die0-proportional-gain apple,perf-filter-drop-threshold apple,perf-filter-time-constant
apple,perf-filter-time-constant2 apple,perf-integral-gain2 apple,perf-integral-min-clamp
apple,perf-proportional-gain2 apple,perf-tgt-utilization apple,ppm-filter-time-constant-ms
apple,ppm-ki apple,ppm-kp apple,pwr-min-duty-cycle""".split()

F_DRIVER, F_PHANDLE, F_INFO, F_FRAMEWORK = 1, 2, 4, 8

DIAGS = {
    1: "core mask assumed, not read from hardware",
    2: "iBoot version not in the firmware table",
    3: "the driver has no G13 ABI for this firmware",
    4: "static value differs from the ADT",
    5: "ADT counterpart of a static value is missing",
    6: "static reg base differs from the ADT",
    7: "no mailbox phandle given, mboxes left out",
    8: "no power-domain phandle given, power-domains left out",
    9: "fewer than 8 GPU cores",
    10: "UAT region smaller than the driver needs",
    11: "system-firmware-version missing",
    12: "value too long for the table",
}


def read_ground_truth(paths):
    nodes, cur = {}, None
    for line in (l for path in paths for l in open(path)):
        line = line.rstrip("\n")
        if line.startswith("== "):
            cur = nodes.setdefault(line[3:].strip(), {})
            continue
        parts = line.split(" ")
        if cur is None or len(parts) < 2:
            continue
        cur[parts[0]] = (int(parts[1]), parts[2] if len(parts) > 2 else "")
    return nodes


def run(harness, args):
    out = subprocess.run([harness, "dump"] + args, capture_output=True, text=True)
    props, diags, info, ret = [], [], {}, None
    for line in out.stdout.splitlines():
        f = line.split("|")
        if f[0] == "R":
            ret = int(f[1])
        elif f[0] == "I":
            info[f[1]] = f[2]
        elif f[0] == "D":
            diags.append((int(f[1]), f[2], int(f[3]), int(f[4])))
        elif f[0] == "P":
            props.append(dict(node=f[1], name=f[2], type=f[3], origin=f[4], flags=int(f[5]),
                              xcheck=f[6], roundtrip=f[7] == "1", len=int(f[8]), hex=f[9], iodt=f[10]))
    return ret, props, diags, info


def cells(hexstr):
    return [int(hexstr[i:i + 8], 16) for i in range(0, len(hexstr), 8)]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    harness, gt_paths = sys.argv[1], sys.argv[2:]
    gt = {node_path(k): v for k, v in read_ground_truth(gt_paths).items()}
    gpu = gt.get("gpu@206400000")
    if gpu is None:
        sys.exit("compare: no ./soc/gpu@206400000 node in %s" % " ".join(gt_paths))

    # phandle numbers are whatever dtc assigned; take them from the ground truth
    args = []
    if "memory-region" in gpu:
        args += ["--phandle-base", str(cells(gpu["memory-region"][1])[0])]
    if "mboxes" in gpu:
        args += ["--mbox", str(cells(gpu["mboxes"][1])[0])]
    if "power-domains" in gpu:
        args += ["--power", str(cells(gpu["power-domains"][1])[0])]

    # the core mask is a hardware register, not in the ADT: find the count the ground truth implies
    implied = []
    for n in range(1, 9):
        ret, props, _, _ = run(harness, args + ["--core-mask", str((1 << n) - 1)])
        if ret != 0:
            sys.exit("compare: the library refused the device tree (%s)" % ret)
        mine = {p["name"]: p["hex"] for p in props if p["node"] == "gpu@206400000"}
        if all(mine[k] == gpu.get(k, (0, ""))[1] for k in ("apple,core-leak-coef", "apple,sram-leak-coef")):
            implied.append(n)
    cores = implied[0] if len(implied) == 1 else 8
    ret, props, diags, info = run(harness, args + ["--core-mask", str((1 << cores) - 1)])

    print("m1-gpu-dt: library output against Linux on the same machine")
    print("  firmware detected: %s; GPU cores used: %d" % (info.get("firmware"), cores))
    if len(implied) == 1:
        print("  core count: the ground-truth leakage coefficients match %d cores and no other count" % cores)
    else:
        print("  core count: NOT determined by the ground truth (matching counts: %s); 8 used" % implied)
    print()

    counts = dict(identical=0, identical_ph=0, differs=0, expected=0, not_in_gt=0, not_produced=0)
    h, hashed, unhashable = 0x811C9DC5, 0, []
    fail = False
    seen = set()
    cur = None
    for p in props:
        if p["node"] != cur:
            cur = p["node"]
            print("== %s" % cur)
        gt_node = gt.get(p["node"])
        seen.add((p["node"], p["name"]))
        # the checksum the kext logs as "linux-comparable", computed from the ground-truth bytes
        if not p["flags"] & (F_PHANDLE | 0x10):
            if gt_node is not None and p["name"] in gt_node:
                length, hexstr = gt_node[p["name"]]
                h = fnv1a(h, p["node"].encode() + b"\0" + p["name"].encode() + b"\0" + bytes([length & 0xFF]))
                h = fnv1a(h, bytes.fromhex(hexstr))
                hashed += 1
            else:
                unhashable.append(p["node"] + "/" + p["name"])
        tag = "%-36s %-8s" % (p["name"], p["origin"])
        if gt_node is None:
            counts["not_in_gt"] += 1
            print("  not in ground truth  %s %s  (node not in the dump)" % (tag, p["hex"]))
        elif p["name"] not in gt_node:
            counts["not_in_gt"] += 1
            print("  not in ground truth  %s %s" % (tag, p["hex"]))
        elif gt_node[p["name"]] == (p["len"], p["hex"]):
            if p["flags"] & F_PHANDLE:
                counts["identical_ph"] += 1
                print("  identical*           %s" % tag)
            else:
                counts["identical"] += 1
                print("  identical            %s" % tag)
        elif (p["node"], p["name"]) in EXPECTED_DIFFERENCE:
            counts["expected"] += 1
            print("  differs (intended)   %s ours %d:%s  linux %d:%s" %
                  (tag, p["len"], p["hex"], gt_node[p["name"]][0], gt_node[p["name"]][1]))
        else:
            counts["differs"] += 1
            fail = True
            print("  DIFFERS              %s ours %d:%s  linux %d:%s" %
                  (tag, p["len"], p["hex"], gt_node[p["name"]][0], gt_node[p["name"]][1]))

    print("== in the ground truth, not produced")
    mine_nodes = {p["node"] for p in props}
    foreign = sorted(n for n in gt if n not in mine_nodes)
    for node in sorted(mine_nodes):
        for name, (length, hexstr) in sorted(gt.get(node, {}).items()):
            if (node, name) in seen:
                continue
            counts["not_produced"] += 1
            note = NOT_PRODUCED.get((node, name))
            if note is None and name == "name":
                note = "node name, not a property of the FDT"
            if note is None and node == "chosen":
                note = CHOSEN_NOTE
            if note is None:
                fail = True
                note = "UNEXPLAINED"
            shown = "" if node == "chosen" else " " + hexstr
            print("  not produced         %s/%s (%d bytes)%s: %s" % (node, name, length, shown, note))

    if foreign:
        print("  ground-truth nodes that are not the GPU's (other devices' reserved memory): %d" % len(foreign))
        for node in foreign:
            print("    %s" % node.split("@")[0])

    mine = {p["name"] for p in props if p["node"] == "gpu@206400000"}
    missing = [n for n in REQUIRED if n not in mine]
    if missing:
        fail = True
        print("\nREQUIRED BY THE DRIVER BUT NOT PRODUCED: %s" % " ".join(missing))

    xc = [p for p in props if p["xcheck"] != "-"]
    bad = [p["name"] for p in xc if p["xcheck"] != "match"]
    print("\nstatic values with an ADT counterpart: %d, of which equal to the ADT: %d%s" %
          (len(xc), len(xc) - len(bad), ("; NOT equal: " + " ".join(bad)) if bad else ""))

    print("library diagnostics: %s" % ("none" if not diags else ""))
    for code, text, a, b in diags:
        print("  [%d] %s %s (0x%x, 0x%x)" % (code, DIAGS.get(code, "?"), text, a, b))

    rt = [p for p in props if not p["roundtrip"]]
    print("\nvalues the unmodified sf_linux_of.c would hand the driver wrongly: %d" % len(rt))
    for p in rt:
        print("  %s/%s: FDT %s is stored as %s, which passes its string test" %
              (p["node"], p["name"], p["hex"], p["iodt"]))

    print("\nlinux-comparable checksum (fnv1a over %s properties, phandles and boot-time blobs left out):"
          % info.get("linux_count"))
    print("  library:      0x%s" % info.get("linux_checksum"))
    if unhashable:
        print("  ground truth: not computable, missing: %s" % " ".join(unhashable))
        fail = True
    else:
        print("  ground truth: 0x%08x over %d properties" % (h, hashed))
        if "%08x" % h != info.get("linux_checksum"):
            fail = True
            print("  CHECKSUMS DIFFER")

    print("\nsummary: %d identical, %d identical* (phandle numbers taken from the ground truth), "
          "%d differ, %d differ as intended, %d not in ground truth, %d ground-truth properties not produced" %
          (counts["identical"], counts["identical_ph"], counts["differs"], counts["expected"],
           counts["not_in_gt"], counts["not_produced"]))
    by_origin = {}
    for p in props:
        by_origin[p["origin"]] = by_origin.get(p["origin"], 0) + 1
    print("produced by origin: " + ", ".join("%s %d" % kv for kv in sorted(by_origin.items())))
    print("result: %s" % ("FAIL" if fail else "PASS"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
