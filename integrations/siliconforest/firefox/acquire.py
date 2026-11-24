#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Download an official Mozilla release, verify SHA-512, and unpack privately."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
from urllib.parse import quote
from urllib.request import urlopen

LAB = Path(__file__).resolve().parents[3]


def download(url, path):
    tmp = path.with_name(path.name + '.part')
    with urlopen(url, timeout=60) as src, tmp.open('wb') as dst:
        shutil.copyfileobj(src, dst)
    tmp.replace(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--version', help='default: current release from Mozilla product-details')
    ap.add_argument('--7zz', dest='seven', default='7zz')
    args = ap.parse_args()
    with urlopen('https://product-details.mozilla.org/1.0/firefox_versions.json', timeout=30) as f:
        versions = json.load(f)
    version = args.version or versions['LATEST_FIREFOX_VERSION']
    dest = LAB / 'private/mac-apps/third-party' / ('firefox-' + version)
    dest.mkdir(parents=True, exist_ok=True)
    (dest / 'versions.json').write_text(json.dumps(versions, indent=2) + '\n')
    base = 'https://archive.mozilla.org/pub/firefox/releases/' + quote(version) + '/'
    download(base + 'SHA512SUMS', dest / 'SHA512SUMS')
    relative = 'mac/en-US/Firefox ' + version + '.dmg'
    entries = [s.split(None, 1) for s in (dest / 'SHA512SUMS').read_text().splitlines()]
    checksums = [h for h, name in entries if name.strip() == relative]
    if len(checksums) != 1:
        raise RuntimeError('Expected exactly one published mac/en-US SHA-512')
    dmg = dest / ('Firefox-' + version + '.dmg')
    if not dmg.exists():
        download(base + quote(relative), dmg)
    digest = hashlib.file_digest(dmg.open('rb'), 'sha512').hexdigest()
    if digest != checksums[0]:
        raise RuntimeError('SHA-512 mismatch; refusing extraction')
    (dest / 'VERIFIED.sha512').write_text(digest + '  ' + dmg.name + '\n')
    unpacked = dest / 'unpacked'
    bundle = unpacked / 'Firefox/Firefox.app'
    if not bundle.exists():
        subprocess.run([args.seven, 'x', '-y', '-o' + str(unpacked), str(dmg)], check=True)
    with (bundle / 'Contents/Info.plist').open('rb') as f:
        info = plistlib.load(f)
    if info['CFBundleShortVersionString'] != version:
        raise RuntimeError('Bundle version differs from selected release')
    # All regular files, including nested helpers; symlinks remain bundle symlinks.
    records = {str(p.relative_to(bundle)): hashlib.file_digest(p.open('rb'), 'sha256').hexdigest()
               for p in sorted(bundle.rglob('*')) if p.is_file() and not p.is_symlink()}
    (dest / 'bundle-sha256.json').write_text(json.dumps(records, indent=2) + '\n')
    print(json.dumps(dict(version=version, bundle=str(bundle), files=len(records), sha512=digest)))


if __name__ == '__main__':
    main()
