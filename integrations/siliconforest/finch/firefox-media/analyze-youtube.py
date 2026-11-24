#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Admit real1080p60 decoded streaming observations, never infer panel cadence."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import re
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from video_selection import installed_family


def analyze(run,require_hardware=False):
    launch=json.loads((run/'cadence-launch.json').read_text())
    raw=json.loads((run/'browser/result.json').read_text())
    streaming=raw.get('streaming',{});video=streaming.get('playback',{})
    before,after=video.get('before',{}),video.get('after',{})
    frames=after.get('total',0)-before.get('total',0)
    drops=after.get('dropped',0)-before.get('dropped',0)
    media=video.get('endMedia',0)-video.get('startMedia',0)
    fps=frames/media if media>0 else None
    checks=dict(own_quality_case=launch.get('youtube_1080') is True,
        no_private_selection=launch.get('staged_media') is False and not launch.get('graphics_override'),
        unprofiled=launch.get('youtube_profile') is False and launch.get('boundary_trace') is False,
        automation_complete=raw.get('automation_complete') is True,
        actual_graphics=raw.get('providers',{}).get('complete') is True and raw.get('providers',{}).get('expected_system_gpu') is True,
        original_streaming_complete=streaming.get('complete') is True,
        exact_public_video=video.get('videoId')=='aqz-KE-bpKQ' and video.get('ad') is False,
        actual_dimensions=video.get('width',0)>=1920 and video.get('height',0)>=1080,
        twelve_seconds=media>=12 and video.get('wallSeconds',0)>=12,
        decoded_60fps=fps is not None and math.isfinite(fps) and 55<=fps<=65,
        no_reported_drops=drops==0 and frames>0,
        quality_admitted=video.get('quality_admission',{}).get('complete') is True,
        actual_media=video.get('media_providers',{}).get('complete') is True,
        no_input=launch.get('synthetic_input') is False and raw.get('synthetic_input') is False,
        renderer_route=video.get('renderer_capture',{}).get('source')=='renderer' and
            video.get('renderer_capture',{}).get('owner_input_overlap') is False and
            video.get('renderer_capture',{}).get('input_injected') is False,
        natural_quit_zero=raw.get('exit_before_cleanup')==0 and raw.get('exit_after_cleanup')==0)
    selected=video.get('selected_format_after',{}).get('selectedFormat') or {}
    checks['source_60fps']=55<=selected.get('fps',0)<=65 and selected.get('width',0)>=1920 and selected.get('height',0)>=1080
    try:installed_family(launch['release']);checks['selected_manifest']=True
    except (KeyError,ValueError):checks['selected_manifest']=False
    quit=json.loads((run/'natural-quit.json').read_text()) if (run/'natural-quit.json').is_file() else {}
    checks['family_gone_before_cleanup']=quit.get('requested') is True and bool(quit.get('family_before')) and quit.get('family_remaining')==[] and quit.get('cleanup_signals')==[]
    gaps=[b['wall']-a['wall'] for a,b in zip(video.get('frames',[]),video.get('frames',[])[1:])]
    native=(run/'browser/firefox.log').read_text(errors='replace')
    log_receipt=run/'browser/native-logs.json'
    for receipt in json.loads(log_receipt.read_text()) if log_receipt.exists() else []:
        name=Path(receipt['path'])
        if name.is_absolute() or '..' in name.parts or name.parent!=Path('native-output'):raise ValueError('Unowned native log')
        data=(run/'browser'/name).read_bytes()
        if len(data)!=receipt['bytes'] or hashlib.sha256(data).hexdigest()!=receipt['sha256']:raise ValueError('Native log receipt differs')
        native+='\n'+data.decode(errors='replace')
    footers=sorted({tuple(map(int,row)) for row in re.findall(r'SF_VT_GPU frames=(\d+) copied-bytes=(\d+) required=(\d+)',native)})
    def counters(text):
        words=(text or '').partition(': ')[2].split()
        if len(words)%2:raise ValueError('Incomplete decoder counters')
        return {words[i]:int(words[i+1],0) for i in range(0,len(words),2)}
    a,b=counters(streaming.get('avd_before')),counters(streaming.get('avd_after'))
    delta={k:b[k]-a[k] for k in ['jobs','done','errors','timeouts','dart_faults','quarantined_desc'] if k in a and k in b}
    if require_hardware:
        preferences=json.loads((run/'owned-startup-preferences.json').read_text())
        if launch.get('installed_codec_policy') is True:
            from codec_policy_guard import checks as policy_checks
            checks.update(policy_checks(run,False))
            checks['installed_policy_manifest']=launch['release'].get('av1_enabled') is False
            checks['actual_installed_av1_default']=preferences.get('media.av1.enabled',{}).get('value') is False
        else:checks['own_private_av1_disabled']=launch.get('youtube_disable_av1') is True and preferences.get('media.av1.enabled',{}).get('value') is False
        checks['selected_hardware_codec']=bool(re.search(r'codecs="(?:vp09|vp9|avc1)',selected.get('mimeType','')))
        positive=[row for row in footers if row[0]>0]
        checks['owned_shared_frames']=any(row[0]>=frames for row in positive) and all(row[1]==0 for row in positive)
        checks['hardware_jobs']=delta.get('jobs',-1)>=frames and delta.get('done',-1)>=frames and all(delta.get(k)==0 for k in ['errors','timeouts','dart_faults','quarantined_desc'])
        checks['power_efficient_declaration']=video.get('quality_admission',{}).get('capabilities',{}).get('powerEfficient') is True
    return dict(passed=all(checks.values()),checks=checks,decoded_frames=frames,reported_dropped=drops,
        decoded_frames_per_media_second=fps,media_seconds=media,wall_seconds=video.get('wallSeconds'),
        dimensions=[video.get('width'),video.get('height')],quality=video.get('quality_admission'),
        selected_format_after=video.get('selected_format_after'),
        reused_media_profile=launch.get('reused_media_profile'),private_av1_disabled=launch.get('youtube_disable_av1'),
        start_media=video.get('startMedia'),end_media=video.get('endMedia'),diagnostic_pixels=video.get('diagnosticPixels'),
        native_vt_counters=footers,decoder_delta=delta,
        owned_cpu=video.get('owned_process_cpu',{}).get('comparison'),
        rvfc=dict(count=len(gaps),median_ms=statistics.median(gaps) if gaps else None,max_ms=max(gaps) if gaps else None),
        avd_before=streaming.get('avd_before'),avd_after=streaming.get('avd_after'),
        capture=video.get('renderer_capture'),natural_quit=quit,visual_review='REQUIRED',
        limits='Generated requests do not prove quality; this gate uses actual dimensions and decoded/media-time deltas. CPU is one measured interval. Global decoder counters can include other clients and do not identify the codec. No pointer/key input, acoustic output, panel cadence, entire-browser zero-copy or full-site qualification.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--require-hardware',action='store_true');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();result=analyze(a.run,a.require_hardware);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ['passed','checks','decoded_frames','reported_dropped','decoded_frames_per_media_second','dimensions','rvfc']}))
    raise SystemExit(0 if result['passed'] else 1)
