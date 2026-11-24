#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate our local H264/AAC representation-switch inputs on Linux only.

Each file has its own init segment and starts at zero. A browser test must
append these in MSE sequence mode. Generation is not browser/decode evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path)
    a=p.parse_args()
    if os.uname().sysname!='Linux':raise ValueError('Generate here on Linux; no Mac builds')
    ffmpeg=shutil.which('ffmpeg');ffprobe=shutil.which('ffprobe')
    if not ffmpeg or not ffprobe:raise ValueError('Linux ffmpeg and ffprobe required')
    a.output.mkdir();rows=[]
    for width,height,bitrate in [(640,360,'500k'),(1920,1080,'3000k'),(1280,720,'1500k')]:
        name=f'{width}x{height}-60.mp4';path=a.output/name
        command=[ffmpeg,'-nostdin','-hide_banner','-loglevel','error',
            '-f','lavfi','-i',f'testsrc2=size={width}x{height}:rate=60',
            '-f','lavfi','-i','sine=frequency=440:sample_rate=48000',
            '-t','2','-c:v','libx264','-preset','ultrafast','-profile:v','main','-level:v','4.2',
            '-coder','1','-pix_fmt','yuv420p','-b:v',bitrate,'-g','60','-keyint_min','60','-sc_threshold','0','-bf','0',
            '-c:a','aac','-b:a','96k','-af','volume=0.01',
            '-movflags','frag_keyframe+empty_moov+default_base_moof',str(path)]
        subprocess.run(command,capture_output=True,check=True,timeout=45)
        probe=subprocess.run([ffprobe,'-v','error','-count_packets','-show_streams','-show_format','-of','json',str(path)],
                             capture_output=True,text=True,check=True,timeout=15)
        metadata=json.loads(probe.stdout);video=next(s for s in metadata['streams'] if s['codec_type']=='video')
        audio=next(s for s in metadata['streams'] if s['codec_type']=='audio')
        if (video['codec_name']!='h264' or video['width']!=width or video['height']!=height or
                int(video['nb_read_packets'])!=120 or audio['codec_name']!='aac' or audio['sample_rate']!='48000'):
            raise ValueError('Unexpected generated stream layout')
        data=path.read_bytes();avcc=data.find(b'avcC');moof=data.find(b'moof')
        if not 0<avcc<moof or data[avcc+4]!=1:raise ValueError('Own fragmented MP4 lacks its init avcC')
        codec='avc1.'+data[avcc+5:avcc+8].hex()
        rows.append(dict(file=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
                         width=width,height=height,video_packets=120,video_codec=codec,
                         audio_codec='mp4a.40.2',probe=metadata,command=command))
    if len({r['video_codec'] for r in rows})!=1:raise ValueError('Switch fixtures must use the same declared codec profile')
    version=subprocess.run([ffmpeg,'-version'],capture_output=True,text=True,check=True).stdout.splitlines()[0]
    manifest=dict(segments=rows,ffmpeg_version=version,ffmpeg_sha256=hashlib.sha256(Path(ffmpeg).read_bytes()).hexdigest(),
                  expected_video_frames=360,mime='video/mp4; codecs="'+rows[0]['video_codec']+',mp4a.40.2"',
                  browser_qualified=False,hardware_qualified=False,acoustic_proof=False)
    (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(dict(output=str(a.output),frames=360,mime=manifest['mime'],sizes=[r['bytes'] for r in rows])))


if __name__=='__main__':raise SystemExit(main())
