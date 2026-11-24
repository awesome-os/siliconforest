#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Correlate owned browser phases with native output aggregates, never PCM.

Darwin Python monotonic and C CLOCK_MONOTONIC have different epochs here.
Use their independently recorded wall/monotonic pairs before selecting bins.
This is an output-boundary test, not acoustic output or an underrun test.
"""
import argparse
from collections import defaultdict
import json
import hashlib
import math
from pathlib import Path
import statistics


def fields(line):
    return dict(word.split('=',1) for word in line.split()[1:] if '=' in word)


def analyze(result,text,expect_installed_clock=False):
    if result.get('error') or not result.get('diagnostic_complete'):
        raise ValueError('Incomplete browser state run')
    phases=result.get('state_controls',{}).get('phases',[])
    required=['full','half','muted','unmuted','paused','restarted','two-tabs',
              'second-with-first-paused','second-with-first-closed','all-media-tabs-closed']
    if [p['name'] for p in phases]!=required:raise ValueError('Incomplete ordered phase controls')
    pairs=[row['epoch']-row['monotonic'] for row in result.get('timeline',[]) if 'epoch' in row and 'monotonic' in row]
    if not pairs or max(pairs)-min(pairs)>.02:raise ValueError('Missing/unstable controller clock bridge')
    python_offset=statistics.median(pairs);c_offsets=defaultdict(list);meters=[]
    for line in text.splitlines():
        if line.startswith('FXHAL '):
            f=fields(line)
            if 'epoch' in f and 'monotonic' in f:c_offsets[int(f['pid'])].append(float(f['epoch'])-float(f['monotonic']))
        elif line.startswith('SFHAL_PCM '):
            f=fields(line)
            if 'context' in f:
                meters.append(f)
                if 'epoch' in f:c_offsets[int(f['pid'])].append(float(f['epoch'])-float(f['monotonic']))
    if not meters:raise ValueError('Native PCM meter was not observed')
    offsets={}
    for pid,values in c_offsets.items():
        if max(values)-min(values)>.02:raise ValueError('Unstable native clock bridge')
        offsets[pid]=statistics.median(values)-python_offset
    streams=defaultdict(list)
    for f in meters:
        pid=int(f['pid'])
        if pid not in offsets:raise ValueError('Meter process has no native clock bridge')
        if any(int(f[k]) for k in ['errors','bad-buffers','nonfinite']):raise ValueError('Invalid native output observations')
        row={k:int(f[k]) for k in ['calls','frames','samples']}
        row.update(time=float(f['monotonic'])+offsets[pid],power_sum=float(f['square-sum']))
        if not math.isfinite(row['power_sum']) or row['power_sum']<0:raise ValueError('Invalid output power')
        streams[pid,int(f['context'])].append(row)
    rows=[]
    for phase in phases:
        start,end=float(phase['start']),float(phase['end'])
        if not math.isfinite(start+end) or end-start<1.2:raise ValueError('Phase too short')
        # Exclude at least600ms, longer than the measured~200ms output lead.
        # This diagnoses the software boundary after drain, not the speaker.
        lo=start+(.9 if phase['name']=='all-media-tabs-closed' else .6)
        total_samples=total_calls=total_frames=0;total_power=0;observed=[]
        for key,stream in streams.items():
            selected=[r for r in stream if lo<=r['time']<=end]
            if len(selected)<2 or selected[-1]['time']-selected[0]['time']<.45:continue
            first,last=selected[0],selected[-1]
            delta={k:last[k]-first[k] for k in ['samples','calls','frames','power_sum']}
            if any(v<0 for v in delta.values()):raise ValueError('Reset/nonmonotonic meter counters')
            if delta['samples']!=2*delta['frames']:raise ValueError('Packed stereo frame mismatch')
            total_samples+=delta['samples'];total_calls+=delta['calls'];total_frames+=delta['frames'];total_power+=delta['power_sum']
            observed.append(dict(pid=key[0],context=key[1],seconds=last['time']-first['time'],**delta))
        if not observed:raise ValueError('No qualified native observation interval for '+phase['name'])
        power=total_power/total_samples if total_samples else 0
        silent=phase['name'] in ['muted','paused','all-media-tabs-closed']
        passed=(total_power==0) if silent else total_samples>0 and power>1e-12
        rows.append(dict(name=phase['name'],output_boundary_passed=passed,expected_silent=silent,
                         samples=total_samples,frames=total_frames,calls=total_calls,power=power,streams=observed))
    ratio=rows[1]['power']/rows[0]['power'] if rows[0]['power'] else None
    half_pass=ratio is not None and .20<=ratio<=.30
    natural_exit=not result.get('before_cleanup_owned') and not result.get('cleanup_signals')
    answer=dict(phases=rows,half_to_full_power_ratio=ratio,half_volume_passed=half_pass,
                native_to_controller_seconds=offsets,
                output_boundary_passed=all(r['output_boundary_passed'] for r in rows) and half_pass,
                natural_exit_without_cleanup_signals=natural_exit,
                acoustic_proof=False,glitch_free_proof=False,performance_qualified=False)
    if expect_installed_clock:
        rings=[fields(line) for line in text.splitlines() if line.startswith('SFMEDIA_RING ')]
        parent=str(result.get('parent_pid'))
        checks=dict(installed_outer=result.get('installed_outer_launcher') is True,
            admitted_expectation=result.get('expected_installed_clock') is True,
            no_private_selection=result.get('normalized_audio_family') is False,
            image_proof=result.get('media_providers',{}).get('complete') is True,
            natural_quit_zero=result.get('exit_before_cleanup')==0 and result.get('launcher_final_exit')==0 and natural_exit,
            clock_marker='SF_AU_CLOCK normalized=1 ' in text,
            own_hal_accepts=any(r.get('pid')==parent and r.get('rate')=='48000' and
                               int(r.get('write','0'))>0 and int(r.get('read','0'))>0 for r in rings))
        manifest=result.get('selected_manifest') or {}
        checks['selected_manifest']=manifest.get('audio_output_clock') is True and manifest.get('audio_override')==(
            '/usr/local/lib/siliconforest/firefox/160-system1/overrides/audio-clock-v1/Frameworks')
        answer['installed_clock_gate']=dict(checks=checks,passed=all(checks.values()))
        answer['output_boundary_passed'] &= all(checks.values())
    return answer


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('run',type=Path);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--native-log',type=Path,help='Explicit recovered original own runtime stdout, retained separately from the empty outer log')
    p.add_argument('--expect-installed-clock',action='store_true',help='Additionally require installed selection, image/clock/HAL evidence and natural Quit0')
    a=p.parse_args();raw=json.loads((a.run/'result.json').read_text());text=(a.run/'firefox.log').read_text(errors='replace')
    if raw.get('native_log_collection_error'):raise ValueError('Incomplete native log collection')
    for row in raw.get('native_log_files',[]):
        path=(a.run/row['path']).resolve()
        if not path.is_relative_to(a.run.resolve()):raise ValueError('Native log escapes own run')
        data=path.read_bytes()
        if len(data)!=row['bytes'] or hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('Native log receipt mismatch')
        text+='\n'+data.decode(errors='replace')
    if a.native_log:text+='\n'+a.native_log.read_text(errors='replace')
    result=analyze(raw,text,a.expect_installed_clock)
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
    return int(not result['output_boundary_passed'])


if __name__=='__main__':raise SystemExit(main())
