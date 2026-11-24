#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compares the display tree dispdt builds (build/dispdt_dump) with a dump of the live Linux
device tree (private/linux-dt/display-nodes.txt, never committed).

  compare_disp.py <dispdt_dump> <display-nodes.txt>

Phandle numbers differ by construction: every phandle-bearing cell is translated through the
`phandle` properties of corresponding nodes before comparing. Prints one line per property and
fails on a difference that is not in EXPLAINED."""
import subprocess
import sys

# property -> (cells per entry, indices of phandle cells); None: every cell is a phandle
PHANDLES = {"phandle": None, "mboxes": None, "clocks": None, "memory-region": None, "panel": None,
            "post-init-providers": None, "iommus": (6, (0,)), "apple,bw-scratch": (4, (0,)),
            "apple,bw-doorbell": (3, (0,)), "iommu-addresses": (5, (0,))}

# (node suffix or "*", property) -> why Linux has it and we do not (or differ)
EXPLAINED = {
    ("*", "name"): "implicit: the registry entry's own name",
    ("*", "interrupts"): "not consumed: interrupts come from Apple's device tree through the RTKit port (mailbox) and nothing else in the T8103 driver path takes one",
    ("*", "interrupt-parent"): "as interrupts",
    ("*", "interrupt-names"): "as interrupts",
    ("*", "power-domains"): "not consumed: no genpd on this layer; DISP0_CPU0 is on (boots dc04..dc12) and the driver's own lookups of the property are in dcpext_scanout.c only",
    ("*", "resets"): "not consumed: no reset_control call in the driver; Linux's mailbox.c does not take one either",
    ("display-subsystem", "iommus"): "Linux lists dispext0's DART as a second entry; dcpext is not published here",
}


def parse(lines):
    nodes, cur = {}, None
    for line in lines:
        line = line.rstrip("\n")
        if line.startswith("####"):
            break
        if line.startswith("== "):
            path = line[3:]
            # Linux hangs the SoC devices below soc/; here they are below the root
            key = "/".join(c for c in path.split("/") if c != "soc")
            cur = nodes.setdefault(key, {})
        elif cur is not None and line:
            name, _, val = line.partition(" ")
            cur[name] = val.strip()
    return nodes


def cells(h):
    return [h[i:i + 8] for i in range(0, len(h), 8)]


def main():
    ours = parse(subprocess.run(sys.argv[1:-1], capture_output=True, text=True, check=True).stdout.splitlines())
    linux = parse(open(sys.argv[-1]))
    # our phandle -> Linux's, through nodes present in both
    ph = {ours[n]["phandle"]: linux[n]["phandle"] for n in ours if n in linux and "phandle" in ours[n] and "phandle" in linux[n]}
    bad = same = explained = 0
    for node in sorted(set(ours) | set(linux)):
        if node not in ours or node not in linux:
            print("%-9s node %s only in %s" % ("DIFFERS", node, "ours" if node in ours else "linux"))
            bad += 1
            continue
        for prop in sorted(set(ours[node]) | set(linux[node])):
            o, l = ours[node].get(prop), linux[node].get(prop)
            if o is not None and prop in PHANDLES:
                c = cells(o)
                spec = PHANDLES[prop]
                idx = range(len(c)) if spec is None else [e * spec[0] + k for e in range(len(c) // spec[0]) for k in spec[1]]
                for i in idx:
                    c[i] = ph.get(c[i], "?" + c[i])
                o = "".join(c)
            why = EXPLAINED.get((node, prop)) or EXPLAINED.get((node.split("/")[-1].split("@")[0], prop)) or EXPLAINED.get(("*", prop))
            if o == l:
                same += 1
                print("identical %s %s" % (node, prop))
            elif why:
                explained += 1
                print("explained %s %s: %s" % (node, prop, why))
            else:
                bad += 1
                print("DIFFERS   %s %s\n   ours  %s\n   linux %s" % (node, prop, o, l))
    print("summary: %d identical, %d explained, %d unexplained differences" % (same, explained, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
