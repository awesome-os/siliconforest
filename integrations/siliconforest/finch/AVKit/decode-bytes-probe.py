#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own generated movie -> real FFmpeg NV12 bytes, preserving any mismatch.

Run only under gui.lock -> bench -> fresh coordination gate, from a new M1
prefix. This neither opens a window nor stops another process. Global kernel
counters remain separate from per-process pixel/decoder observations.
"""
import argparse,hashlib,json,os,re,subprocess,time
from pathlib import Path


def counts(text):
    return {key:int(value) for key,value in re.findall(r'\b(jobs|done|errors|timeouts|stale|contexts|dart_faults) (\d+)',text)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ffmpeg',type=Path,required=True)
    p.add_argument('--movie',type=Path,required=True)
    p.add_argument('--reference',type=Path,required=True)
    p.add_argument('--width',type=int,required=True);p.add_argument('--height',type=int,required=True)
    p.add_argument('--repeat',type=int,default=3)
    a=p.parse_args();root=Path.cwd().resolve()
    if os.uname().sysname!='Darwin' or not str(root).startswith('/private/var/db/sffinch/'):
        raise ValueError('A fresh reviewed M1 prefix is required')
    if not 1<=a.repeat<=4 or not 0<a.width<=4096 or not 0<a.height<=2160 or a.width%2 or a.height%2:
        raise ValueError('Invalid bounded NV12 experiment')
    for path in [a.movie,a.reference]:
        if not path.resolve().is_relative_to(root):raise ValueError('Only own fixture inputs')
    if a.reference.stat().st_size>128*1024*1024:raise ValueError('Reference exceeds experiment bound')
    reference=a.reference.read_bytes();size=a.width*a.height*3//2
    if not reference or len(reference)%size:raise ValueError('Incomplete reference frames')
    frames=len(reference)//size
    expected=[hashlib.md5(reference[i*size:(i+1)*size]).hexdigest() for i in range(frames)]
    results=root/'decode-bytes';results.mkdir()
    env={k:v for k,v in os.environ.items() if not k.startswith('DYLD_')}
    env.update(SF_METAL_ENABLE_REAL_DEVICE='0',VT_AVD_TIMING='1')
    def stats():
        try:
            run=subprocess.run(['sysctl','kern.sf_avd.stats'],capture_output=True,text=True,timeout=2)
            return dict(returncode=run.returncode,stdout=run.stdout,stderr=run.stderr,counts=counts(run.stdout))
        except subprocess.TimeoutExpired:return dict(returncode=None,error='stats timeout',counts={})
    rows=[]
    for n in range(a.repeat+1):
        mode='software' if n==0 else 'hardware'
        output=results/f'{n}-{mode}.nv12'
        argv=[str(a.ffmpeg),'-nostdin','-v','info','-hwaccel','none' if n==0 else 'videotoolbox',
              '-threads','1','-i',str(a.movie),'-map','0:v:0','-an','-frames:v',str(frames+1),
              '-pix_fmt','nv12','-f','rawvideo',str(output)]
        before=stats();start=time.monotonic();run=None;error=None
        try:run=subprocess.run(argv,env=env,capture_output=True,text=True,timeout=20)
        except subprocess.TimeoutExpired as failure:
            error='own FFmpeg timed out'
            (results/f'{n}.timeout.stderr').write_bytes(failure.stderr or b'')
        after=stats()
        data=output.read_bytes() if output.exists() else b''
        actual=[hashlib.md5(data[i*size:(i+1)*size]).hexdigest() for i in range(len(data)//size)]
        bad=[]
        for i,(got,want) in enumerate(zip(actual,expected)):
            if got==want:continue
            pixels=data[i*size:(i+1)*size];wanted=reference[i*size:(i+1)*size]
            different=[j for j,(x,y) in enumerate(zip(pixels,wanted)) if x!=y]
            bad.append(dict(index=i,actual_md5=got,expected_md5=want,different_bytes=len(different),
                            different_y=sum(j<a.width*a.height for j in different),
                            first_offsets=different[:32],max_absolute_error=max(abs(pixels[j]-wanted[j]) for j in different)))
        timing=re.findall(r'VT_AVD_TIMING pictures (\d+) hardware ([01])',run.stderr if run else '')
        hardware=(sum(int(v) for v,_ in timing)==frames and bool(timing) and all(v=='1' for _,v in timing)) if n else None
        exact=len(data)==len(reference) and actual==expected
        b,c=before['counts'],after['counts'];required={'jobs','done','errors','timeouts','stale','contexts','dart_faults'}
        isolated=(required<=b.keys() and required<=c.keys() and b['contexts']==c['contexts']==0 and
                  c['jobs']-b['jobs']==c['done']-b['done']==(frames if n else 0) and
                  all(c[k]==b[k] for k in ['errors','timeouts','stale','dart_faults']))
        row=dict(mode=mode,index=n,argv=argv,returncode=run.returncode if run else None,error=error,
                 seconds=round(time.monotonic()-start,3),expected_frames=frames,actual_frames=len(actual),
                 exact_pixels=exact,hardware_reported=hardware,independent_kernel_gate=isolated,
                 output_sha256=hashlib.sha256(data).hexdigest(),bad_frames=bad,before=before,after=after,
                 stdout=run.stdout if run else '',stderr=run.stderr if run else '')
        rows.append(row);(root/'decode-bytes-results.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('DECODE_BYTES_RESULT '+json.dumps(row),flush=True)
        if error:break
    # A passing pixel check never masks nonisolated kernel counters or an old
    # failure. Every full raw candidate is retained for independent inspection.
    return int(len(rows)!=a.repeat+1 or any(r['returncode']!=0 or not r['exact_pixels'] or
               (r['mode']=='hardware' and not r['hardware_reported']) for r in rows))


if __name__=='__main__':raise SystemExit(main())
