#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify public video sample-cursor tables against synthetic-fixture ffprobe data."""
import argparse
import json
from pathlib import Path
import re


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('receipt',type=Path)
    ap.add_argument('fixtures',type=Path)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();results=[]
    for r in json.loads(a.receipt.read_text()):
        ext={'avasset-mp4':'mp4','avasset-mov':'mov'}[r['name']]
        if r['status']!='OK' or r['returncode']!=0:raise ValueError('Program failed: '+r['name'])
        f=json.loads((a.fixtures/('test-av.'+ext+'.ffprobe.json')).read_text())
        v=next(s for s in f['streams'] if s['codec_type']=='video')
        packets=[p for p in f['packets'] if p['stream_index']==v['index']]
        table=re.findall(r'^sample (\d+) offset=(\d+) size=(\d+) dts=(-?\d+)/(\d+) pts=(-?\d+)/(\d+) duration=(\d+)/(\d+) sync=(\d+)$',r['stdout'],re.M)
        if len(packets)!=10 or len(table)!=10:raise ValueError('Expected ten video samples')
        num,den=map(int,v['time_base'].split('/'))
        for i,(packet,row) in enumerate(zip(packets,table)):
            seq,offset,size,dts,ds,pts,ps,duration,ts,sync=map(int,row)
            ok=(seq==i and offset==int(packet['pos']) and size==int(packet['size']) and
                dts*den==int(packet['dts'])*num*ds and pts*den==int(packet['pts'])*num*ps and
                duration*den==int(packet['duration'])*num*ts and sync==int('K' in packet['flags']))
            if not ok:raise ValueError('Table mismatch: '+r['name']+' sample '+str(i))
        results.append(dict(name=r['name'],binary_sha256=r['sha256'],entries=10,video_table_matches_linux_ffprobe=True))
    if {r['name'] for r in results}!={'avasset-mp4','avasset-mov'}:raise ValueError('Missing fixture result')
    a.output.write_text(json.dumps(results,indent=2)+'\n')
    print('MP4 and MOV: all ten offsets/sizes/DTS/PTS/durations/sync flags match ffprobe')


if __name__=='__main__':main()
