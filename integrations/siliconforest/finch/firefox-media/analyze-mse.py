#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Local representation switching and HAL acceptance, not network adaptation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from video_selection import installed_family


def analyze(run,scroll):
    launch=json.loads((run/'cadence-launch.json').read_text())
    raw=json.loads((run/'browser/result.json').read_text())
    rows=raw.get('video_measurements',[])
    case=rows[0] if len(rows)==1 else {};video=case.get('video',{})
    first=video.get('initialQuality',{});last=(video.get('quality') or [{}])[-1]
    frames=last.get('total',0)-first.get('total',0);drops=last.get('dropped',0)-first.get('dropped',0)
    activity=video.get('activity',{});moves=activity.get('scrolls',[])
    checks=dict(installed_outer=launch.get('installed_media') is True and launch.get('staged_media') is False,
        own_mse=launch.get('mse_representation_control') is True,
        expected_activity=launch.get('mse_programmatic_scroll') is scroll and activity.get('kind')==('programmatic-scroll' if scroll else 'none'),
        no_input=activity.get('inputInjected') is False and launch.get('synthetic_input') is False,
        all_frames=frames==video.get('expectedFrames')==360,zero_reported_drops=drops==0,
        completed=raw.get('automation_complete') is True and video.get('ended') is True and not video.get('playError'),
        three_representations=[(r['width'],r['height']) for r in video.get('representations',[])]==[(640,360),(1920,1080),(1280,720)],
        exact_final_pixels=video.get('finalPixels',{}).get('hash')=='e2d92265',
        media_providers=video.get('media_providers',{}).get('complete') is True,
        actual_system_graphics=raw.get('providers',{}).get('complete') is True and raw.get('providers',{}).get('expected_system_gpu') is True,
        renderer=video.get('renderer_capture',{}).get('source')=='renderer' and video.get('renderer_capture',{}).get('owner_input_overlap') is False,
        natural_quit_zero=raw.get('exit_before_cleanup')==0 and raw.get('exit_after_cleanup')==0)
    if scroll:checks['actual_programmatic_moves']=len(moves)>=50 and len({p['y'] for p in moves})>=5
    try:installed_family(launch['release']);checks['selected_manifest']=True
    except (KeyError,ValueError):checks['selected_manifest']=False
    quit=json.loads((run/'natural-quit.json').read_text()) if (run/'natural-quit.json').exists() else {}
    checks['family_gone']=quit.get('requested') is True and bool(quit.get('family_before')) and quit.get('family_remaining')==[] and quit.get('cleanup_signals')==[]
    text=(run/'browser/firefox.log').read_text(errors='replace')
    receipts=json.loads((run/'browser/native-logs.json').read_text()) if (run/'browser/native-logs.json').exists() else []
    for receipt in receipts:
        name=Path(receipt['path'])
        if name.is_absolute() or '..' in name.parts or name.parent!=Path('native-output'):raise ValueError('Unowned log path')
        data=(run/'browser'/name).read_bytes()
        if len(data)!=receipt['bytes'] or hashlib.sha256(data).hexdigest()!=receipt['sha256']:raise ValueError('Native log receipt differs')
        text+='\n'+data.decode(errors='replace')
    def fields(line):return dict(re.findall(r'([A-Za-z0-9_-]+)=(\S+)',line))
    rings=[fields(line) for line in text.splitlines() if line.startswith('SFMEDIA_RING ')]
    pcm=[fields(line) for line in text.splitlines() if line.startswith('SFHAL_PCM ')]
    parent=str(raw.get('marionette',{}).get('capabilities',{}).get('moz:processID'))
    own_rings=[r for r in rings if r.get('pid')==parent and r.get('rate')=='48000' and 'read' in r and 'write' in r]
    checks['own_hal_accepts']=any(int(r['read'])>=240000 and int(r['write'])>=240000 for r in own_rings)
    checks['normalized_audio']='SF_AU_CLOCK normalized=1 ' in text
    checks['nonzero_pcm']=any(r.get('pid')==parent and int(r.get('samples','0'))>0 and float(r.get('square-sum','0'))>0 for r in pcm)
    checks['pcm_valid']=bool(pcm) and all(int(r.get(k,'0'))==0 for r in pcm if r.get('pid')==parent for k in ['errors','bad-buffers','nonfinite'])
    footers=[tuple(map(int,r)) for r in re.findall(r'SF_VT_GPU frames=(\d+) copied-bytes=(\d+) required=(\d+)',text)]
    positive=[r for r in footers if r[0]>0]
    checks['strict_shared_output']=bool(positive) and all(copied==0 and required==1 for frames,copied,required in positive)
    def stats(text):
        words=text.partition(': ')[2].split()
        if len(words)%2:raise ValueError('Incomplete decoder counters')
        return {words[i]:int(words[i+1],0) for i in range(0,len(words),2)}
    delta={}
    if case:
        a,b=stats(case['avd_before']),stats(case['avd_after']);delta={k:b[k]-a[k] for k in ['jobs','done','errors','timeouts','dart_faults','quarantined_desc']}
    checks['decoder_jobs']=delta.get('jobs')==delta.get('done')==360 and all(delta.get(k)==0 for k in ['errors','timeouts','dart_faults','quarantined_desc'])
    gpu={k:case.get('gpu_after',{}).get(k,-1)-case.get('gpu_before',{}).get(k,0) for k in ['cpu_upload_bytes','cpu_readback_bytes','gpu_blit_bytes']}
    checks['gpu_transport']=gpu['cpu_upload_bytes']==gpu['cpu_readback_bytes']==0 and gpu['gpu_blit_bytes']>0
    gaps=[b['wall']-a['wall'] for a,b in zip(video.get('presented',[]),video.get('presented',[])[1:])]
    callbacks=video.get('presented',[])
    preview_gap=None
    if len(callbacks)>=2 and callbacks[0]['mediaTime']==0:
        preview_gap=dict(wall_ms=gaps[0],media_advance_ms=1000*(callbacks[1]['mediaTime']-callbacks[0]['mediaTime']),
            wall_minus_media_ms=gaps[0]-1000*(callbacks[1]['mediaTime']-callbacks[0]['mediaTime']),
            interpretation='Initial preview-to-progress interval, not an attributed scroll or panel stall; compare actual audio-clock/output preroll separately')
    return dict(passed=all(checks.values()),checks=checks,decoded_frames=frames,reported_dropped=drops,
        representations=video.get('representations'),activity=dict(kind=activity.get('kind'),moves=len(moves)),
        rvfc=dict(count=len(gaps),median_ms=statistics.median(gaps) if gaps else None,max_ms=max(gaps) if gaps else None),
        initial_preview_gap=preview_gap,remaining_callback_max_ms=max(gaps[1:]) if len(gaps)>1 else None,
        own_hal=own_rings,decoder_delta=delta,gpu_delta=gpu,raw_vt_footers=footers,
        natural_quit=quit,capture=video.get('renderer_capture'),visual_review='REQUIRED',
        limits='Own preloaded local H264/AAC representation sequence; not Internet DASH adaptation. Programmatic scrolling is not trusted pointer/wheel input. PCM metering adds diagnostic cost: no performance qualification, acoustic sync, exact server-slot destruction or panel cadence proof.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--scroll',action='store_true');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();result=analyze(a.run,a.scroll);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ['passed','checks','decoded_frames','reported_dropped','activity','rvfc']}))
    raise SystemExit(0 if result['passed'] else 1)
