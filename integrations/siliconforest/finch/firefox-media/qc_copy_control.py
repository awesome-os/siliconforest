# SPDX-License-Identifier: MIT
"""Strict private QC/CGL provider admission and owned read-only upload counters."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

FIELDS=['yuv_frames','gpu_attempts','gpu_frames','cpu_frames','cpu_plane_calls',
        'cpu_visible_bytes','cpu_source_span_bytes','storage_allocations','gpu_failures','wide_cpu_frames']

def binary_identity(path,symbol):
    path=Path(path)
    if not str(path.resolve()).startswith('/nix/store/'):raise ValueError('Original Linux Nix binary required')
    data=path.read_bytes();position=32;identities=[]
    if struct.unpack_from('<I',data)[0]!=0xfeedfacf:raise ValueError('Original thin Mach-O required')
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,size=struct.unpack_from('<II',data,position)
        if size<8 or position+size>len(data):raise ValueError('Invalid load command')
        if command==27 and size==24:identities.append(data[position+8:position+24].hex())
        position+=size
    if len(identities)!=1:raise ValueError('Exactly one UUID required')
    symbols=subprocess.check_output(['llvm-nm',str(path)],text=True)
    found=re.findall(r'^([0-9a-fA-F]+)\s+[bBdD]\s+'+re.escape(symbol)+r'$',symbols,re.M)
    if len(found)!=1:raise ValueError('Exactly one original data symbol required')
    return dict(original_linux_nix=str(path),sha256=hashlib.sha256(data).hexdigest(),
                uuid=identities[0],offset=int(found[0],16))

def image_rows(lines):
    rows=[]
    for line in lines:
        m=re.match(r'^\s*([0-9a-fA-F]+)\s+-\s+[0-9a-fA-F]+\s+(\S+)\s+<([0-9a-fA-F-]+)>\s+(.+)$',line)
        if m:rows.append(dict(base=int(m[1],16),name=m[2],uuid=m[3].replace('-','').lower(),path=m[4]))
    return rows

def verify(providers,identity,root):
    root=Path(root);private=root/'qc-nv12';verified=[]
    required={name:'/System/Library/Frameworks/'+name+'.framework/' for name in
              ['AppKit','Onyx2D','CoreText','QuartzCore','IOSurface']}
    required['libWindowServer.dylib']='/usr/lib/libWindowServer.dylib'
    required['libGL.1.dylib']='/usr/local/lib/siliconforest/opengl/v1/lib/libGL.1.dylib'
    for process in providers['processes']:
        images=image_rows(process['images']);checks={}
        for name,prefix in required.items():
            selected=[x for x in images if x['name']==name]
            checks[name]=len(selected)==1 and (selected[0]['path'].startswith(prefix) if prefix.endswith('/') else selected[0]['path']==prefix)
        for name,key,relative in [('QuartzCore.pd','quartzcore','lib/QuartzCore.pd'),
                ('OpenGL','cgl','Frameworks/OpenGL.framework/Versions/A/OpenGL')]:
            selected=[x for x in images if x['name']==name]
            checks[name]=len(selected)==1 and selected[0]['path']==str(private/relative) and selected[0]['uuid']==identity[key]['uuid']
        process['private_qc_copy_checks']=checks
        process['passed']=process['status']==0 and all(checks.values())
        if process['passed']:
            selected=next(x for x in images if x['name']=='QuartzCore.pd')
            verified.append(dict(selected,pid=process['pid'],role=process['role']))
    providers['complete']=set(x['role'] for x in verified)=={'parent','gpu'} and all(x['passed'] for x in providers['processes'])
    providers.update(expected_system_gpu=False,canonical_cocoa_complete=False,
                     private_quartzcore_copy=True,scope='Private QC.pd+CGL only; other listed providers canonical. No installed qualification.')
    if not providers['complete']:raise RuntimeError('Actual private QC/CGL cohort was not admitted')
    return verified

def snapshot(verified,identity,helper,starts):
    rows=[]
    for image in verified:
        pid=image['pid'];current=subprocess.check_output(['ps','-p',str(pid),'-o','lstart='],text=True).strip()
        if not starts.get(pid) or current!=starts[pid].strip():raise RuntimeError('Owned counter target start identity changed')
        address=image['base']+identity['quartzcore']['offset'];values=[];reads=[]
        # Read only the ten-counter range. The final four overlap counters6/7;
        # every native read is bounded to this exact private diagnostic array.
        for offset in [0,32,48]:
            r=subprocess.run([str(helper),str(pid),'--read-four-u64',hex(address+offset)],capture_output=True,text=True,timeout=5)
            raw=json.loads(r.stdout);v=raw.get('raw_u64')
            if (r.returncode or raw.get('pid')!=pid or raw.get('task_status') or raw.get('read_status') or raw.get('read_bytes')!=32
                    or not isinstance(v,list) or len(v)!=4 or any(type(x)!=int or not 0<=x<2**64 for x in v)):
                raise RuntimeError('Owned QC counter read failed: '+str(raw))
            reads.append(raw);values.extend(v if offset!=48 else v[2:])
        rows.append(dict(pid=pid,role=image['role'],provider=image['path'],uuid=image['uuid'],
            os_start_identity=current,address=hex(address),values=dict(zip(FIELDS,values)),native_reads=reads))
    return dict(processes=rows,method='Owned, UUID-matched read-only Mach VM counters; adjacent updates can straddle reads. No call injection or task suspension.')

def compare(before,after):
    old={(p['pid'],p['os_start_identity'],p['uuid'],p['provider']):p for p in before['processes']}
    rows=[]
    for p in after['processes']:
        key=(p['pid'],p['os_start_identity'],p['uuid'],p['provider'])
        if key not in old:raise ValueError('QC counter process identity changed')
        delta={k:p['values'][k]-old[key]['values'][k] for k in FIELDS}
        if any(v<0 for v in delta.values()):raise ValueError('QC counters reversed')
        rows.append(dict(pid=p['pid'],role=p['role'],delta=delta))
    if len(rows)!=len(old):raise ValueError('QC counter process disappeared')
    return dict(processes=rows,total={k:sum(p['delta'][k] for p in rows) for k in FIELDS},
                limits='CPU pixel payload and source spans at actual QC upload calls, not all driver-internal memory movement or panel frames.')
