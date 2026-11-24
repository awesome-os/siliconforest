#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Relocate an already scanned Homebrew ffmpeg closure into a fresh CLI stage.

System providers are inputs, not installed. No SSH, build, service or kernel
operation is performed. Only normal Homebrew load names and ad-hoc hashes change.
"""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['closure','kegs','coremedia','corevideo','videotoolbox','reference-ffmpeg','fixture','stage']:
        ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--remote-stage',required=True)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--overlay',type=Path,action='append',default=[])
    ap.add_argument('--provider',action='append',default=[],metavar='NAME=OUTPUT',
                    help='Additional explicitly selected staged framework output')
    ap.add_argument('--capability-probe',type=Path)
    ap.add_argument('--vt-probe',type=Path)
    a=ap.parse_args();here=Path(__file__).resolve().parent
    if a.stage.exists():raise ValueError('Stage must be new')
    if not a.remote_stage.startswith('/private/var/db/sffinch/'):raise ValueError('Remote stage must stay isolated')
    sys.path.insert(0,str(here.parents[2]/'homebrew'))
    # Root resolves the scanner's Homebrew prefix links and original bottle names.
    import sf_gapscan
    api=runpy.run_path(str(here.parents[2]/'homebrew/hb_macho.py'))
    root=sf_gapscan.Root(a.root,overlays=a.overlay,mounts=[('/opt/homebrew/Cellar',a.kegs.resolve())])
    (a.stage/'lib').mkdir(parents=True);(a.stage/'bin').mkdir();(a.stage/'fixtures').mkdir()
    logical=[]
    for line in a.closure.read_text().splitlines():
        fields=line.split(maxsplit=2)
        if len(fields)==3 and fields[0]=='CLOSURE' and fields[2].startswith('/opt/homebrew/'):
            logical.append(fields[2])
    if not any(p.endswith('/bin/ffmpeg') for p in logical):raise ValueError('No ffmpeg in closure')
    def relocate(name):
        if name.startswith('/opt/homebrew/') or '@@HOMEBREW_' in name:
            return a.remote_stage+'/lib/'+Path(name).name if name.endswith('.dylib') else a.remote_stage+'/lib'
        return name
    receipts=[]
    for name in logical:
        source=root.resolve(name)
        if source is None:raise ValueError('Unresolved bottle file '+name)
        before=source.read_bytes();after,changed=api['rewrite_names'](before,relocate);after=api['resign'](after)[0]
        if api['verify'](after):raise ValueError('Bad ad-hoc hashes')
        target=a.stage/('bin/ffmpeg-bottle' if name.endswith('/bin/ffmpeg') else 'lib/'+Path(name).name)
        if target.exists() and target.read_bytes()!=after:raise ValueError('Conflicting library basename '+target.name)
        target.write_bytes(after);target.chmod(0o755)
        receipts.append(dict(logical=name,original_sha256=hashlib.sha256(before).hexdigest(),staged_sha256=hashlib.sha256(after).hexdigest(),changed_names=changed))
    providers=[('CoreMedia',a.coremedia),('CoreVideo',a.corevideo),('VideoToolbox',a.videotoolbox)]
    for item in a.provider:
        name,path=item.split('=',1)
        if not name.isalnum() or name in {p[0] for p in providers}:raise ValueError('Invalid/duplicate provider '+name)
        providers.append((name,Path(path)))
    for name,path in providers:
        shutil.copytree(path/'System/Library/Frameworks'/(name+'.framework'),a.stage/'System/Library/Frameworks'/(name+'.framework'),symlinks=True)
    hashes=[]
    for name,_ in providers:
        path=a.stage/'System/Library/Frameworks'/(name+'.framework')/'Versions/A'/name
        hashes.append(dict(name=name,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    (a.stage/'provider-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
    shutil.copy2(a.reference_ffmpeg,a.stage/'bin/ffmpeg-linux-reference');shutil.copy2(a.fixture,a.stage/'fixtures/test-av.mp4')
    env={'DYLD_FRAMEWORK_PATH':'@ROOT@/System/Library/Frameworks'}
    options=['-nostdin','-v','error','-hwaccel','none','-threads','1','-i','fixtures/test-av.mp4','-map','0:v:0','-an','-pix_fmt','nv12','-f','framemd5','-']
    rows=[dict(name='ffmpeg-linux-reference',argv=['bin/ffmpeg-linux-reference']+options,mode='reference',skip_lines=0,timeout=30,env=env),dict(name='ffmpeg-bottle-software',argv=['bin/ffmpeg-bottle']+options,mode='exit',skip_lines=0,timeout=30,env=env)]
    probes=[]
    if a.capability_probe:probes.append(('bottle-capabilities',a.capability_probe,[],'exit'))
    if a.vt_probe:probes.append(('vt-software-test',a.vt_probe,['--software','fixtures/test-av.mp4'],'reference'))
    if probes:
        probe_dir=a.stage/'probe-programs';probe_dir.mkdir()
        for name,source,args,mode in probes:shutil.copy2(source,probe_dir/name)
        subprocess.run([sys.executable,str(here.parents[1]/'tools/portable-tests.py'),
                        str(here.parents[2]/'homebrew/hb_macho.py'),str(probe_dir),
                        str(a.stage/'probe-hashes.json')],check=True)
        for name,_,args,mode in probes:
            shutil.move(probe_dir/name,a.stage/'bin'/name)
            rows.append(dict(name=name,argv=['bin/'+name]+args,mode=mode,skip_lines=0,timeout=30,env=env))
        probe_dir.rmdir()
    (a.stage/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n');(a.stage/'bottle-relocation.json').write_text(json.dumps(receipts,indent=2)+'\n')
    shutil.copy2(here.parents[1]/'tools/compare-tests.py',a.stage)


if __name__=='__main__':main()
