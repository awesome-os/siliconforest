#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read only snapshot of open system providers for sf_gapscan (no Apple bundles)."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tarfile

REMOTE = r'''
import os,sys,tarfile
from pathlib import Path
magic={bytes.fromhex(s) for s in ['cffaedfe','cafebabe','feedfacf']}
assert os.uname().sysname == 'Darwin', 'Target is not SiliconForest'
with tarfile.open(fileobj=sys.stdout.buffer,mode='w|gz',dereference=False) as out:
 for base in ['/System/Library/Frameworks','/System/Library/PrivateFrameworks','/usr/lib','/usr/local/lib']:
  for directory,dirs,files in os.walk(base,followlinks=False):
   dirs[:]=[d for d in dirs if not any(x in d for x in ['.pre-','.old','.before-','Headers','Resources'])]
   for name in dirs+files:
    if any(x in name for x in ['.pre-','.old','.before-']):continue
    p=Path(directory)/name
    if p.is_symlink():out.add(p,arcname=str(p).lstrip('/'),recursive=False)
    elif p.is_file():
     with p.open('rb') as f:head=f.read(4)
     if head in magic:out.add(p,arcname=str(p).lstrip('/'),recursive=False)
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host', default='root@10.1.1.211')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--root', action='append', help='provider directories; default includes /lib')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    ssh = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o',
           'StrictHostKeyChecking=yes', '-o', 'HostKeyAlias=m1-siliconforest', '-o',
           'UserKnownHostsFile=' + str(Path.home() / '.ssh/known_hosts_m1_siliconforest'), args.host]
    archive = args.out / 'providers.tar.gz'
    roots = args.root or ['/System/Library/Frameworks', '/System/Library/PrivateFrameworks',
                          '/usr/lib', '/usr/local/lib', '/lib']
    remote = REMOTE.replace("['/System/Library/Frameworks','/System/Library/PrivateFrameworks','/usr/lib','/usr/local/lib']",
                            repr(roots))
    with archive.open('wb') as f:
        subprocess.run(ssh + ['python3 -c ' + shlex.quote(remote)], stdout=f, check=True, timeout=180)
    records, links = [], []
    with tarfile.open(archive) as tar:
        for member in tar:
            if member.name.startswith('/') or '..' in Path(member.name).parts:
                raise ValueError(member.name)
            path = args.out / member.name
            if member.issym():
                links.append((path, member.linkname))
                continue
            if not member.isfile():
                raise ValueError(member.name)
            path.parent.mkdir(parents=True, exist_ok=True)
            data = tar.extractfile(member).read()
            path.write_bytes(data)
            records.append(dict(path=member.name, sha256=hashlib.sha256(data).hexdigest(), size=len(data)))
    for path, target in links:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(target)
    (args.out / 'inventory.json').write_text(json.dumps(records, indent=2) + '\n')
    archive.unlink()
    print(len(records), 'providers;', sum(r['size'] for r in records), 'bytes')
    print('Loaded-image readbacks are for import scanning, not pristine-byte verification.')


if __name__ == '__main__':
    main()
