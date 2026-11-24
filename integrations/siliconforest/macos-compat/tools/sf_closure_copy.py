#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Copy what dyld loads for some binaries out of a root directory, laid out as in the root.

  sf_closure_copy.py --root <root> --out <dir> <logical path of a binary> ...

For each binary: itself and every library of its load closure (../../homebrew/sf_gapscan.py
walks it as dyld does). A library inside a framework brings the whole framework bundle
(resources included); every symbolic link on the way to a file is recreated, so each name a
load command uses exists in the copy. Libraries the root does not have are reported and left
to whoever uses the copy (the base system provides libSystem and friends).
"""
import argparse
import os
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'homebrew'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sf_gapscan  # noqa: E402


def copy_path(root, out, logical, done):
    """Recreate `logical` under out: links on the way as links, the file as a file."""
    parts = Path(logical.lstrip('/')).parts
    for i in range(len(parts)):
        rel = Path(*parts[:i + 1])
        src = root / rel
        dst = out / rel
        if src.is_symlink():
            target = os.readlink(src)
            if not (dst.is_symlink() or dst.exists()):
                dst.parent.mkdir(parents=True, exist_ok=True)
                os.symlink(target, dst)
            base = target if target.startswith('/') else os.path.normpath(os.path.join('/', *parts[:i], target))
            rest = '/'.join(parts[i + 1:])
            return copy_path(root, out, base + ('/' + rest if rest else ''), done)
    src, dst = root / Path(*parts), out / Path(*parts)
    if str(dst) in done or not src.is_file():
        return
    done.add(str(dst))
    dst.parent.mkdir(parents=True, exist_ok=True)
    put(src, dst)


def tree(src, dst):
    """Copy a directory: links as links, files with put(); what is there already stays."""
    for dirpath, dirs, files in os.walk(src):
        d = dst / Path(dirpath).relative_to(src)
        d.mkdir(parents=True, exist_ok=True)
        os.chmod(d, 0o755)
        for name in list(dirs) + files:
            s = Path(dirpath) / name
            if s.is_symlink():
                if not os.path.lexists(d / name):
                    os.symlink(os.readlink(s), d / name)
            elif s.is_file():
                put(s, d / name)
        dirs[:] = [x for x in dirs if not (Path(dirpath) / x).is_symlink()]


def put(src, dst):
    """Copy a file unless it is there already; the copy is writable by its owner (sources may be read-only)."""
    if os.path.lexists(dst):
        return dst
    shutil.copyfile(src, dst)
    os.chmod(dst, 0o755 if os.access(src, os.X_OK) else 0o644)
    return dst


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('binaries', nargs='+')
    a = ap.parse_args()
    root, out = Path(a.root), Path(a.out)
    r = sf_gapscan.Root(root)
    done, bundles, absent = set(), set(), set()
    todo = list(a.binaries)
    seen = set()
    while todo:
        b = todo.pop()
        h = r.resolve(b)
        if h is None or r.macho(h) is None:
            sys.exit('%s: not a Mach-O in %s' % (b, root))
        for (kind, lib, _sym, weak) in sf_gapscan.closure_gaps(r, b, h):
            if kind == 'library' and not weak:
                absent.add(lib)
        for logical, _host in sf_gapscan.closure_files(r, b, h):
            if logical in seen:
                continue
            seen.add(logical)
            copy_path(root, out, logical, done)
            if '.framework/' in logical:
                bundle = logical[:logical.index('.framework/') + len('.framework')]
                if bundle not in bundles:
                    bundles.add(bundle)
                    src = root / bundle.lstrip('/')
                    if src.is_dir() and not src.is_symlink():
                        tree(src, out / bundle.lstrip('/'))
                        # every binary of the bundle (other versions, nested frameworks) with its closure
                        for dirpath, _d, files in os.walk(src):
                            for f in files:
                                p = Path(dirpath) / f
                                if not p.is_symlink() and r.macho(p) is not None and r.macho(p).filetype in (2, 6, 8):
                                    todo.append('/' + str(p.relative_to(root)))
    for lib in sorted(absent):
        print('not in the root (the base system provides it): %s' % lib)
    print('%d files, %d framework bundles' % (sum(1 for _ in out.rglob('*') if _.is_file() and not _.is_symlink()), len(bundles)))


if __name__ == '__main__':
    main()
