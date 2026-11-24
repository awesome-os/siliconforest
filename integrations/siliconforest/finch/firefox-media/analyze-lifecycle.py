#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Classify observed HAL activity without inferring a browser/acoustic stop."""
import argparse,json,re
from pathlib import Path


def analyze(text,after=None):
    records=[];incomplete='tracking-capacity-exhausted=1' in text
    for line in text.splitlines():
        if not line.startswith(('SFMEDIA_AU ','SFMEDIA_RING ','SFMEDIA_CALLBACK ')):continue
        values=dict(re.findall(r'([A-Za-z0-9_-]+)=(\S+)',line));values['kind']=line.split()[0]
        if values.get('tracked')=='0':incomplete=True
        records.append(values)
    records.sort(key=lambda r:float(r.get('monotonic',0)))
    states=[r for r in records if r['kind']=='SFMEDIA_AU' and r.get('phase')=='state']
    pairs=sorted({(int(r['pid']),int(r['device'])) for r in states if int(r['device'])!=0})
    results=[]
    for pid,device in pairs:
        events=[r for r in states if int(r['pid'])==pid and int(r['device'])==device and int(r['status'])==0]
        latest={}
        for r in sorted(events,key=lambda r:float(r['monotonic'])):latest[r['unit']]=r
        base=dict(pid=pid,device=device,latest_units=latest,tracking_incomplete=incomplete)
        if incomplete:base['classification']='INCOMPLETE_TRACKING'
        elif any(len({int(e['device']) for e in states if int(e['pid'])==pid and e['unit']==unit and int(e['device'])!=0})>1 for unit in latest):
            # Callback counters aggregate an address, not a device/generation.
            # Reuse on another device cannot be assigned safely to this ring.
            base['classification']='AMBIGUOUS_UNIT_ADDRESS_DEVICE_REUSE'
        elif not latest:base['classification']='NO_SUCCESSFUL_LIFECYCLE'
        elif any(int(r['active']) for r in latest.values()):
            base['classification']='TRACKED_STREAM_REMAINS_ACTIVE_NOT_AN_AUDIBLE_LEAK_VERDICT'
        else:
            anchor=max(float(r['monotonic']) for r in latest.values())
            if after is not None:anchor=max(anchor,after)
            rings=[r for r in records if r['kind']=='SFMEDIA_RING' and int(r.get('pid',-1))==pid and int(r.get('device',-1))==device and r.get('protocol')=='2' and float(r['monotonic'])>=anchor+.1]
            base.update(anchor_monotonic=anchor,ring_samples=len(rings))
            if len(rings)<2 or float(rings[-1]['monotonic'])-float(rings[0]['monotonic'])<.25:
                base['classification']='INSUFFICIENT_POST_TEARDOWN_RING_EVIDENCE'
            else:
                cursors=[int(r['read']) for r in rings]
                base.update(first_cursor=cursors[0],last_cursor=cursors[-1],observation_span_s=float(rings[-1]['monotonic'])-float(rings[0]['monotonic']))
                if any(b<a for a,b in zip(cursors,cursors[1:])):
                    base['classification']='RING_CURSOR_RESET_OR_INCONSISTENT'
                elif any(int(r['active-tracked-units']) for r in rings):
                    base['classification']='ANOTHER_TRACKED_UNIT_ACTIVE'
                elif cursors[-1]!=cursors[0] or any(int(r['running']) for r in rings):
                    base['classification']='HAL_ACTIVITY_CONTINUES_NOT_AN_AUDIBLE_LEAK_VERDICT'
                else:
                    callback_units={r['unit'] for r in events if r['event']=='start'}
                    callback_report={};observed=True;changing=False;inflight=False
                    for unit in sorted(callback_units):
                        rows=[r for r in records if r['kind']=='SFMEDIA_CALLBACK' and int(r['pid'])==pid and r['unit']==unit and float(r['monotonic'])>=anchor+.1]
                        enough=len(rows)>=2 and float(rows[-1]['monotonic'])-float(rows[0]['monotonic'])>=.25
                        observed&=enough
                        counts=[int(r['calls']) for r in rows]
                        completions=[(int(r['successes']),int(r['errors'])) for r in rows if 'successes' in r and 'errors' in r]
                        observed&=len(completions)==len(rows)
                        changing|=bool(counts and max(counts)!=min(counts)) or len(set(completions))>1
                        inflight|=any(calls!=successes+errors for calls,(successes,errors) in zip(counts,completions))
                        callback_report[unit]=dict(samples=len(rows),counts=counts,completions=completions)
                    base['callbacks']=callback_report
                    if changing:base['classification']='CALLBACK_ACTIVITY_CONTINUES_HAL_RING_QUIET'
                    elif inflight:base['classification']='HAL_RING_QUIET_CALLBACK_COMPLETION_UNRESOLVED'
                    elif not observed or not callback_units:base['classification']='HAL_RING_QUIET_CALLBACKS_UNOBSERVED'
                    else:base['classification']='QUIESCENT_OWN_DEVICE_RING_AND_CALLBACKS'
        results.append(base)
    return dict(results=results,records=len(records),close_mark_supplied=after is not None,
                browser_document_or_acoustic_stop_proved=False,
                limits='Read-only own process/device evidence. Active or moving cursors may be silent/reused streams or other units. Pair document/close provenance and qualified nonzero PCM evidence; do not infer sound or a live tab from process liveness.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path)
    p.add_argument('--after',type=float);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    text=a.log.read_text()
    if a.log.suffix=='.json':
        payload=json.loads(text)
        text=payload['silence/receipt.json']['stderr'] if 'silence/receipt.json' in payload else payload['stderr']
    result=analyze(text,a.after);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
