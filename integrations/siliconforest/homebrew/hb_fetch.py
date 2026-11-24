#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fetch Homebrew bottles (prebuilt arm64 macOS packages) for a list of formulae and their
run-time dependency closure, the way `brew` does, without `brew`.

  formulae.brew.sh/api/formula.json   every formula: version, dependencies, bottle blobs per tag
  ghcr.io/v2/homebrew/core/<name>/blobs/sha256:<digest>
                                      the bottle (a gzip tarball <name>/<version>/...), fetched
                                      with an anonymous bearer token from ghcr.io/token

Everything lands under runs/homebrew/ (never committed; bottles are fetched by the user at run
time and are not redistributed by this repository):

  api/formula.json                 the index (refetched with --refresh)
  bottles/<tag>/<name>--<version>.tar.gz
  kegs/<tag>/<name>/<version>/...  the unpacked bottle, untouched (placeholders still in place)
  closure-<tag>.json               what was asked for, the closure, sizes, digests

Requests carry a generic User-Agent and nothing that identifies the person.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

LAB = Path(__file__).resolve().parents[3]
RUNS = LAB / 'runs/homebrew'
UA = {'User-Agent': 'curl/8.0'}
# The order brew itself falls back in on an arm64 Mac that reports macOS 26: the system's own
# tag, then older ones (a bottle built for an older macOS runs on a newer one), then `all`
# (bottles without machine code).
TAG_ORDER = ['arm64_tahoe', 'arm64_sequoia', 'arm64_sonoma', 'arm64_ventura', 'arm64_monterey', 'arm64_big_sur', 'all']


def get(url, headers=None, binary=True):
    req = urllib.request.Request(url, headers=dict(UA, **(headers or {})))
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read() if binary else r.read().decode()


def index(refresh=False):
    path = RUNS / 'api/formula.json'
    if refresh or not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(get('https://formulae.brew.sh/api/formula.json'))
    out = {}
    for f in json.loads(path.read_text()):
        out[f['name']] = f
        for alias in f.get('aliases', []) + f.get('oldnames', []):
            out.setdefault(alias, f)
    return out


def pkg_version(f):
    v = f['versions']['stable']
    return v + ('_%d' % f['revision'] if f.get('revision') else '')


def bottle_for(f, tag):
    files = (f.get('bottle') or {}).get('stable', {}).get('files', {})
    order = TAG_ORDER[TAG_ORDER.index(tag):] if tag in TAG_ORDER else [tag] + TAG_ORDER
    for t in order:
        if t in files:
            return t, files[t]
    return None, None


def closure(names, idx):
    """Run-time closure as brew computes it for a pour on macOS: `dependencies` only.
    `uses_from_macos` entries are NOT installed on macOS (the system provides them); they are
    recorded so the survey can say which system libraries a formula expects."""
    seen, order = {}, []

    def visit(n, via):
        f = idx.get(n)
        if f is None:
            raise SystemExit('unknown formula %s (needed by %s)' % (n, via))
        if f['name'] in seen:
            return
        seen[f['name']] = f
        for d in f['dependencies']:
            visit(d, f['name'])
        order.append(f['name'])
    for n in names:
        visit(n, 'request')
    return order


def fetch(f, tag, token_cache):
    name = f['name']
    got, b = bottle_for(f, tag)
    if b is None:
        return dict(name=name, error='no bottle for %s or older' % tag)
    version = pkg_version(f)
    tarball = RUNS / 'bottles' / tag / ('%s--%s.tar.gz' % (name, version))
    keg = RUNS / 'kegs' / tag / name
    info = dict(name=name, version=version, bottle_tag=got, sha256=b['sha256'], cellar=b.get('cellar'),
                rebuild=f['bottle']['stable'].get('rebuild', 0))
    if not tarball.exists() or hashlib.file_digest(open(tarball, 'rb'), 'sha256').hexdigest() != b['sha256']:
        tarball.parent.mkdir(parents=True, exist_ok=True)
        repo = b['url'].split('/v2/', 1)[1].split('/blobs/', 1)[0]
        if repo not in token_cache:
            token_cache[repo] = json.loads(get('https://ghcr.io/token?service=ghcr.io&scope=repository:%s:pull' % repo))['token']
        tmp = tarball.with_suffix('.part')
        subprocess.run(['curl', '-fsSL', '--retry', '3', '-A', UA['User-Agent'], '-H', 'Authorization: Bearer ' + token_cache[repo],
                        '-o', str(tmp), b['url']], check=True)
        if hashlib.file_digest(open(tmp, 'rb'), 'sha256').hexdigest() != b['sha256']:
            tmp.unlink()
            return dict(info, error='sha256 mismatch')
        tmp.rename(tarball)
        shutil.rmtree(keg, ignore_errors=True)
    info['bytes'] = tarball.stat().st_size
    if not (keg / version).is_dir():
        shutil.rmtree(keg, ignore_errors=True)
        keg.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(tarball) as t:
            members = [m for m in t.getmembers() if m.name.split('/')[0] == name]
            t.extractall(keg.parent, members=members, filter='tar')
        subprocess.run(['chmod', '-R', 'u+rwX', str(keg)], check=True)
    return info


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('formulae', nargs='*')
    p.add_argument('--list', type=Path, help='file with one formula per line (# comments)')
    p.add_argument('--tag', default='arm64_tahoe')
    p.add_argument('--refresh', action='store_true')
    p.add_argument('--no-deps', action='store_true')
    a = p.parse_args()
    names = list(a.formulae)
    if a.list:
        names += [l.split('#')[0].strip() for l in a.list.read_text().splitlines() if l.split('#')[0].strip()]
    idx = index(a.refresh)
    order = [idx[n]['name'] for n in names] if a.no_deps else closure(names, idx)
    tokens, out = {}, []
    for i, n in enumerate(order):
        f = idx[n]
        r = fetch(f, a.tag, tokens)
        r.update(requested=n in {idx[x]['name'] for x in names}, dependencies=f['dependencies'],
                 uses_from_macos=[u if isinstance(u, str) else next(iter(u)) + ':' + str(next(iter(u.values()))) for u in f.get('uses_from_macos', [])],
                 keg_only=f.get('keg_only', False), post_install_defined=f.get('post_install_defined'))
        out.append(r)
        print('%3d/%d %-28s %-14s %s' % (i + 1, len(order), n, r.get('bottle_tag', '-'), r.get('error') or '%.1f MB' % (r['bytes'] / 1e6)), flush=True)
    path = RUNS / ('closure-%s.json' % a.tag)
    known = {r['name']: r for r in json.loads(path.read_text())} if path.exists() else {}
    for r in out:
        r['requested'] = r['requested'] or known.get(r['name'], {}).get('requested', False)
        known[r['name']] = r
    path.write_text(json.dumps(sorted(known.values(), key=lambda r: r['name']), indent=1) + '\n')
    return 1 if any('error' in r for r in out) else 0


if __name__ == '__main__':
    sys.exit(main())
