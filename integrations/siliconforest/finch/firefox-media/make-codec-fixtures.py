#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate our own nonproprietary codec fixtures on Linux, never on the Mac."""
import argparse,hashlib,json,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('destination',type=Path)
p.add_argument('--high-rate',action='store_true',help='Separate actual60Hz AV1 and4K30 VP9 sources, not repeated/upsampled frames')
a=p.parse_args();a.destination.mkdir(exist_ok=False)
version=subprocess.check_output(['ffmpeg','-version'],text=True).splitlines()[0]
video=[
 ('av1-1080p30.webm',1920,1080,30,10,['-c:v','libsvtav1','-preset','12','-crf','35','-svtav1-params','lp=4']),
 ('vp9-1080p60.webm',1920,1080,60,10,['-c:v','libvpx-vp9','-deadline','realtime','-cpu-used','8','-row-mt','1','-threads','4','-b:v','6M']),
 ('hevc-main10-pq.mp4',1280,720,30,2,['-vf','format=yuv420p10le,setparams=color_primaries=bt2020:color_trc=smpte2084:colorspace=bt2020nc','-pix_fmt','yuv420p10le','-c:v','libx265','-preset','ultrafast','-x265-params','pools=2:frame-threads=2:log-level=error:colorprim=bt2020:transfer=smpte2084:colormatrix=bt2020nc','-crf','28','-tag:v','hvc1','-color_primaries','bt2020','-color_trc','smpte2084','-colorspace','bt2020nc']),
 ('vp9-profile2-pq.webm',1280,720,30,2,['-vf','format=yuv420p10le,setparams=color_primaries=bt2020:color_trc=smpte2084:colorspace=bt2020nc','-pix_fmt','yuv420p10le','-c:v','libvpx-vp9','-profile:v','2','-deadline','realtime','-cpu-used','8','-threads','4','-b:v','3M','-color_primaries','bt2020','-color_trc','smpte2084','-colorspace','bt2020nc'])]
commands=[]
if a.high_rate:
    video=[
      ('av1-1080p60.webm',1920,1080,60,10,['-c:v','libsvtav1','-preset','12','-crf','35','-svtav1-params','lp=4']),
      ('vp9-4k30.webm',3840,2160,30,10,['-c:v','libvpx-vp9','-deadline','realtime','-cpu-used','8','-row-mt','1','-threads','4','-b:v','16M'])]
for name,width,height,rate,seconds,options in video:
    argv=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-f','lavfi','-i',f'testsrc2=size={width}x{height}:rate={rate}','-t',str(seconds),'-an',*options,str(a.destination/name)]
    commands.append(argv)
    with (a.destination/(name+'.encode.log')).open('w') as log:subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
for name,codec in [('aac-stereo-48k.m4a','aac'),('opus-stereo-48k.webm','libopus')]:
    argv=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','3','-ac','2','-c:a',codec,'-b:a','128k',str(a.destination/name)]
    commands.append(argv);subprocess.run(argv,check=True,timeout=30)
rows=[]
for path in sorted(a.destination.iterdir()):
    if path.suffix not in ['.webm','.mp4','.m4a']:continue
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-show_format','-of','json',str(path)],text=True))
    stream=probe['streams'][0]
    expected=next((v for v in video if v[0]==path.name),None)
    if expected:
        _,width,height,rate,seconds,_=expected
        if (stream['width'],stream['height'],int(stream['nb_read_frames']))!=(width,height,rate*seconds):
            raise ValueError('Wrong dimensions or decoded frame count: '+path.name)
        if '-pq.' in path.name and (stream.get('pix_fmt'),stream.get('color_primaries'),stream.get('color_transfer'),stream.get('color_space'))!=('yuv420p10le','bt2020','smpte2084','bt2020nc'):
            raise ValueError('10-bit/PQ metadata missing: '+path.name)
        reference=path.with_suffix(path.suffix+'.framemd5')
        argv=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-i',str(path),'-map','0:v:0','-an','-f','framemd5',str(reference)]
        commands.append(argv);subprocess.run(argv,check=True,timeout=90)
    rows.append(dict(file=path.name,sha256=hashlib.file_digest(path.open('rb'),'sha256').hexdigest(),probe=probe))
result=dict(ffmpeg=version,commands=commands,fixtures=rows,
            limits='Self-generated test patterns/tones only. PQ/BT2020 tags test metadata and 10-bit admission; these patterns are not an HDR visual-quality reference. No target playback/speed claim.')
(a.destination/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(fixtures=len(rows),destination=str(a.destination))))
