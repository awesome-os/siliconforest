#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Add a second audio language and our original text to our player fixture."""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('own_player_movie',type=Path)
p.add_argument('new_output',type=Path)
p.add_argument('--ffmpeg',default='ffmpeg')
a=p.parse_args();a.new_output.mkdir()
caption=a.new_output/'own.srt'
caption.write_text('1\n00:00:00,000 --> 00:00:04,500\nOur original caption.\n')
movie=a.new_output/'movie.mov'
argv=[a.ffmpeg,'-nostdin','-v','error','-i',str(a.own_player_movie),'-i',str(caption),
      '-map','0:v:0','-map','0:a:0','-map','0:a:0','-map','1:0','-c:v','copy',
      '-c:a','copy','-c:s','mov_text','-metadata:s:a:0','language=eng',
      '-metadata:s:a:1','language=fra','-metadata:s:s:0','language=eng',str(movie)]
subprocess.run(argv,check=True,timeout=30)
# FFmpeg's MOV output gives the two copied audio tracks separate alternate
# groups. Put only our generated tracks in one real switching group. The
# layout agrees with Finch's source-owned AVFMovie parser/AVAssetWriter.
data=bytearray(movie.read_bytes())
def boxes(start,end):
    while start<end:
        if start+8>end:raise ValueError('Incomplete own box header')
        size,kind=struct.unpack_from('>I4s',data,start)
        if size<8 or start+size>end:raise ValueError('Unsupported own box size')
        yield start,size,kind
        start+=size
tracks=[]
for offset,size,kind in boxes(0,len(data)):
    if kind!=b'moov':continue
    for track,length,tag in boxes(offset+8,offset+size):
        if tag!=b'trak':continue
        header=handler=None
        for child,n,t in boxes(track+8,track+length):
            if t==b'tkhd':header=(child+8,n-8)
            if t==b'mdia':
                for sub,sn,st in boxes(child+8,child+n):
                    if st==b'hdlr' and sn>=20:handler=bytes(data[sub+16:sub+20])
        if header is None or handler is None:raise ValueError('Own track lacks metadata')
        start,n=header;version=data[start]
        relative=34 if version==0 else 46 if version==1 else None
        if relative is None or relative+2>n:raise ValueError('Unsupported own track header')
        tracks.append((handler,start+relative))
if [kind for kind,_ in tracks[:3]]!=[b'vide',b'soun',b'soun'] or len(tracks)!=4 or tracks[3][0] not in [b'text',b'sbtl',b'subt']:
    raise ValueError('Expected our video, two audio and caption tracks')
groups=[]
for i,(kind,position) in enumerate(tracks):
    previous=struct.unpack_from('>h',data,position)[0]
    group=0 if i==0 else 1 if kind==b'soun' else 2
    struct.pack_into('>h',data,position,group)
    groups.append(dict(handler=kind.decode(),old=previous,new=group))
movie.write_bytes(data)
version=subprocess.run([a.ffmpeg,'-version'],check=True,capture_output=True,text=True,timeout=5).stdout.splitlines()[0]
receipt=dict(argv=argv,ffmpeg_version=version,alternate_groups=groups,
             input_sha256=hashlib.sha256(a.own_player_movie.read_bytes()).hexdigest(),
             files=[dict(path=f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in [caption,movie]])
(a.new_output/'generation.json').write_text(json.dumps(receipt,indent=2)+'\n')
