#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Installed default plus actual generated AV1 opt-in; keep raw survival verdicts."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from codec_policy_guard import checks as policy_checks
from video_selection import installed_family


def analyze(run,switchback=False):
    if not switchback:
        spec=importlib.util.spec_from_file_location('yt_policy',Path(__file__).with_name('analyze-youtube.py'))
        yt=importlib.util.module_from_spec(spec);spec.loader.exec_module(yt)
        return yt.analyze(run,require_hardware=True)
    launch=json.loads((run/'cadence-launch.json').read_text());raw=json.loads((run/'browser/result.json').read_text())
    rows=raw.get('video_measurements',[]);video=rows[0]['video'] if len(rows)==1 else {}
    first=video.get('initialQuality',{});last=(video.get('quality') or [{}])[-1]
    frames=last.get('total',0)-first.get('total',0);drops=last.get('dropped',0)-first.get('dropped',0)
    checks=policy_checks(run,True)
    checks.update(installed_manifest=launch.get('installed_codec_policy') is True and launch['release'].get('av1_enabled') is False,
        switchback=launch.get('av1_switchback') is True and launch.get('staged_media') is False,
        one_av1_fixture=len(rows)==1 and video.get('source','').startswith('file:///private/var/db/sffx/') and video.get('source','').endswith('/media/codecs/av1-1080p60.webm'),
        complete=raw.get('automation_complete') is True and video.get('ended') is True and not video.get('playError'),
        all600=frames==600 and drops==0,
        actual_dimensions=(video.get('finalPixels',{}).get('videoWidth'),video.get('finalPixels',{}).get('videoHeight'))==(1920,1080),
        picture=video.get('finalPixels',{}).get('nonBlack',0)>0,
        actual_media=video.get('media_providers',{}).get('complete') is True,
        actual_graphics=raw.get('providers',{}).get('complete') is True and raw.get('providers',{}).get('expected_system_gpu') is True,
        actual_renderer=video.get('renderer_capture',{}).get('source')=='renderer' and video.get('renderer_capture',{}).get('owner_input_overlap') is False,
        no_input=launch.get('synthetic_input') is False and raw.get('synthetic_input') is False,
        natural_quit_zero=raw.get('exit_before_cleanup')==0 and raw.get('exit_after_cleanup')==0)
    try:installed_family(launch['release']);checks['selected_video_family']=True
    except (KeyError,ValueError):checks['selected_video_family']=False
    quit=json.loads((run/'natural-quit.json').read_text()) if (run/'natural-quit.json').exists() else {}
    checks['family_gone']=quit.get('requested') is True and bool(quit.get('family_before')) and quit.get('family_remaining')==[] and quit.get('cleanup_signals')==[]
    declarations=video.get('codec_declarations',{}).get('results',[])
    av1=next((r for r in declarations if r.get('name')=='av1-1080p60'),{})
    queries=av1.get('queries',[])
    checks['software_av1_declaration']=len(queries)==2 and all(r.get('supported') is True and r.get('powerEfficient') is False for r in queries)
    return dict(passed=all(checks.values()),checks=checks,decoded_frames=frames,reported_dropped=drops,
        av1_capabilities=av1,final_pixels=video.get('finalPixels'),capture=video.get('renderer_capture'),natural_quit=quit,
        visual_review='REQUIRED: inspect the actual GPU image',
        limits='Fresh own profile, installed launcher opt-in and actual generated AV1 decoding. Not all AV1 content, acoustic output, panel cadence or a guarantee of default AV1-only coverage. Original survival-oriented false verdict after intentional Quit is retained.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--switchback',action='store_true');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=analyze(a.run,a.switchback);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:r[k] for k in ['passed','checks','decoded_frames','reported_dropped']}))
    raise SystemExit(0 if r['passed'] else 1)
