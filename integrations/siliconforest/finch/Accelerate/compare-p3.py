#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Measure full P3 observations; never masks or changes the raw test verdict."""
import argparse
import json
import math
from pathlib import Path


def read(path):
    row=next(r for r in json.loads(path.read_text()) if r['name']=='vimage-p3-probe')
    if row['status']!='OK' or row['returncode']!=0:
        raise ValueError('Probe failed: '+str(path))
    pixels={}
    for line in row['stdout'].splitlines():
        f=line.split()
        if f and f[0]=='PIX':
            if len(f)!=8:raise ValueError('Incomplete pixel')
            values=list(map(float,f[4:]))
            if not all(math.isfinite(v) for v in values):raise ValueError('Nonfinite pixel')
            if f[2]=='U8' and any(v!=int(v) or not 0<=v<=255 for v in values):
                raise ValueError('Invalid byte component')
            key=(f[1],f[2],int(f[3]))
            if key in pixels:raise ValueError('Duplicate pixel')
            pixels[key]=values
    if len(pixels)!=80:raise ValueError('Wrong pixel count')
    return row,pixels


def metrics(a,b):
    errors=[x-y for row,other in zip(a,b) for x,y in zip(row,other)]
    return dict(exact=a==b,maximum_absolute_error=max(map(abs,errors)),
                rms_error=math.sqrt(sum(x*x for x in errors)/len(errors)),
                components_different=sum(x!=0 for x in errors))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('baseline',type=Path);ap.add_argument('fixed',type=Path)
    ap.add_argument('reference',type=Path);ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();data=[read(p) for p in [args.baseline,args.fixed,args.reference]]
    if len({row['sha256'] for row,p in data})!=1:raise ValueError('Programs differ')
    output=dict(sha256=data[0][0]['sha256'],measurements=[],
        limits='Supplementary measurements only. No tolerance/mask or replacement raw grade.')
    for role,(row,pixels) in zip(['baseline','fixed','reference'],data):
        for kind in ['U8','F32']:
            values=lambda case:[pixels[(case,kind,i)] for i in range(10)]
            native=lambda case:[data[2][1][(case,kind,i)] for i in range(10)]
            for case in ['identity','named-P3','ICC-P3','generated-P3']:
                output['measurements'].append(dict(role=role,kind=kind,case=case,
                    against_native=metrics(values(case),native(case))))
            output['measurements'].append(dict(role=role,kind=kind,
                named_against_identity=metrics(values('named-P3'),values('identity')),
                named_against_ICC=metrics(values('named-P3'),values('ICC-P3'))))
    args.output.write_text(json.dumps(output,indent=2)+'\n')
    for row in output['measurements']:
        if 'named_against_identity' in row:
            print(row['role'],row['kind'],'named vs identity',row['named_against_identity'],
                  'named vs ICC',row['named_against_ICC'])


if __name__=='__main__':main()
