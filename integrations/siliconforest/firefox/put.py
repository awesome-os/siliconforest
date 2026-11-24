#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Copy a new private-prefix tree and verify all regular files BEFORE execution.

Call only after checking the coordination log. Screen/GPU use needs gui.lock.
The destination must not already exist; installed system paths are refused.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import tarfile
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--destination', required=True)
    ap.add_argument('--receipt', type=Path, required=True)
    args = ap.parse_args()
    dest = PurePosixPath(args.destination)
    if dest.parent == dest or '..' in dest.parts or not str(dest).startswith('/private/var/db/sffx/'):
        raise ValueError('Destination must be a new directory inside /private/var/db/sffx/')
    ssh = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=yes',
           '-o', 'HostKeyAlias=m1-siliconforest', '-o',
           'UserKnownHostsFile=' + str(Path.home() / '.ssh/known_hosts_m1_siliconforest'), 'root@10.1.1.211']
    records = {}
    symlinks = {}
    for p in sorted(args.source.rglob('*')):
        if p.is_symlink():
            symlinks[str(p.relative_to(args.source))] = str(p.readlink())
        elif p.is_file():
            with p.open('rb') as f:
                records[str(p.relative_to(args.source))] = hashlib.file_digest(f, 'sha256').hexdigest()
    if not records:
        raise ValueError('Source has no regular files')
    with tempfile.TemporaryFile() as stream:
        with tarfile.open(fileobj=stream, mode='w') as tar:
            for p in sorted(args.source.iterdir()):
                tar.add(p, arcname=p.name)
        stream.seek(0)
        command = 'test ! -e {p} && mkdir -p {p} && tar -C {p} -xf -'.format(p=shlex.quote(str(dest)))
        subprocess.run(ssh + [command], stdin=stream, check=True, timeout=180)
    remote = '''import hashlib,json,sys,os
from pathlib import Path
assert os.uname().sysname == 'Darwin'
root=Path(sys.argv[1]); expected=json.load(sys.stdin); actual={}
for name,want in expected['sha256'].items():
 with (root/name).open('rb') as f: got=hashlib.sha256(f.read()).hexdigest()
 if got!=want:raise RuntimeError('Hash mismatch: '+name)
 actual[name]=got
links={}
for name,want in expected['symlinks'].items():
 path=root/name
 if not path.is_symlink() or os.readlink(path)!=want:raise RuntimeError('Symlink mismatch: '+name)
 links[name]=want
receipt=dict(destination=str(root),cold_before_execution=True,sha256=actual,symlinks=links)
(root/'cold-verified.json').write_text(json.dumps(receipt,indent=2)+'\\n')
print(json.dumps(receipt,indent=2))
'''
    result = subprocess.run(ssh + ['python3 -c ' + shlex.quote(remote) + ' ' + shlex.quote(str(dest))],
                            input=json.dumps(dict(sha256=records,symlinks=symlinks)), capture_output=True, text=True, check=True, timeout=60)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(result.stdout)
    print('Cold verified', len(records), 'files at', dest)


if __name__ == '__main__':
    main()
