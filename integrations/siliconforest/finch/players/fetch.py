#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fetch explicitly requested third-party players; verify pinned publisher hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--destination',type=Path,default=Path('private/mac-apps/third-party'))
    ap.add_argument('--execute',action='store_true')
    args=ap.parse_args()
    rows=json.loads(Path(__file__).with_name('downloads.json').read_text())
    if not args.execute:
        print(json.dumps(rows,indent=2));return
    args.destination.mkdir(parents=True,exist_ok=True)
    for row in rows:
        output=args.destination/row['name']
        if not output.exists():
            temporary=output.with_suffix(output.suffix+'.partial')
            request=urllib.request.Request(row['url'],headers={'User-Agent':'SiliconForest-conformance/1.0'})
            with urllib.request.urlopen(request,timeout=60) as response,temporary.open('wb') as target:
                while block:=response.read(1024*1024):target.write(block)
            if hashlib.file_digest(temporary.open('rb'),'sha256').hexdigest()!=row['sha256']:
                raise ValueError('Publisher SHA256 mismatch: '+row['name'])
            temporary.rename(output)
        digest=hashlib.file_digest(output.open('rb'),'sha256').hexdigest()
        if digest!=row['sha256']:raise ValueError('Existing download SHA256 mismatch: '+row['name'])
        print(row['name'],digest,'publisher SHA256 verified',flush=True)


if __name__=='__main__':main()
