# SPDX-License-Identifier: MIT
"""Interpret owned native export IDs; never infer compositor or panel delivery."""
import math
import re
from statistics import median
from frame_marker import decode_levels


def distribution(values):
    if not values:return None
    values=sorted(values)
    def percentile(f):
        point=(len(values)-1)*f;lo=math.floor(point);hi=math.ceil(point)
        return values[lo]+(values[hi]-values[lo])*(point-lo)
    return dict(count=len(values),minimum=values[0],p50=median(values),p95=percentile(.95),
                p99=percentile(.99),maximum=values[-1])


def analyze(text,parent):
    samples=[];foreign=0;record_bytes=0
    for line in text.splitlines():
        if not line.startswith('FXFRAMEID '):continue
        f=dict(re.findall(r'([A-Za-z0-9_]+)=(\S+)',line))
        if int(f['pid'])!=parent:foreign+=1;continue
        if f.get('log_buffered') not in [None,'1']:raise ValueError('Unknown native log buffering protocol')
        record_bytes+=len((line+'\n').encode())
        before=float(f['before_ms']);after=float(f['after_ms']);sequence=int(f['sequence'])
        previous_emit=float(f['previous_emit_ms']) if 'previous_emit_ms' in f else None
        if previous_emit is not None and (not math.isfinite(previous_emit) or previous_emit<0):
            raise ValueError('Invalid native previous-write cost')
        if not math.isfinite(before+after) or before<0 or after<before:raise ValueError('Invalid export clock interval')
        if samples and (sequence<=samples[-1]['sequence'] or before<samples[-1]['before_ms']):
            raise ValueError('Own export sequence/clock is not ordered')
        frame=int(f['frame']);error=int(f['gl_error']);size=int(f['readback_bytes']);surface=int(f['surface'])
        if not 0<size<=65536 or not 0<surface<2**32:raise ValueError('Invalid readback extent or surface identity')
        valid=not error and frame>=0
        if valid:
            if f['reason']!='ok':raise ValueError('Native frame/reason contradiction')
            levels=[int(x) for x in f['rows'].split(',')]
            if len(levels)!=36 or decode_levels([levels[:18],levels[18:]])!=frame:
                raise ValueError('Independent raw marker decoding disagrees with native frame')
        elif frame!=-1 or f['reason'] not in ['gl_error','marker_rejected']:
            raise ValueError('Invalid native rejection row')
        samples.append(dict(sequence=sequence,surface=surface,frame=frame,valid=valid,gl_error=error,
            before_ms=before,after_ms=after,readback_ms=after-before,readback_bytes=size,
            previous_emit_ms=previous_emit if sequence>0 else None,log_buffered=f.get('log_buffered')=='1'))
        if len(samples)>4096:raise ValueError('Unexpected own diagnostic sample count')
    if not samples:raise ValueError('No owned native frame-ID export rows')
    buffered=None
    if any(s['log_buffered'] for s in samples):
        if not all(s['log_buffered'] for s in samples):raise ValueError('Mixed live and buffered native rows')
        receipts=[]
        for line in text.splitlines():
            if not line.startswith('FXFRAMEID_BUFFER '):continue
            fields=dict(re.findall(r'([A-Za-z0-9_]+)=(\S+)',line))
            if int(fields.get('pid',0))==parent:receipts.append(fields)
        if len(receipts)!=1:raise ValueError('One complete owned deferred-log receipt required')
        fields=receipts[0]
        if (fields.get('explicit_flush')!='1' or int(fields['records'])!=len(samples) or int(fields['bytes'])!=record_bytes or
            int(fields['write_return'])!=record_bytes or int(fields['write_errno']) or int(fields['overflow'])):
            raise ValueError('Deferred native log incomplete, truncated or failed')
        costs={k:float(fields[k]) for k in ['write_ms','format_sum_ms','format_max_ms','hook_sum_ms','hook_max_ms']}
        if any(not math.isfinite(v) or v<0 for v in costs.values()):raise ValueError('Invalid deferred observer cost')
        buffered=dict(records=len(samples),bytes=record_bytes,complete=True,**costs,
            scope='Bounded RAM log explicitly flushed before teardown; excludes one frame-zero preview write before play. Hook cost ends before mutex unlock, includes formatting and buffer copy; final bulk write outside playback.')
    valid=[s for s in samples if s['valid']];positive=next((i for i,s in enumerate(valid) if s['frame']>0),None)
    start=None;end=None
    if positive is not None:
        zeros=[i for i,s in enumerate(valid[:positive]) if s['frame']==0]
        if zeros:start=zeros[-1]
        end=next((i for i,s in enumerate(valid) if i>=positive and s['frame']==899),None)
    active=valid[start:end+1] if start is not None and end is not None else []
    changed=[]
    for sample in active:
        if not changed or sample['frame']!=changed[-1]['frame']:changed.append(sample)
    observed={s['frame'] for s in active};elapsed=active[-1]['after_ms']-active[0]['after_ms'] if len(active)>1 else None
    changes=[b['after_ms']-a['after_ms'] for a,b in zip(changed,changed[1:])]
    return dict(scope='Private post-completion PARENT EXPORT images only; not completed compositor flips/panel fps',
        own_parent=parent,samples=len(samples),foreign_pid_rows=foreign,valid_samples=len(valid),
        rejected_marker_samples=sum(not s['valid'] and not s['gl_error'] for s in samples),
        gl_error_samples=sum(bool(s['gl_error']) for s in samples),
        identity_span_complete=bool(active),first_to_last_identity_span_ms=elapsed,
        distinct_source_ids=len(observed),missing_source_ids=sorted(set(range(900))-observed),
        all900_ids_observed=observed==set(range(900)),
        adjacent_repeats=len(active)-len(changed),regressions=sum(b['frame']<a['frame'] for a,b in zip(changed,changed[1:])),
        forward_skips=sum(max(0,b['frame']-a['frame']-1) for a,b in zip(changed,changed[1:])),
        diagnostic_distinct_id_hz=(len(observed)-1)*1000/elapsed if elapsed and len(observed)>1 else None,
        changed_id_interval_ms=distribution(changes),
        readback_cost_ms=distribution([s['readback_ms'] for s in samples]),
        previous_log_write_cost_ms_excluding_last=distribution([s['previous_emit_ms'] for s in samples if s['previous_emit_ms'] is not None]),
        buffered_log_receipt=buffered,
        diagnostic_readback_bytes=sum(s['readback_bytes'] for s in samples),
        active_export_samples=active,panel_delivery_proved=False,production_performance_qualified=False,
        limits='Raw36 levels independently decoded; native PID supplied by the admitted own fixture. Excludes preroll0 repeats and after-end899 repeats using the last0 before positive progress/first899. No JS/native clock bridge, actual-provider admission or completed-flip association is established by this parser. Readback phase and previous-write cost are separate; the last write and formatting cost are not measured by these fields. Discontinuities remain observations.')
