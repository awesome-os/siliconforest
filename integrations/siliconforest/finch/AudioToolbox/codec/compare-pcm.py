#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Quantify observed PCM differences without replacing the raw DIFF verdict.

Programs emit their own PCM on stdout. No native-generated files are acquired.
This synthetic fixture is not an ISO normative decoder conformance sequence.
"""
import argparse,json,math,re
from pathlib import Path

def pcm(row):
    if row['returncode']!=0 or row['status']!='OK':raise ValueError('program failed: '+row['name'])
    lines=row['stdout'].splitlines()
    head=re.fullmatch(r'PCM rate=(\d+) channels=(\d+)',lines[0])
    tail=re.fullmatch(r'PCM frames=(\d+)',lines[-1])
    if not head or not tail:raise ValueError('incomplete PCM observations')
    rate,channels=map(int,head.groups());frames=int(tail[1]);data=[]
    if frames!=len(lines)-2:raise ValueError('frame count mismatch')
    for index,line in enumerate(lines[1:-1]):
        fields=line.split()
        if fields[:2]!=['PCM',str(index)] or len(fields)!=channels+2:raise ValueError('frame order/channel count')
        values=[int(v) for v in fields[2:]]
        if any(v < -32768 or v > 32767 for v in values):raise ValueError('not S16 PCM')
        data.append(values)
    return rate,channels,data

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('candidate',type=Path);ap.add_argument('reference',type=Path,nargs='+');ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    candidates={r['name']:r for r in json.loads(args.candidate.read_text()) if r['name'].startswith('pcm-')}
    references={r['name']:r for p in args.reference for r in json.loads(p.read_text()) if r['name'].startswith('pcm-')}
    result=[]
    for name,c in candidates.items():
        r=references[name]
        if c['sha256']!=r['sha256']:raise ValueError('program bytes differ')
        rate,ch,a=pcm(c);rr,rc,b=pcm(r)
        if (rate,ch,len(a))!=(rr,rc,len(b)):raise ValueError('sample rate/channel/frame contract differs')
        channels=[]
        for channel in range(ch):
            x=[v[channel] for v in a];y=[v[channel] for v in b];diff=[u-v for u,v in zip(x,y)]
            rms=math.sqrt(sum(v*v for v in diff)/len(diff));signal=math.sqrt(sum(v*v for v in y)/len(y))
            lag_rms={str(lag):math.sqrt(sum((x[n+lag]-y[n])**2 for n in range(2,len(x)-2))/(len(x)-4)) for lag in range(-2,3)}
            channels.append(dict(channel=channel,maximum_error_lsb=max(map(abs,diff)),rms_error_lsb=rms,rms_error_fullscale=rms/32768,snr_db=None if not rms else 20*math.log10(signal/rms),samples_different=sum(v!=0 for v in diff),lag_rms_lsb=lag_rms))
        result.append(dict(name=name,sha256=c['sha256'],rate=rate,channels=ch,frames=len(a),exact=a==b,measurements=channels))
    args.output.write_text(json.dumps(dict(raw_verdict='DIFF' if any(not r['exact'] for r in result) else 'MATCH',results=result,limits='Measurements only, no added tolerance/mask. Our tones and the native decoder are not ISO supplied normative conformance data.'),indent=2)+'\n')
    for r in result:print(r['name'],'EXACT' if r['exact'] else 'DIFF',[(m['maximum_error_lsb'],round(m['rms_error_lsb'],6)) for m in r['measurements']])
if __name__=='__main__':main()
