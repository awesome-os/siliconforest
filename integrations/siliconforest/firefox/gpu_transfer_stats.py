# SPDX-License-Identifier: MIT
"""Read the owned GPU process's actual CGL counters from a UUID-matched image.

No injected call, task suspension or process write is used. Aligned monotonic
counters may straddle adjacent updates; these count CGL transport, not the
decoder's independent row-copy path or diagnostic canvas readback.
"""
import json
from pathlib import Path
import re
import subprocess


def snapshot(providers,specification,helper,identities,owned):
    spec=json.loads(Path(specification).read_text())
    gpu=next(row for row in providers['processes'] if row['role']=='gpu')
    pid=gpu['pid']
    if pid not in identities or identities[pid][1]!=owned.get(pid):
        raise RuntimeError('GPU counter target is not the admitted owned process')
    matches=[]
    for line in gpu['images']:
        match=re.match(r'^\s*([0-9a-fA-F]+)\s+-\s+[0-9a-fA-F]+\s+OpenGL\s+<([0-9A-Fa-f-]+)>\s+(.+)$',line)
        if match:matches.append(match)
    if len(matches)!=1:raise RuntimeError('Expected one observed private CGL image')
    match=matches[0];path=match[3];uuid=match[2].replace('-','').upper()
    if path!=spec['provider'] or uuid!=spec['uuid']:
        raise RuntimeError('Observed CGL provider does not match the counter symbol specification')
    address=int(match[1],16)+spec['offset']
    result=subprocess.run([helper,str(pid),'--read-four-u64',hex(address)],capture_output=True,text=True,timeout=5)
    raw=json.loads(result.stdout)
    if result.returncode or raw.get('pid')!=pid or raw.get('task_status') or raw.get('read_status') or raw.get('read_bytes')!=32:
        raise RuntimeError('Native GPU counter read failed: '+str(raw))
    values=raw.get('raw_u64')
    if not isinstance(values,list) or len(values)!=4 or any(type(value)!=int or not 0<=value<2**64 for value in values):
        raise RuntimeError('Invalid native CGL counter values')
    return dict(pid=pid,provider=path,uuid=uuid,address=hex(address),
        cpu_upload_bytes=values[0],cpu_readback_bytes=values[1],gpu_blit_bytes=values[2],
        direct_alias_commits=values[3],method='Read-only Mach VM snapshot of four aligned CGL counters; recorded provider UUID and symbol offset must match. Counts CGL transport, excludes decoder row copies and verification readback.')
