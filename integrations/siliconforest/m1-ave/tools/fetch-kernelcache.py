#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fetch only manifest/kernelcache ZIP members from Apple's IPSW into private/ave/."""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import plistlib
import re
import subprocess
from urllib.request import Request, urlopen
from urllib.parse import urlsplit
import zipfile


class AppleZip(io.RawIOBase):
    def __init__(self, url, private, build):
        self.url, self.private, self.build, self.position = url, private, build, 0
        self.entries = []
        self.total = None
        self.chunk(0, 0)

    def chunk(self, first, last):
        request = Request(self.url, headers={'Range':f'bytes={first}-{last}', 'Accept-Encoding':'identity'})
        with urlopen(request, timeout=60) as response:
            final = urlsplit(response.url)
            if final.scheme != 'https' or final.hostname != 'updates.cdn-apple.com':
                raise ValueError('Apple archive redirected outside the authorized CDN')
            match = re.fullmatch(r'bytes (\d+)-(\d+)/(\d+)', response.headers.get('Content-Range',''))
            if response.status != 206 or not match or tuple(map(int,match.groups()[:2])) != (first,last):
                raise ValueError('Server did not honor the exact byte range; refusing full archive download')
            total = int(match[3])
            if self.total is not None and total != self.total:raise ValueError('Archive length changed')
            self.total = total
            data = response.read(last-first+2)
        if len(data) != last-first+1:raise ValueError('Range body length mismatch')
        digest = hashlib.sha256(data).hexdigest()
        target = self.private/f'range-{first}-{last}.bin'
        if target.exists() and target.read_bytes() != data:raise ValueError('Existing range differs')
        target.write_bytes(data)
        record={'time':datetime.now(timezone.utc).isoformat(), 'operation':'apple-ipsw-range',
                'source':self.url,'build':self.build,'first':first,'last':last,'archive_size':total,
                'path':target.name,'sha256':digest,'note':'Partial archive bytes only; no whole-IPSW hash claimed'}
        with (self.private/'acquisition-ledger.jsonl').open('a') as ledger:ledger.write(json.dumps(record)+'\n')
        return data

    def seekable(self):return True
    def readable(self):return True
    def tell(self):return self.position
    def seek(self,offset,whence=0):
        position=offset if whence==0 else self.position+offset if whence==1 else self.total+offset
        if position<0 or position>self.total:raise ValueError('Archive seek outside bounds')
        self.position=position
        return position
    def read(self,size=-1):
        if size<0:size=self.total-self.position
        size=min(size,self.total-self.position)
        if size>256*1024*1024:raise ValueError('Refusing archive read over256MiB')
        if size==0:return b''
        result=self.chunk(self.position,self.position+size-1);self.position+=size
        return result


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--url',required=True)
    ap.add_argument('--build',required=True)
    ap.add_argument('--directory',type=Path,required=True)
    a=ap.parse_args()
    url=urlsplit(a.url)
    if url.scheme!='https' or url.hostname!='updates.cdn-apple.com' or url.query or url.fragment or not url.path.endswith('.ipsw'):
        raise ValueError('Require direct public Apple CDN IPSW URL')
    repo=Path(__file__).resolve().parents[4];private=(repo/'private/ave').resolve();directory=a.directory.resolve()
    if not directory.is_relative_to(private):raise ValueError('Inputs must stay in private/ave/')
    if subprocess.run(['git','check-ignore','--quiet',str(directory)],cwd=repo).returncode:raise ValueError('Directory must be git-ignored')
    directory.mkdir(parents=True,mode=0o700,exist_ok=False)
    archive=AppleZip(a.url,directory,a.build)
    with zipfile.ZipFile(archive) as z:
        def member(name):
            entry=z.getinfo(name)
            if entry.file_size>256*1024*1024:raise ValueError('Member exceeds extraction bound')
            data=z.read(entry)
            target=directory/Path(name).name
            with target.open('xb') as f:f.write(data)
            with (directory/'acquisition-ledger.jsonl').open('a') as ledger:ledger.write(json.dumps({'time':datetime.now(timezone.utc).isoformat(),'operation':'zip-member','source':a.url,'build':a.build,'member':name,'path':target.name,'sha256':hashlib.sha256(data).hexdigest()})+'\n')
            return data
        manifest=plistlib.loads(member('BuildManifest.plist'))
        if manifest.get('ProductVersion')!='13.5' or manifest.get('ProductBuildVersion')!=a.build or 'MacBookPro17,1' not in manifest.get('SupportedProductTypes',[]):
            raise ValueError('Manifest does not match target13.5 build/model')
        paths={i['Manifest']['KernelCache']['Info']['Path'] for i in manifest['BuildIdentities'] if i.get('Info',{}).get('DeviceClass','').lower()=='j293ap'}
        if len(paths)!=1:raise ValueError('Manifest lacks unique J293 kernelcache')
        name=paths.pop()
        if not re.fullmatch(r'(?:kernelcaches/)?kernelcache\.release\.mac13g',name):raise ValueError('Unexpected kernelcache path; review before fetching')
        member(name)
    print('Matching manifest and kernelcache acquired privately; hashes/source/build in private ledger only.')


if __name__=='__main__':main()
