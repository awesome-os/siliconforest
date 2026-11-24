#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Refuse incomplete installed-outer video admission; actual pictures need review."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from video_selection import installed_family,VIDEO_FAMILIES
spec=importlib.util.spec_from_file_location('video_pair',HERE/'analyze-video-pair.py')
pair=importlib.util.module_from_spec(spec);spec.loader.exec_module(pair)


def analyze(run,staged=False):
    launch=json.loads((run/'cadence-launch.json').read_text())
    raw=json.loads((run/'browser/result.json').read_text())
    checks={}
    try:installed_family(launch['release']);checks['selected_manifest']=True
    except (ValueError,KeyError):checks['selected_manifest']=False
    if staged:
        checks['staged_outer']=launch.get('installed_media') is False and launch.get('staged_media') is True
    else:
        checks['installed_outer']=launch.get('installed_media') is True and launch.get('staged_media') is False and not launch.get('private_quartzcore_copy')
    checks['canonical_graphics']=not any(launch.get(k) for k in ['graphics_override','private_quartzcore_copy','private_frame_identity_graphics'])
    checks['actual_graphics']=raw.get('providers',{}).get('complete') is True and raw.get('providers',{}).get('expected_system_gpu') is True
    checks['strict_mode']=launch.get('frame_mode')=='require'
    checks['unprofiled']=launch.get('boundary_trace') is False
    checks['no_input']=launch.get('synthetic_input') is False and raw.get('synthetic_input') is False
    checks['automation_complete']=raw.get('automation_complete') is True
    checks['natural_status0']=raw.get('exit_before_cleanup')==0 and raw.get('exit_after_cleanup')==0
    quit=json.loads((run/'natural-quit.json').read_text()) if (run/'natural-quit.json').is_file() else {}
    checks['natural_quit']=quit.get('requested') is True and bool(quit.get('family_before')) and quit.get('family_remaining')==[] and quit.get('cleanup_signals')==[]
    text=(run/'browser/firefox.log').read_text(errors='replace')
    receipts=json.loads((run/'browser/native-logs.json').read_text()) if (run/'browser/native-logs.json').is_file() else []
    for receipt in receipts:
        name=Path(receipt['path'])
        if name.is_absolute() or '..' in name.parts or name.parent!=Path('native-output'):
            raise ValueError('Native log receipt escapes this run')
        data=(run/'browser'/name).read_bytes()
        if len(data)!=receipt['bytes'] or hashlib.sha256(data).hexdigest()!=receipt['sha256']:
            raise ValueError('Collected native log receipt differs')
        text+='\n'+data.decode(errors='replace')
    if staged:checks['native_stdout_captured']=bool(text)
    else:checks['native_output_collected']=bool(receipts)
    footers=[tuple(map(int,m)) for m in re.findall(r'SF_VT_GPU frames=(\d+) copied-bytes=(\d+) required=(\d+)',text)]
    clips=[pair.case_summary(c,footers,'shared') for c in raw.get('video_measurements',[])]
    checks['two_frozen_clips']=len(clips)==2 and {c['expected_frames'] for c in clips}=={600,300}
    hashes={600:'b424348',300:'88999ae5'}
    for index,clip in enumerate(clips):
        prefix=str(index)+'_'
        checks[prefix+'all_frames']=clip['decoded_frames']==clip['expected_frames'] and clip['dropped']==0 and clip['ended'] is True and not clip['play_error']
        checks[prefix+'shared']=bool(clip['vt_counter']) and clip['vt_counter']['copied_bytes']==0 and clip['vt_counter']['zero_copy_frames']==clip['expected_frames']
        delta=clip['avd_delta']
        checks[prefix+'decoder_jobs']=delta['jobs']==delta['done']==clip['expected_frames'] and all(delta[k]==0 for k in ['errors','timeouts','dart_faults','quarantined_desc'])
        checks[prefix+'final_pixels']=bool(clip['final_pixels']) and clip['final_pixels'].get('hash')==hashes[clip['expected_frames']]
        checks[prefix+'gpu_transport']=clip['gpu_delta']['cpu_upload_bytes']==clip['gpu_delta']['cpu_readback_bytes']==0 and clip['gpu_delta']['gpu_blit_bytes']>0
        checks[prefix+'renderer_route']=clip['actual_renderer_source_observed'] and not clip['capture_error'] and clip['capture'].get('input_injected') is False and clip['capture'].get('owner_input_overlap') is False
        observed=raw['video_measurements'][index]['video'].get('media_providers',{})
        processes=observed.get('processes',[])
        checks[prefix+'images']=observed.get('complete') is True and {'browser','rdd'}.issubset({p['type'] for p in processes}) and all(
            p.get('passed') is True and all(p.get('staged_media_checks',{}).get(name) is True for name in ['CoreVideo','VideoToolbox','VideoToolbox.pd']) for p in processes)
        if not staged:
            expected=VIDEO_FAMILIES.get(launch.get('release',{}).get('video_override'))
            actual=[re.findall(r'\sCoreVideo\s+<([^>]+)>','\n'.join(p.get('images',[]))) for p in processes]
            checks[prefix+'corevideo_uuid']=bool(expected) and bool(actual) and all(
                len(found)==1 and found[0].replace('-','').lower()==expected for found in actual)
        audio='/usr/local/lib/siliconforest/firefox/160-system1/overrides/audio-clock-v1/Frameworks/AudioToolbox.framework/'
        checks[prefix+'retained_audio']=bool(processes) and all(
            len(p.get('selected',{}).get(name,[]))==1 and p['selected'][name][0].startswith(audio)
            for p in processes for name in ['AudioToolbox','AudioToolbox.output-sf'])
    return dict(passed=all(checks.values()),checks=checks,clips=clips,
        qualification='private media candidate' if staged else 'installed outer launcher',
        visual_review='REQUIRED: inspect both actual renderer images before overall acceptance',
        shared_runner_verdict={k:raw.get(k) for k in ['browser_survived_run','measurement_passed','exit_before_cleanup','exit_after_cleanup']},
        limits='The shared survival-oriented runner marks intentional Quit as not surviving; its original verdict is retained. This admission requires completed pictures followed by natural status0 and no live family before cleanup. Not acoustic sync, panel cadence or whole-browser zero-copy proof.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--staged-media',action='store_true',help='Qualify a private media candidate; never an installed-outer pass')
    a=p.parse_args();result=analyze(a.run,staged=a.staged_media);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=result['passed'],checks=result['checks'])))
    raise SystemExit(0 if result['passed'] else 1)
