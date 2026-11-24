#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize owned copy/shared browser observations, never infer panel FPS.

Historical CPU-scene files are not renderer evidence. VT finalizer rows can
repeat; retain them and match the unique per-clip counter, never sum them.
"""
import argparse
import json
from pathlib import Path
import re
import statistics


def avd(text):
    words=text.partition(': ')[2].split()
    if len(words)%2:raise ValueError('Incomplete decoder statistics')
    return {words[i]:int(words[i+1],0) for i in range(0,len(words),2)}


def case_summary(case,footers,mode):
    video=case['video'];source=video.get('source','')
    if mode not in ['copy','shared']:raise ValueError('Unknown comparison mode')
    if not source.startswith('file:///private/var/db/sffx/') or any(x in source for x in ['..','%','?','#']):
        raise ValueError('Own private file fixture required; cold-byte admission remains separate')
    if source.endswith('pattern-1080p60-h264.mp4'):expected,width,height=600,1920,1080
    elif source.endswith('pattern-4k30-hevc.mp4'):expected,width,height=300,3840,2160
    else:raise ValueError('Only the two frozen high-rate fixtures are admitted')
    quality=video['quality'][-1];initial=video.get('initialQuality',{})
    frames=quality['total']-initial.get('total',0)
    unit=width*height*3//2
    # The producer's `frames` field counts shared frames only. In forced-copy
    # mode it stays0; identify the frozen clip by its exact visible-byte total.
    candidates=sorted({row for row in footers if
        (mode=='shared' and row[0]==expected and row[2]==1) or
        (mode=='copy' and row[0]==0 and row[2]==0 and row[1]==unit*frames)})
    counter=dict(zero_copy_frames=candidates[0][0],copied_bytes=candidates[0][1],required=candidates[0][2]) if len(candidates)==1 else None
    if counter:
        counter['visible_nv12_frame_equivalents_copied']=counter['copied_bytes']/unit
        counter['pixel_copies_per_decoded_frame']=counter['copied_bytes']/unit/frames if frames else None
    before,after=avd(case['avd_before']),avd(case['avd_after'])
    delta={k:after[k]-before[k] for k in ['jobs','done','errors','timeouts','dart_faults','quarantined_desc']}
    gpu={k:case['gpu_after'][k]-case['gpu_before'][k] for k in ['cpu_upload_bytes','cpu_readback_bytes','gpu_blit_bytes','direct_alias_commits']}
    if any(v<0 for v in [*delta.values(),*gpu.values()]):raise ValueError('Counters reset; no zero-cost inference')
    gaps=[b['wall']-a['wall'] for a,b in zip(video.get('presented',[]),video.get('presented',[])[1:])]
    cpu=video.get('owned_process_cpu',{}).get('comparison',{})
    capture=video.get('renderer_capture',{})
    return dict(source=source,expected_frames=expected,decoded_frames=frames,dropped=quality['dropped']-initial.get('dropped',0),
        ended=video.get('ended'),play_error=video.get('playError'),final_pixels=video.get('finalPixels'),
        vt_counter=counter,vt_counter_candidates=candidates,avd_delta=delta,gpu_delta=gpu,
        cpu_percent_one_core=cpu.get('percent_one_core_by_type'),matched_family_cpu=cpu.get('matched_family_percent_one_core'),
        cpu_family_complete=cpu.get('family_complete'),cpu_limits=cpu.get('limits'),
        rvfc_ms=dict(count=len(gaps),median=statistics.median(gaps) if gaps else None,maximum=max(gaps) if gaps else None),
        actual_renderer_source_observed=capture.get('source')=='renderer',capture=capture,
        capture_error=video.get('renderer_capture_error'),visual_review='REQUIRED; no automatic visible PASS')


def analyze(stage):
    phases={}
    for mode in ['copy','shared']:
        run=stage/('target-'+mode)/(mode+'-run');raw=json.loads((run/'browser/result.json').read_text())
        text=(run/'browser/firefox.log').read_text(errors='replace')
        footers=[tuple(map(int,m)) for m in re.findall(r'SF_VT_GPU frames=(\d+) copied-bytes=(\d+) required=(\d+)',text)]
        phases[mode]=dict(browser_survived=raw.get('browser_survived_run'),measurement_passed=raw.get('measurement_passed'),
            raw_vt_footer_rows=footers,clips=[case_summary(c,footers,mode) for c in raw['video_measurements']])
    return dict(phases=phases,performance_qualified=False,
        limits='One owned pair. CPU is percent of one core and may omit unmatched processes. CGL counters exclude QuartzCore CPU uploads and verification readbacks. rVFC cadence is not panel cadence. Global decoder deltas may include other clients; actual pictures need visual review. Duplicate VT footer rows are not added.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();result=analyze(a.stage);a.output.write_text(json.dumps(result,indent=2)+'\n')
    for mode,phase in result['phases'].items():
        for clip in phase['clips']:print(mode,clip['source'].rsplit('/',1)[-1],clip['decoded_frames'],clip['dropped'],clip['vt_counter'],clip['cpu_percent_one_core'],clip['actual_renderer_source_observed'])
