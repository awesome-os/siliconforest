#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Qualify a private capture run against independent framemd5 and kernel deltas.

This reads receipts only; it never builds, contacts hardware or turns an
unsupported/copy run into shared-capture evidence. It does not measure browser
presentation, GPU completion or frame rate.
"""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re


def golden(text,stream):
    match=re.search(r'^#tb 0: (\d+/\d+)\s*$',text,re.M)
    if not match:raise ValueError('No independent stream0 time base')
    base=Fraction(match[1]);result=[]
    width,height=int(stream['width']),int(stream['height'])
    expected_size=width*height+2*((width+1)//2)*((height+1)//2)
    for line in text.splitlines():
        if not line.strip() or line.startswith('#'):continue
        fields=[value.strip() for value in line.split(',')]
        if len(fields)!=6 or fields[0]!='0':raise ValueError('Unexpected reference stream/row')
        if int(fields[4])!=expected_size or not re.fullmatch('[a-f0-9]{32}',fields[5]):raise ValueError('Not visible NV12 data')
        result.append((int(fields[2])*base,width,height,fields[5]))
    if len(result)<65 or len({row[0] for row in result})!=len(result):raise ValueError('Insufficient or duplicate reference pictures')
    return sorted(result)


def stats(text):
    return {key:int(value,16 if value.startswith('0x') else 10)
            for key,value in re.findall(r'\b([a-z_]+) (0x[a-fA-F0-9]+|\d+)\b',text)}


def qualify(record,expected,stream,before,after):
    source_base=Fraction(stream['time_base'])
    # The inherited test client prints raw packet ticks with denominator den.
    if source_base.numerator!=1:raise ValueError('Test client requires stream time_base numerator1')
    text=record.get('stdout','');err=record.get('stderr','');count=len(expected)
    frames=re.findall(r'^SFAVD_FRAME (\d+) pts (-?\d+) (\d+)x(\d+) ([a-f0-9]{32}) ',text,re.M)
    actual=sorted((int(pts)*source_base,int(width),int(height),digest) for _,pts,width,height,digest in frames)
    counters=re.findall(r'^SF_VT_GPU frames=(\d+) copied-bytes=(\d+) required=(\d+)\s*$',err,re.M)
    mandatory=['jobs','done','errors','timeouts','stale','dart_faults','contexts','dart_poisoned','quarantined_desc']
    stats_present=all(key in table for table in [before,after] for key in mandatory)
    checks={
        'program_exit0':record.get('returncode')==0 and record.get('status')=='OK',
        'nonvacuous_reference':count>=65,
        'all_visible_pixels_and_pts':len(frames)==count and actual==expected,
        'callback_indices_complete':sorted(int(row[0]) for row in frames)==list(range(count)),
        'hardware_selected':bool(re.search(r'^VT_SESSION .* hardware 1\s*$',text,re.M)),
        'import_contract_available':'SF_VT_GPU requested=1 required=1 available=1 kernel-cap=1 cv-lifetime=1' in err,
        'zero_cpu_copy_counters':bool(counters) and all(tuple(map(int,row))==(count,0,1) for row in counters),
        'real_gpu_output_and_held_picture':f'CAPTURE_SUMMARY GPU-frames={count} held-checks=1 errors=0' in text and 'CAPTURE_HELD stable-after-64-frames=1' in text,
        'no_callback_failures':not re.search(r'^(?:SFAVD_FAIL|VT_DECODE_FAILED|CAPTURE_FAIL)\b',text,re.M),
        'kernel_stats_complete':stats_present,
    }
    checks['actual_kernel_jobs']=stats_present and after['jobs']-before['jobs']==count and after['done']-before['done']==count
    checks['no_kernel_faults_or_quarantine']=stats_present and all(after[k]==before[k] for k in ['errors','timeouts','stale','dart_faults']) and all(table[k]==0 for table in [before,after] for k in ['contexts','dart_poisoned','quarantined_desc'])
    return dict(status='PASS' if all(checks.values()) else 'FAIL',pictures=count,checks=checks,
                limits='Private capture correctness/CPU-copy counters only. No GPU fence, browser presentation, 1080p60 delivery, or performance claim.')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ['candidate','framemd5','fixture_manifest','movie','before','after','output']:
        ap.add_argument('--'+key.replace('_','-'),type=Path,required=True)
    ap.add_argument('--name',required=True);a=ap.parse_args()
    fixture=next(row for row in json.loads(a.fixture_manifest.read_text()) if row['file']==a.movie.name)
    if hashlib.file_digest(a.movie.open('rb'),'sha256').hexdigest()!=fixture['sha256']:raise ValueError('Reference movie SHA mismatch')
    rows=[row for row in json.loads(a.candidate.read_text()) if row['name']==a.name]
    if len(rows)!=1:raise ValueError('Require exactly one owned capture record')
    result=qualify(rows[0],golden(a.framemd5.read_text(),fixture['stream']),fixture['stream'],stats(a.before.read_text()),stats(a.after.read_text()))
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(result['status'])
    return result['status']!='PASS'


if __name__=='__main__':raise SystemExit(main())
