#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The store path a recipe under integrations/siliconforest builds to, without evaluating it
again when nothing it reads has changed.

Usage: bs_nix_path.py <directory below integrations/siliconforest> <file.nix> [nix-build arguments]

`nix-build --no-out-link` on an already built recipe still takes 20 to 70 s here (the pinned
SiliconForest flake is evaluated every time), and the desktop wrappers do it three or four
times before each boot. The answer is kept in runs/nix-path-cache, keyed by the call and by
the content of everything the recipe names: the .nix file, every relative path in it (a .nix
file is followed, another file is hashed, a directory is hashed whole), the pinned Linux
tree's commit and tracked changes, and absolute lab paths under runs/. The flake and nixpkgs
are pinned by revision in rebase/pd.nix, which the recipes import (SF_PD, when set, is part
of the key). A cached path is used only while it exists
in the store. BS_NIX_CACHE=0 always evaluates; BS_NIX_DEBUG=1 lists the inputs.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
BASE = ROOT / 'integrations' / 'siliconforest'
LINUX = ROOT / 'sources' / 'gpu-plan' / 'linux-aurora'
RELATIVE = re.compile(r'(?<![A-Za-z0-9_$}/.-])(\.\.?(?:/[A-Za-z0-9._+-]+)+)')
ABSOLUTE = re.compile(r'/home/chrisk/src/darwin-lab/((?:runs|artifacts|private)/[A-Za-z0-9._/+-]+)')


def build(directory, name, arguments):
    env = dict(os.environ, PATH=str(Path.home() / '.nix-profile/bin') + ':' + os.environ['PATH'])
    done = subprocess.run(['nix-build', '--extra-experimental-features', 'nix-command',
                           '--extra-experimental-features', 'flakes', '--no-out-link', name] + arguments,
                          cwd=BASE / directory, capture_output=True, text=True, env=env)
    lines = done.stdout.strip().splitlines()
    if done.returncode or not lines or not Path(lines[-1]).exists():
        sys.stderr.write(done.stderr[-2000:])
        sys.exit('nix-build of %s/%s failed' % (directory, name))
    return lines[-1]


def inputs(start):
    """Every file the recipe can read, by following the paths its .nix files name."""
    files, seen, todo, linux = set(), set(), [start], False
    while todo:
        nix = todo.pop()
        if nix in seen:
            continue
        seen.add(nix)
        files.add(nix)
        # comments name other files in prose; only code is read for paths
        text = re.sub(r'(?m)^\s*#.*$', '', nix.read_text(errors='replace'))
        linux = linux or 'linux-aurora' in text
        for found in ABSOLUTE.findall(text):
            target = ROOT / found
            files.update(p for p in ([target] if target.is_file() else target.rglob('*')) if p.is_file())
        for found in RELATIVE.findall(text):
            target = (nix.parent / found).resolve()
            if not target.is_relative_to(ROOT) or not target.exists():
                continue
            if target.is_dir():
                if target in (ROOT, BASE, ROOT / 'integrations'):
                    sys.exit('%s names %s: too wide to key a cache on' % (nix, target))
                for path in target.rglob('*'):
                    if path.is_file() and '__pycache__' not in path.parts:
                        if path.suffix == '.nix':
                            todo.append(path)
                        files.add(path)
            elif target.suffix == '.nix':
                todo.append(target)
            else:
                files.add(target)
    return sorted(files), linux


def main(argv):
    directory, name, arguments = argv[0], argv[1], argv[2:]
    if os.environ.get('BS_NIX_CACHE', '1') != '1':
        print(build(directory, name, arguments))
        return
    files, linux = inputs((BASE / directory / name).resolve(strict=True))
    state = hashlib.sha256()
    for path in files:
        state.update(str(path.relative_to(ROOT)).encode() + b'\0' + hashlib.sha256(path.read_bytes()).digest())
    if linux:
        for command in (['rev-parse', 'HEAD'], ['diff', 'HEAD']):
            state.update(hashlib.sha256(subprocess.run(['git', '-C', str(LINUX)] + command, capture_output=True).stdout).digest())
    state.update('\0'.join([directory, name] + arguments).encode())
    # The source switch of rebase/pd.nix selects another SiliconForest tree for the same recipe
    # text. Part of the key only when set, so the keys of the default (the pin) are unchanged.
    if os.environ.get('SF_PD'):
        state.update(b'\0SF_PD=' + os.environ['SF_PD'].encode())
    if os.environ.get('BS_NIX_DEBUG'):
        sys.stderr.write('%d input files, linux tree %s\n' % (len(files), linux))
        sys.stderr.write(''.join('  %s\n' % p.relative_to(ROOT) for p in files[:400]))
    cache = ROOT / 'runs' / 'nix-path-cache' / state.hexdigest()[:40]
    if cache.is_file():
        out = cache.read_text().strip()
        if out and Path(out).exists():
            print(out)
            return
    out = build(directory, name, arguments)
    cache.parent.mkdir(parents=True, exist_ok=True)
    temporary = cache.with_suffix('.%d' % os.getpid())
    temporary.write_text(out + '\n')
    temporary.replace(cache)
    print(out)


if __name__ == '__main__':
    main(sys.argv[1:])
