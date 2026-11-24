#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Locate observed cadence boundaries; never turn missing hooks into zero cost."""
import argparse,json,math,re
from collections import defaultdict
from pathlib import Path


def distribution(values):
    values=sorted(values)
    if not values:return None
    def p(q):
        point=(len(values)-1)*q;a=math.floor(point);b=math.ceil(point)
        return values[a]+(values[b]-values[a])*(point-a)
    return dict(n=len(values),minimum=values[0],p50=p(.5),p95=p(.95),p99=p(.99),maximum=values[-1])


def analyze(text,epoch_start=None,epoch_end=None):
    headers={};rows=[];errors=[];end=[]
    for line in text.splitlines():
        if not line.startswith('SFCADENCE'):continue
        d=dict(re.findall(r'([A-Za-z0-9_-]+)=(\S+)',line));kind=line.split()[0]
        if kind=='SFCADENCE_BEGIN':
            pid=int(d['pid'])
            if pid in headers:raise ValueError('Duplicate process timebase; split reused PIDs/runs')
            if int(d['numer'])<=0 or int(d['denom'])<=0:raise ValueError('Invalid actual timebase')
            headers[pid]=d
        elif kind=='SFCADENCE':rows.append(d)
        elif kind=='SFCADENCE_ERROR':errors.append(d)
        elif kind in ['SFCADENCE_END','SFCADENCE_FLUSH']:end.append(d)
    groups=defaultdict(list);missing_headers=set();seen_sequences=set()
    for row in rows:
        pid=int(row['pid']);header=headers.get(pid)
        if header is None:missing_headers.add(pid);continue
        identity=pid,int(row['sequence'])
        if identity in seen_sequences:raise ValueError('Duplicate event row')
        seen_sequences.add(identity)
        ratio=int(header['numer'])/int(header['denom'])
        stamp=float(header['epoch'])+(int(row['host'])-int(header['host']))*ratio/1e9
        if epoch_start is not None and stamp<epoch_start:continue
        if epoch_end is not None and stamp>epoch_end:continue
        key=(pid,row['event'],int(row['context']),int(row['plane']) if row['event']=='cgl-plane' else None)
        groups[key].append(dict(host_ns=int(row['host'])*ratio,duration_ms=int(row['duration'])*ratio/1e6,
                                status=int(row['status']),pts=int(row['pts']),scale=int(row['scale']),token=row['token']))
    summaries=[]
    for (pid,event,context,plane),observed in sorted(groups.items(),key=lambda pair:repr(pair[0])):
        observed.sort(key=lambda row:row['host_ns'])
        intervals=[(b['host_ns']-a['host_ns'])/1e6 for a,b in zip(observed,observed[1:])]
        span=(observed[-1]['host_ns']-observed[0]['host_ns'])/1e9
        summaries.append(dict(pid=pid,event=event,context=context,plane=plane,records=len(observed),
          interval_ms=distribution(intervals),duration_ms=distribution([r['duration_ms'] for r in observed]),
          observed_events_per_second=(len(observed)-1)/span if span>0 else None,
          status_errors=sum(r['status']!=0 for r in observed),
          first_pts_seconds=observed[0]['pts']/observed[0]['scale'] if observed[0]['scale'] else None,
          last_pts_seconds=observed[-1]['pts']/observed[-1]['scale'] if observed[-1]['scale'] else None))
    kinds={key[1] for key in groups}
    return dict(process_headers=headers,summaries=summaries,errors=errors,missing_timebase_pids=sorted(missing_headers),
                unobserved_stages=sorted({'cv-callback','vt-output','cgl-plane'}-kinds),
                overflow=any(int(r.get('lost',0))>0 for r in end),flush_or_end_markers=end,
                observations_only=True,presented_fps_proved=False,
                limits='Diagnostic instrumented cadence only. Decoder prefetch/bursts are not presentation FPS. Missing hooks are unobserved, not free/zero. Pair same owned browser rVFC and Gecko media/IPC profile; qualify final performance without instrumentation.')


def browser_intervals(text,result):
    """Same-run fixture epochs, with callback observations kept distinct from FPS."""
    clips=[]
    for entry in result.get('video_measurements',[]):
        video=entry['video'];origin=video['timeOrigin']
        begin=(origin+video['measureStart'])/1000;end=(origin+video['measureEnd'])/1000
        if not math.isfinite(begin) or not math.isfinite(end) or end<=begin:
            raise ValueError('Incomplete browser measurement interval')
        callbacks=video.get('presented',[])
        walls=[r['wall'] for r in callbacks]
        if any(b<a for a,b in zip(walls,walls[1:])):
            raise ValueError('Nonmonotonic callback timestamps')
        gaps=[b-a for a,b in zip(walls,walls[1:])]
        serials=[r['presentedFrames'] for r in callbacks]
        serial_steps=[b-a for a,b in zip(serials,serials[1:])]
        clips.append(dict(source=video['source'],epoch_start=begin,epoch_end=end,
            requested_playback_rate=video.get('requestedPlaybackRate'),
            actual_playback_rate=video.get('actualPlaybackRate'),
            callback_records=len(callbacks),callback_interval_ms=distribution(gaps),
            reported_picture_serial_steps=distribution(serial_steps),
            reported_picture_steps_larger_than_one=sum(n>1 for n in serial_steps),
            initial_quality=video.get('initialQuality'),quality=video.get('quality'),
            play_error=video.get('playError'),decoder_error=video.get('error'),
            boundaries=analyze(text,begin,end),presented_fps_proved=False))
    return dict(clips=clips,observations_only=True,presented_fps_proved=False,
        limits='rVFC reports callbacks and picture serials, not independently observed panel presentations. Do not label serial jumps dropped/replaced frames. Epoch alignment is approximate: header host and realtime are sampled sequentially.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--epoch-start',type=float);p.add_argument('--epoch-end',type=float);p.add_argument('--browser-result',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    text=a.log.read_text(errors='replace')
    if a.browser_result and (a.epoch_start is not None or a.epoch_end is not None):
        p.error('Choose browser fixture intervals or an explicit epoch interval')
    result=browser_intervals(text,json.loads(a.browser_result.read_text())) if a.browser_result else analyze(text,a.epoch_start,a.epoch_end)
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
