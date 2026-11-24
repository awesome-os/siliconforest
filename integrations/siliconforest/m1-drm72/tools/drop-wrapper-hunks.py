#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""drop-wrapper-hunks.py <patch dir> <out dir>

Copies every *.patch, without the per-file sections that touch the Rust wrappers this ticket
measures unmodified (kernel/dma_fence.rs, kernel/drm/**). A patch's description is kept.
"""
import os
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
DROP = re.compile(r"^--- a/kernel/(dma_fence\.rs|drm/)")
for name in sorted(os.listdir(src)):
    if not name.endswith(".patch"):
        continue
    out, keep, dropped = [], True, []
    for line in open(os.path.join(src, name)):
        if line.startswith("--- "):
            keep = not DROP.match(line)
            if not keep:
                dropped.append(line.split()[1])
        if keep:
            out.append(line)
    open(os.path.join(dst, name), "w").write("".join(out))
    if dropped:
        print(f"{name}: dropped {' '.join(dropped)}")
