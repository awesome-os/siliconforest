#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare or execute one private GPU-copy control. Caller holds gui/core/bench.

No selected provider, owner profile, input, output device or kernel is changed.
Do not run across another agent's announced guarded work or presentation series.
"""
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import tarfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[3]

def probe_layout(value=None):
    """Only the three compiled, bounded fixtures are eligible for admission."""
    choices=[]
    for width,height,stride in [(65,33,128),(1920,1080,2048),(3840,2160,4096)]:
        uv_offset=6144 if width==65 else stride*(height+8)
        allocation=max(16384,(uv_offset+stride*((height+1)//2+7)+16383)//16384*16384)
        choices.append(dict(width=width,height=height,stride=stride,
            uv_offset=uv_offset,allocation=allocation))
    if value is None:return choices[0] # Original reviewed odd-size build.
    if value not in choices:raise ValueError('Known compiled probe geometry required')
    return value

def plane_checks(text,layout):
    layout=probe_layout(layout)
    w,h,stride,uv,allocation=(layout[k] for k in ['width','height','stride','uv_offset','allocation'])
    exact=2*(w*h+((w+1)//2)*((h+1)//2)*2)
    children=re.findall(r'^SFNV12STRICT child profile=([0-9a-f]+) exact_bytes=(\d+) cpu_upload=(\d+) cpu_readback=(\d+) gpu_blit=(\d+) reuse_after_completion=1 state_restored=9 paired_copy=1 local_use=0 global_in_use=0 pass=1$',text,re.M)
    parents=re.findall(r'^SFNV12STRICT parent profile=(\d+) range=(full|video) (?:odd_visible|visible)='+str(w)+'x'+str(h)+r' stride='+str(stride)+r' y_offset=(0|512) top_stride=('+str(stride)+'|'+str(stride+64)+r') uv_offset='+str(uv)+' padded_allocation='+str(allocation)+r' cross_exec=1 local_use=0 global_in_use=0 pass=1$',text,re.M)
    return dict(twelve_children=len(children)==12,twelve_parents=len(parents)==12,
        exact_all_bytes=bool(children) and all(int(c[1])==exact for c in children),
        cpu_transport_zero=bool(children) and all(c[2]==c[3]=='0' for c in children),
        exact_gpu_blits=bool(children) and all(int(c[4])==exact for c in children),
        profiles_complete=all(sum(c[0]==profile for c in children)==4 for profile in ['3200','4100','1000']),
        distinct_layout_range_profiles=set(parents)=={(profile,range_,offset,top_stride)
            for profile in ['12800','16640','4096'] for range_ in ['full','video']
            for offset,top_stride in [('0',str(stride)),('512',str(stride+64))]},
        layouts_refused='SFNV12STRICT malformed_layouts_rejected=4 cases=12 pass=1' in text)

def uuid(path):
    data=path.read_bytes();position=32;found=[]
    if struct.unpack_from('<I',data)[0]!=0xfeedfacf:raise ValueError('Original thin Mach-O required')
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,size=struct.unpack_from('<II',data,position)
        if size<8 or position+size>len(data):raise ValueError('Invalid load command')
        if command==27 and size==24:found.append(data[position+8:position+24].hex())
        position+=size
    if len(found)!=1:raise ValueError('One original UUID required')
    return found[0]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('phase',choices=['prepare','run']);p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--cgl',type=Path);p.add_argument('--probe',type=Path)
    a=p.parse_args();stage=a.stage.resolve()
    if stage.parent!=REPO/'runs/sffinch' or not stage.name.startswith('firefox-strict-nv12-packet-'):
        raise ValueError('Fresh own strict NV12 packet required')
    if a.phase=='prepare':
        if not a.cgl or not a.probe or stage.exists():raise ValueError('Original builds and a new packet required')
        if any(not str(v.resolve()).startswith('/nix/store/') for v in [a.cgl,a.probe]):
            raise ValueError('Original Linux Nix outputs required')
        stage.mkdir();(stage/'Frameworks').mkdir()
        shutil.copytree(a.cgl/'Frameworks/OpenGL.framework',stage/'Frameworks/OpenGL.framework',symlinks=True)
        shutil.copy2(a.probe/'bin/nv12-plane-probe',stage/'nv12-plane-probe')
        shutil.copy2(HERE/'strict-nv12-guest.py',stage/'run.py')
        binary=a.cgl/'Frameworks/OpenGL.framework/Versions/A/OpenGL'
        layout_file=a.probe/'share/probe-layout.json'
        layout=probe_layout(json.loads(layout_file.read_text()) if layout_file.exists() else None)
        (stage/'originals.json').write_text(json.dumps(dict(cgl=str(a.cgl),probe=str(a.probe),
            cgl_uuid=uuid(binary),cgl_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
            probe_uuid=uuid(a.probe/'bin/nv12-plane-probe'),probe_layout=layout,installed=False),indent=2)+'\n')
        print(stage);return 0
    if a.cgl or a.probe:raise ValueError('Run only the prepared packet')
    if not os.environ.get('BENCH_OWNER'):raise ValueError('Caller must hold gui/core/bench')
    if (stage/'target-cold-verified.json').exists():raise ValueError('Retain this attempt; use a fresh packet')
    subprocess.run(['python3',str(HERE.parent/'tools/coord-gate.py'),'--max-seconds','120'],cwd=REPO,check=True)
    dest='/private/var/db/sffx/'+stage.name
    ssh=['ssh','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','ConnectTimeout=8',
         '-o','UserKnownHostsFile='+str(Path.home()/'.ssh/known_hosts_m1_siliconforest'),
         '-o','HostKeyAlias=m1-siliconforest','root@10.1.1.211']
    def note(message):
        with (REPO/'runs/m1-coord/log.txt').open('a') as f:
            f.write(datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')+' CODEX '+message+'\n')
    note('strictNV12 private offscreen control START <=120s; own process/privatebroker only, no window/input/install.')
    status=None;collection=None
    try:
        subprocess.run(['python3',str(HERE.parents[1]/'firefox/put.py'),'--source',str(stage),
            '--destination',dest,'--receipt',str(stage/'target-cold-verified.json')],cwd=REPO,check=True,timeout=40)
        with (stage/'transport.log').open('w') as out:
            status=subprocess.run(ssh+[shlex.join(['python3',dest+'/run.py'])],
                stdout=out,stderr=subprocess.STDOUT,timeout=55).returncode
    finally:
        archive=stage/'evidence.tar.gz'
        with archive.open('wb') as out:
            collection=subprocess.run(ssh+[shlex.join(['tar','-C',dest,'-czf','-','native.log','result.json'])],
                stdout=out,stderr=subprocess.PIPE,timeout=15)
        (stage/'collection.stderr').write_bytes(collection.stderr)
        if collection.returncode==0:
            with tarfile.open(archive) as t:
                t.extractall(stage/'evidence',members=[m for m in t.getmembers() if m.isfile() and m.name in ['native.log','result.json']],filter='data')
        (stage/'transport-status.json').write_text(json.dumps(dict(returncode=status,
            collection=collection.returncode,completed=status==0 and collection.returncode==0))+'\n')
        note('strictNV12 control END: transport='+str(status)+' collection='+str(collection.returncode)+'; no installed changes.')
    if status!=0 or collection.returncode!=0:
        (stage/'admission.json').write_text(json.dumps(dict(passed=False,
            checks=dict(transport=status==0,collection=collection.returncode==0),
            scope='Native control did not complete; inspect retained stdout/stderr before retry'))+'\n')
        return 1
    result=json.loads((stage/'evidence/result.json').read_text())
    text=(stage/'evidence/native.log').read_text()
    original=json.loads((stage/'originals.json').read_text())
    layout=probe_layout(original.get('probe_layout'))
    expected={dest+'/Frameworks/OpenGL.framework/Versions/A/OpenGL':original['cgl_uuid'],
        '/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface':'d3d65ac4b3fd308e8c0ba2abc2784159',
        '/usr/local/lib/siliconforest/opengl/v1/lib/libGL.1.dylib':'0c7491be21f135a3a8c8376d0f2213bc'}
    images=re.findall(r'^SFNV12STRICT image uuid=([0-9a-f]{32}) path=(.+)$',text,re.M)
    checks=dict(plane_checks(text,layout),
        actual_images=len(images)==36 and all(expected.get(path)==identity for identity,path in images)
            and all(sum(path==want for _,path in images)==12 for want in expected),
        natural_exit=result.get('status')==0 and not result.get('cleanup_signals') and not result.get('remaining_group'),
        no_input=result.get('input_injected') is False,no_install=result.get('installed_changes') is False)
    admission=dict(passed=all(checks.values()),checks=checks,originals=original,
        scope=f'Private {layout["width"]}x{layout["height"]} padded NV12 cross-exec 2D copies with source overwrite and nine GL-state checks. CPU fixture writes and verification readbacks are outside transport. Not Firefox/QC integration, performance, panel cadence, asynchronous fences, all driver-internal copies or forced GPU-fault qualification.')
    (stage/'admission.json').write_text(json.dumps(admission,indent=2)+'\n')
    print(json.dumps(admission));return 0 if admission['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
