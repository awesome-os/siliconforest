#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Linux-generated 900 distinct H264 frames; verify every scaled marker offline."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import frame_marker as marker


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('destination',type=Path);a=p.parse_args()
    a.destination.mkdir(exist_ok=False);movie=a.destination/'frame-id-1080p60.mp4';commands=[]
    encode=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-f','lavfi','-i',
        'testsrc2=size=1920x1080:rate=60','-frames:v','900','-an','-vf',marker.filters(),
        '-c:v','libx264','-preset','ultrafast','-crf','18','-threads','6','-pix_fmt','yuv420p',
        '-color_primaries','bt709','-color_trc','bt709','-colorspace','bt709','-movflags','+faststart',str(movie)]
    commands.append(encode)
    with (a.destination/'encode.log').open('w') as log:
        subprocess.run(encode,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-select_streams','v:0',
        '-show_streams','-of','json',str(movie)],text=True,timeout=60))
    stream=probe['streams'][0]
    if (stream['width'],stream['height'],stream['nb_read_frames'],stream['r_frame_rate'])!=(1920,1080,'900','60/1'):
        raise ValueError('Actual encoded dimensions/count/rate mismatch')
    checks=[]
    for width,height in [(320,180),(160,90)]:
        command=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-i',str(movie),'-an','-vf',
            f'scale={width}:{height}:flags=bilinear','-pix_fmt','gray','-f','rawvideo','-']
        commands.append(command);count=0
        with (a.destination/f'decode-{width}.stderr').open('w') as err:
            process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=err)
            try:
                while True:
                    data=process.stdout.read(width*height)
                    if not data:break
                    actual=marker.decode_gray_frame(data,width,height)
                    if actual!=count:raise ValueError(f'Frame identity mismatch at {count}: {actual}')
                    count+=1
                if process.wait(timeout=30):raise RuntimeError('Linux reference decoder failed')
            finally:
                if process.poll() is None:process.kill();process.wait()
        if count!=900:raise ValueError('Incomplete reference decode')
        checks.append(dict(width=width,height=height,decoded_ids=count,all_ids_in_order=True))
    manifest=dict(source='Own FFmpeg testsrc2 and Gray-code marker; no proprietary content',
        file=movie.name,sha256=hashlib.sha256(movie.read_bytes()).hexdigest(),stream=stream,layout=marker.LAYOUT,
        prefix=marker.PREFIX,suffix=marker.SUFFIX,commands=commands,scaled_reference_checks=checks,
        ffmpeg=subprocess.check_output(['ffmpeg','-version'],text=True).splitlines()[0],
        limits='Offline encoded-frame identity only. No target decode, parent export, compositor flip, performance or panel presentation result.')
    (a.destination/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(checks))


if __name__=='__main__':raise SystemExit(main())
