#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Make the modules of the kernel crate selectable: every `pub mod X;` of
rust/kernel/lib.rs gains `#[cfg(sf_mod_X)]`, and every `pub mod X;` of the listed submodule
roots gains `#[cfg(sf_mod_<prefix>_X)]` (kept next to an existing #[cfg(CONFIG_...)], both
must hold). Purely mechanical, one added line per module, no line changed. Which modules are
built is then a list of `--cfg sf_mod_...` flags (lkpi/modules.nix), not an edit.

  gate.py <rust/kernel>       rewrites in place, prints the cfg names (without sf_mod_)
"""
import re
import sys

root = sys.argv[1]
# file, cfg prefix
FILES = [("lib.rs", ""), ("drm/mod.rs", "drm_"), ("soc/apple/mod.rs", "soc_apple_")]
names = []
for rel, prefix in FILES:
    path = f"{root}/{rel}"
    out = []
    for line in open(path).read().split("\n"):
        m = re.match(r"^pub mod (\w+);", line)
        if m:
            names.append(prefix + m.group(1))
            out.append(f"#[cfg(sf_mod_{prefix}{m.group(1)})]")
        out.append(line)
    open(path, "w").write("\n".join(out))
print(" ".join(names))
