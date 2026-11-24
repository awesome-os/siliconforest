#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""rust/kernel/error.rs declares one constant per Linux errno. LinuxKPI on SiliconForest uses
XNU's errno numbering and has no value for the Linux-only ones; give those declarations
`#[cfg(not(sf_lkpi))]`. Also ENOIOCTLCMD, which LinuxKPI defines as FreeBSD's ENOIOCTL (-3,
a negative "errno") and so cannot be an `Error`. Mechanical: one added line per errno.

  gate_errnos.py <error.rs> <bindings_generated.rs>     rewrites error.rs, prints the names
"""
import re
import sys

err, gen = sys.argv[1], open(sys.argv[2]).read()
have = {m.group(1): m.group(2) for m in re.finditer(r"pub const (E[A-Z0-9]+): (\w+) = (-?\d+);", gen)}
out, gated = [], []
for line in open(err).read().split("\n"):
    m = re.match(r"(\s*)declare_err!\((E[A-Z0-9]+),", line)
    if m and (m.group(2) not in have or have[m.group(2)] != "u32"):
        out.append(f"{m.group(1)}#[cfg(not(sf_lkpi))]")
        gated.append(m.group(2))
    out.append(line)
open(err, "w").write("\n".join(out))
print(" ".join(gated))
