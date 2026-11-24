#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Reduce a Linux rust/ helper header or helper C set to what SiliconForest's LinuxKPI can parse.

  subset.py headers <helper.h> <out.h> <out.tsv> -- <clang> <cflags...>
      Tries every `#include <...>` of the Linux bindings/uapi helper header on its own, then
      every `const ... = ...;` line on top of the includes that parsed. Writes the reduced
      header and a table: status, header, where it resolved, first error.

  subset.py helpers <helpers dir> <overlay dir> <out.c> <out.tsv> -- <clang> <cflags...>
      Compiles each rust/helpers/*.c on its own (as helpers.c would include it). A file of the
      same name in the overlay dir replaces the Linux one. Writes a helpers.c that includes the
      ones that compile, and a table.

Nothing is guessed: a header is `absent` only when clang reports that very header missing,
`fails` when it exists but does not parse, `ok` when a file including only it parses.
"""
import os
import re
import subprocess
import sys
import tempfile


def run(clang, cflags, source, extra=()):
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(source)
        path = f.name
    try:
        p = subprocess.run([clang, *cflags, *extra, "-fsyntax-only", "-H", path],
                           capture_output=True, text=True)
    finally:
        os.unlink(path)
    return p.returncode, p.stderr


def first_error(stderr):
    errs = [l for l in stderr.splitlines() if " error: " in l]
    if not errs:
        return "", 0
    msg = errs[0].split(" error: ", 1)[1]
    where = errs[0].split(" error: ", 1)[0]
    m = re.search(r"([^/ ]+/[^/ ]+/[^/: ]+):(\d+)", where)
    loc = f" ({m.group(1)}:{m.group(2)})" if m else ""
    return (msg + loc)[:160], len(errs)


def classify_dir(path):
    for needle, name in (
        ("/fbsdkpi/drm/include/", "pd drm"),
        ("/linuxkpi/bsd/include/", "drm-kmod linuxkpi/bsd"),
        ("/sf/lkpi-override/", "pd lkpi-override"),
        ("/linuxkpi/dummy/include/", "linuxkpi dummy"),
        ("/linuxkpi/common/include/", "linuxkpi common"),
        ("/sf/fbsd-extra/", "pd fbsd-extra"),
        ("drm-kmod-src/include/", "drm-kmod include"),
        ("drm-kmod-src/drivers/", "drm-kmod drivers"),
        ("/haiku/", "haiku compat"),
        ("/overlay/", "this directory (overlay)"),
    ):
        if needle in path:
            return name
    return os.path.dirname(path)


def resolved(stderr, header):
    for l in stderr.splitlines():
        if l.startswith(". ") and l.endswith("/" + header.lstrip("./")):
            path = l[2:]
            try:
                real = sum(1 for x in open(path, errors="replace")
                           if x.strip() and not x.lstrip().startswith(("//", "/*", "*", "#ifndef", "#define", "#endif", "#pragma")))
            except OSError:
                real = -1
            return classify_dir(path) + (" (empty stub)" if real == 0 else "")
    return ""


def headers(helper, out_h, out_tsv, clang, cflags):
    lines = open(helper).read().split("\n")
    rows, keep = [], []
    ok_includes = []
    # pass 1: includes
    inc_status = {}
    for l in lines:
        m = re.match(r"#include <(.*)>", l)
        if not m or m.group(1) in inc_status:
            continue
        h = m.group(1)
        rc, err = run(clang, cflags, f"#include <{h}>\n")
        if rc == 0:
            inc_status[h] = ("ok", resolved(err, h), "")
            ok_includes.append(h)
        else:
            msg, n = first_error(err)
            if f"'{h}' file not found" in msg and ".c:1" in [x for x in err.splitlines() if " error: " in x][0]:
                inc_status[h] = ("absent", "", "")
            else:
                inc_status[h] = ("fails", resolved(err, h), f"{msg} [{n} errors]")
    prelude = "".join(f"#include <{h}>\n" for h in ok_includes)
    # pass 2: statements; `const T NAME = EXPR;` possibly continued over lines
    i = 0
    while i < len(lines):
        l = lines[i]
        m = re.match(r"#include <(.*)>", l)
        if m:
            st = inc_status[m.group(1)][0]
            keep.append(l if st == "ok" else f"// pd: {st}: {l}")
        elif re.match(r"const \w+ \w+ =", l):
            stmt = l
            while not stmt.rstrip().endswith(";"):
                i += 1
                stmt += "\n" + lines[i]
            name = re.match(r"const \w+ (\w+)", stmt).group(1)
            rc, err = run(clang, cflags, prelude + stmt + "\n")
            if rc == 0:
                keep.append(stmt)
                rows.append(("ok", "const " + name, "", ""))
            else:
                msg, n = first_error(err)
                keep.append("// pd: fails: " + stmt.replace("\n", "\n// "))
                rows.append(("fails", "const " + name, "", msg))
        else:
            keep.append(l)
        i += 1
    open(out_h, "w").write("\n".join(keep))
    with open(out_tsv, "w") as f:
        for h, (st, where, why) in inc_status.items():
            f.write(f"{st}\t{h}\t{where}\t{why}\n")
        for r in rows:
            f.write("\t".join(r) + "\n")


def helpers(hdir, overlay, out_c, out_tsv, clang, cflags):
    top = open(os.path.join(hdir, "helpers.c")).read()
    names = re.findall(r'#include "(.*\.c)"', top)
    extra = sorted(f for f in os.listdir(overlay) if f.endswith(".c") and f not in names) if os.path.isdir(overlay) else []
    pre = "#include <linux/compiler_types.h>\n#define __rust_helper\n"
    out = ["// generated by subset.py: the rust/helpers files that compile against SiliconForest's LinuxKPI",
           "#include <linux/compiler_types.h>",
           "#ifdef __BINDGEN__", "#define __rust_helper", "#else",
           # not `static inline` here: the helpers are real functions the Rust object calls
           "#define __rust_helper", "#endif"]
    rows = []
    for n in names + extra:
        ov = os.path.join(overlay, n)
        src, origin = (ov, "overlay") if os.path.exists(ov) else (os.path.join(hdir, n), "linux")
        text = open(src).read()
        if origin == "overlay" and "SF_HELPER_EMPTY" in text:
            rows.append(("dropped", n, origin, re.search(r"SF_HELPER_EMPTY:?\s*(.*)", text).group(1)))
            continue
        rc, err = run(clang, cflags, pre + f'#include "{src}"\n',
                      ("-Wno-missing-prototypes", "-I" + hdir))
        nfn = len(re.findall(r"\brust_helper_\w+\s*\(", text))
        if rc == 0:
            out.append(f'#include "{src}"')
            rows.append(("ok", n, origin, f"{nfn} helpers"))
        else:
            msg, cnt = first_error(err)
            out.append(f"// pd: fails: {n}: {msg}")
            rows.append(("fails", n, origin, f"{msg} [{cnt} errors, {nfn} helpers]"))
    open(out_c, "w").write("\n".join(out) + "\n")
    with open(out_tsv, "w") as f:
        for r in rows:
            f.write("\t".join(r) + "\n")


def main():
    args = sys.argv[1:]
    cut = args.index("--")
    mine, clang, cflags = args[:cut], args[cut + 1], args[cut + 2:]
    if mine[0] == "headers":
        headers(mine[1], mine[2], mine[3], clang, cflags)
    elif mine[0] == "helpers":
        helpers(mine[1], mine[2], mine[3], mine[4], clang, cflags)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
