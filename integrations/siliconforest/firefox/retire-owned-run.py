#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Retire one archived private QA run; plan by default.

Call under gui.lock or core.lock. Every archived file is read before --apply
can remove its exact remote run directory. Never removes tools, providers,
installed applications, owner profiles, or a run referenced by a live process.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run',type=Path,required=True,help='collected workstation run directory')
    ap.add_argument('--apply',action='store_true')
    args=ap.parse_args()
    repo=Path(__file__).resolve().parents[3]
    directory=args.run.resolve()
    if directory.parent!=repo/'runs/firefox':
        raise ValueError('Expected one direct child of the owned workstation run root')
    metadata=json.loads((directory/'launch-metadata.json').read_text())
    command=metadata['command']
    remote=command[command.index('--run')+1]
    if remote!='/private/var/db/sffx/'+directory.name or directory.name in {'.','..'}:
        raise ValueError('Collected run and remote path do not match')
    archive=directory/'target-evidence.tar.gz'
    seen=False
    with tarfile.open(archive) as stream:
        for entry in stream:
            if entry.isfile():
                source=stream.extractfile(entry)
                while source.read(1024*1024):
                    pass
                seen|=entry.name.lstrip('./')=='firefox.log'
    if not seen:
        raise ValueError('Archive lacks the owned browser log')
    with archive.open('rb') as stream:
        digest=hashlib.file_digest(stream,'sha256').hexdigest()
    record=dict(remote=remote,archive=str(archive),archive_sha256=digest,
                archive_fully_read=True,apply=args.apply)
    if not args.apply:
        print(json.dumps(record,indent=2));return 0
    gate=subprocess.run(['python3',str(Path(__file__).with_name('coord-gate.py'))],capture_output=True,text=True)
    if gate.returncode:
        record.update(skipped='coordination hold',gate=gate.stdout+gate.stderr)
        (directory/'remote-retirement.json').write_text(json.dumps(record,indent=2)+'\n')
        return gate.returncode
    # The authenticated command receives only a literal exact path, never shell
    # expansion, and refuses current process or installed-runtime references.
    script='''import os,json,shutil,subprocess,sys
from pathlib import Path
assert os.uname().sysname=='Darwin'
root=Path('/private/var/db/sffx');p=Path(__REMOTE_RUN__)
assert p.parent==root and str(root.resolve())==str(root) and p.name not in ['.','..']
if not p.exists():print(json.dumps({'removed':False,'skip':'absent'}));raise SystemExit(0)
if p.is_symlink()or not p.is_dir():raise RuntimeError('Not a regular owned directory')
commands=subprocess.check_output(['ps','-axo','command='],text=True).splitlines()
if any(str(p)in c for c in commands):print(json.dumps({'removed':False,'skip':'live process reference'}));raise SystemExit(0)
def strings(v):
 if isinstance(v,str):yield v
 elif isinstance(v,dict):
  for x in v.values():yield from strings(x)
 elif isinstance(v,list):
  for x in v:yield from strings(x)
manifest=json.loads(Path('/usr/local/lib/siliconforest/firefox/160-system1/release.json').read_text())
if any(x==str(p)or x.startswith(str(p)+'/')for x in strings(manifest)):raise RuntimeError('Installed runtime references this path')
before=os.statvfs(root);before=before.f_bavail*before.f_frsize
shutil.rmtree(p)
after=os.statvfs(root);after=after.f_bavail*after.f_frsize
print(json.dumps({'removed':True,'available_before':before,'available_after':after}))
'''
    ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
         '-o','HostKeyAlias=m1-siliconforest','-o',
         'UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),'root@10.1.1.211']
    script=script.replace('__REMOTE_RUN__',repr(remote))
    result=subprocess.run(ssh+['python3 -'],input=script,
                          capture_output=True,text=True,timeout=60)
    record.update(returncode=result.returncode,stdout=result.stdout,stderr=result.stderr)
    (directory/'remote-retirement.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
    return result.returncode

if __name__=='__main__':
    raise SystemExit(main())
