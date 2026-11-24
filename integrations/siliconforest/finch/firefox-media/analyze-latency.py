#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare captured HAL/AU timestamps with cubeb's reported-latency model.

This measures software clock reporting. HAL's audible-time estimate remains a
model, not an acoustic observation. No timing/provider setting is changed.
"""
import argparse,json,math,re
from pathlib import Path


def summary(values):
    values=sorted(values)
    if not values:return None
    def percentile(fraction):
        point=(len(values)-1)*fraction;left=math.floor(point);right=math.ceil(point)
        return values[left]+(values[right]-values[left])*(point-left)
    return dict(count=len(values),minimum=values[0],p50=percentile(.5),p95=percentile(.95),maximum=values[-1])


def analyze(text):
    records=[]
    for line in text.splitlines():
        if not line.startswith('LATENCY_'):continue
        values=dict(re.findall(r'([A-Za-z0-9_-]+)=(\S+)',line));values['kind']=line.split()[0];records.append(values)
    bases=[r for r in records if r['kind']=='LATENCY_TIMEBASE']
    if len(bases)!=1 or int(bases[0]['numer'])<=0 or int(bases[0]['denom'])<=0:raise ValueError('One valid actual timebase required')
    ns_per_tick=int(bases[0]['numer'])/int(bases[0]['denom'])
    if not any(r['kind']=='LATENCY_DONE' for r in records):raise ValueError('Probe did not reach normal completion')
    cases=[]
    for configuration in [r for r in records if r['kind']=='LATENCY_CASE']:
        fixed_ms=(int(configuration['device-latency'])+int(configuration['stream-latency']))/float(configuration['device-rate'])*1000+float(configuration['unit-latency'])*1000
        rows=[r for r in records if r['kind']=='LATENCY_ROW' and r['case']==configuration['case']]
        if not rows:raise ValueError('No actual callbacks in case')
        samples=[];nonfuture=0
        for row in rows:
            if not (int(row['hal-flags'])&2 and int(row['au-flags'])&2):raise ValueError('Host timestamp unavailable')
            now=int(row['now']);hal=(int(row['hal-host'])-now)*ns_per_tick/1e6
            if hal<=0:nonfuture+=1;continue
            au=(int(row['au-host'])-now)*ns_per_tick/1e6
            reported=fixed_ms+max(0,au)
            samples.append(dict(hal_lead_ms=hal,au_lead_ms=au,cubeb_model_ms=reported,
                                model_minus_hal_ms=reported-hal,cycle_callback=int(row['cycle-callback'])))
        cases.append(dict(configuration=configuration,reported_fixed_ms=fixed_ms,total_callbacks=len(rows),
                          nonfuture_hal_timestamps=nonfuture,future_samples=len(samples),
                          hal_lead_ms=summary([r['hal_lead_ms'] for r in samples]),
                          au_lead_ms=summary([r['au_lead_ms'] for r in samples]),
                          cubeb_model_ms=summary([r['cubeb_model_ms'] for r in samples]),
                          model_minus_hal_ms=summary([r['model_minus_hal_ms'] for r in samples])))
    if not cases:raise ValueError('No case configuration')
    return dict(cases=cases,acoustic_sync_proved=False,source='cubeb-coreaudio-rs 076ecf68 compute_output_latency/get_fixed_latency',
                limits='Own silent units, actual HAL/AU callback timestamps. HAL includes a source-modelled downstream pipeline; no DAC/lip-sync measurement. Nonfuture/startup timestamps are retained as counts. Positive model-minus-HAL means reported delay exceeds HAL prediction; no automatic timing patch.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('receipt',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    raw=json.loads(a.receipt.read_text());raw=raw.get('latency/receipt.json',raw)
    if raw.get('returncode')!=0 or raw.get('abort'):raise ValueError('Only normal completed probe can be analyzed')
    result=analyze(raw['stdout']);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
