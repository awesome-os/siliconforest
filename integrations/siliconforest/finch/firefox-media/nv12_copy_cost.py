# SPDX-License-Identifier: MIT
"""Isolated paired-API costs, not browser CPU/FPS or a population tail estimate."""
import re
import statistics

def distribution(values):
    if not values:return None
    return dict(n=len(values),minimum=min(values),median=statistics.median(values),
                maximum=max(values),mean=statistics.mean(values))

def analyze(text):
    pending=[];samples=[];cases=[]
    cost=re.compile(r'^SFNV12COPYCOST profile=([0-9a-f]+) revision=([01]) wall_ns=(-?\d+) wall_available=([01]) process_cpu_us=(-?\d+) cpu_available=([01])$')
    parent=re.compile(r'^SFNV12STRICT parent profile=(\d+) range=(full|video) visible=(\d+)x(\d+) stride=(\d+) y_offset=(\d+) top_stride=(\d+) uv_offset=(\d+) padded_allocation=(\d+) cross_exec=1 local_use=0 global_in_use=0 pass=1$')
    for line in text.splitlines():
        m=cost.fullmatch(line)
        if m:
            profile,revision,wall,wall_ok,cpu,cpu_ok=m.groups()
            row=dict(profile=int(profile,16),revision=int(revision),wall_ns=int(wall),
                wall_available=wall_ok=='1',process_cpu_us=int(cpu),cpu_available=cpu_ok=='1')
            if (row['wall_available'] and row['wall_ns']<0) or (row['cpu_available'] and row['process_cpu_us']<0):
                raise ValueError('Available cost counter reversed')
            pending.append(row)
        elif line.startswith('SFNV12COPYCOST '):raise ValueError('Malformed cost record')
        m=parent.fullmatch(line)
        if m:
            profile,range_,w,h,stride,y_offset,top_stride,uv_offset,allocation=m.groups()
            if len(pending)!=2 or [p['revision'] for p in pending]!=[0,1] or any(p['profile']!=int(profile) for p in pending):
                raise ValueError('Two matching copy revisions required before each parent receipt')
            case=dict(profile=int(profile),range=range_,width=int(w),height=int(h),
                stride=int(stride),y_offset=int(y_offset),top_stride=int(top_stride),
                uv_offset=int(uv_offset),allocation=int(allocation))
            cases.append(case)
            samples.extend(dict(case,**p) for p in pending);pending=[]
    if pending or len(cases)!=12 or len(samples)!=24:raise ValueError('Complete twelve-case native cost control required')
    if len({(p['profile'],p['range'],p['y_offset']) for p in cases})!=12:raise ValueError('Duplicate native cases')
    result=dict(samples=samples,wall_available=sum(p['wall_available'] for p in samples),
        cpu_available=sum(p['cpu_available'] for p in samples),by_revision={})
    for revision in [0,1]:
        selected=[p for p in samples if p['revision']==revision]
        result['by_revision'][str(revision)]=dict(
            wall_ms=distribution([p['wall_ns']/1e6 for p in selected if p['wall_available']]),
            process_cpu_ms=distribution([p['process_cpu_us']/1e3 for p in selected if p['cpu_available']]))
    result['limits']='Twelve cold-context first calls and twelve second calls, each including both plane copies and completion waits. Process getrusage includes driver worker CPU and clock-call overhead; short calls can quantize to zero and are not free. Fixture writes/readbacks are outside the measured call. Not sustained throughput, browser CPU, panel FPS, loaded latency or a population tail.'
    return result
