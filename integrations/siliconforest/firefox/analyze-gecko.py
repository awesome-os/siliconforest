#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize retained Gecko samples inside the actual fixture interval.

Optional public Breakpad symbols must match the recorded XUL debug ID. Never
upload a profile or replace its original addresses. CPU attribution is sampled,
not exact instrumentation; inclusive weights cannot be added across frames.
"""
import argparse
from bisect import bisect_left,bisect_right
from collections import Counter
import json
from pathlib import Path
import re
import subprocess


def processes(profile):
    yield profile
    for child in profile.get('processes',[]):yield from processes(child)


def cell(row,schema,key):
    index=schema.get(key)
    return row[index] if index is not None and index<len(row) else None


def functions(path,offsets):
    wanted=sorted(offsets);result={};count=0
    with path.open(errors='replace') as stream:
        header=stream.readline().strip().split()
        if len(header)!=5 or header[0]!='MODULE' or header[-1]!='XUL':
            raise ValueError('Expected Mozilla XUL Breakpad MODULE header')
        for line in stream:
            if not line.startswith('FUNC '):continue
            parts=line.rstrip('\n').split(' ',5 if line.startswith('FUNC m ') else 4)
            start_index=2 if parts[1]=='m' else 1
            if len(parts)<=start_index+3:continue
            start=int(parts[start_index],16);end=start+int(parts[start_index+1],16)
            name=parts[start_index+3];count+=1
            for offset in wanted[bisect_left(wanted,start):bisect_left(wanted,end)]:result[offset]=name
    return header[3],result,count


def native_symbols(manifest):
    """Use text symbols only after independently matching Mach-O UUIDs."""
    import hashlib
    import struct
    result={}
    for entry in json.loads(manifest.read_text()):
        path=Path(entry['path']);data=path.read_bytes()
        if hashlib.sha256(data).hexdigest()!=entry['sha256']:raise ValueError('Native image hash changed')
        if struct.unpack_from('<I',data)[0]!=0xfeedfacf:raise ValueError('Expected thin Mach-O image')
        offset=32;uuid=None
        for _ in range(struct.unpack_from('<I',data,16)[0]):
            command,size=struct.unpack_from('<II',data,offset)
            if command==0x1b:uuid=data[offset+8:offset+24].hex().upper()+'0'
            offset+=size
        if uuid!=entry['debug_id']:raise ValueError('Native image UUID mismatch')
        output=subprocess.run(['llvm-nm','--numeric-sort','--defined-only',str(path)],capture_output=True,text=True,check=True).stdout
        values=[]
        for line in output.splitlines():
            row=line.split(None,2)
            if len(row)==3 and row[1] in ['T','t']:values.append((int(row[0],16),row[2]))
        values.sort();result[uuid]=([value for value,_ in values],values)
    return result


def analyze(directory,symbols=None,native_manifest=None):
    if (directory/'qualification-invalidated.json').exists():
        raise ValueError('Run was invalidated; preserve raw artifacts and repeat it')
    run=json.loads((directory/'result.json').read_text())
    if 'fixture_measurement' in run:
        fixture=run['fixture_measurement'];start=fixture['timeOrigin']+fixture['wallStart'];end=fixture['timeOrigin']+fixture['wallEnd']
    else:
        start,end=run['profile_interval']
        if not start or not end or end<=start:raise ValueError('Incomplete benchmark profiling interval')
    profile=json.loads((directory/'pipeline-gecko-profile.json').read_text())
    units=profile['meta'].get('sampleUnits',{}).get('threadCPUDelta')
    if units not in ['µs','us']:raise ValueError('Unrecognized thread CPU units: '+str(units))
    offsets=set();libraries={}
    for process in processes(profile):
        libs=sorted(process.get('libs',[]),key=lambda row:row['start']);starts=[row['start'] for row in libs]
        libraries[id(process)]=(libs,starts)
        for thread in process.get('threads',[]):
            strings=thread['stringTable'];table=thread['frameTable']
            for row in table['data']:
                label=strings[cell(row,table['schema'],'location')]
                if not re.fullmatch('0x[0-9a-fA-F]+',label):continue
                address=int(label,16);i=bisect_right(starts,address)-1
                if i>=0 and libs[i]['name']=='XUL' and address<libs[i]['end']:
                    offsets.add(address-libs[i]['start']+libs[i].get('offset',0))
    symbol_id=None;names={};function_count=0
    if symbols:symbol_id,names,function_count=functions(symbols,offsets)
    native=native_symbols(native_manifest) if native_manifest else {}
    rows=[]
    for process in processes(profile):
        libs,starts=libraries[id(process)];base=process['meta']['startTime']
        def resolve(label):
            if not re.fullmatch('0x[0-9a-fA-F]+',label):return label
            address=int(label,16);i=bisect_right(starts,address)-1
            if i<0 or address>=libs[i]['end']:return label
            lib=libs[i];offset=address-lib['start']+lib.get('offset',0)
            if lib['name']=='XUL' and lib.get('breakpadId')==symbol_id and offset in names:
                return 'XUL: '+names[offset]
            if lib.get('breakpadId') in native:
                addresses,values=native[lib['breakpadId']];index=bisect_right(addresses,offset)-1
                if index>=0:
                    address,name=values[index]
                    return lib['name']+': '+name+'+0x'+format(offset-address,'x')
            return lib['name']+'+0x'+format(offset,'x')
        for thread in process.get('threads',[]):
            strings=thread['stringTable'];ft=thread['frameTable'];st=thread['stackTable'];samples=thread['samples']
            labels=[resolve(strings[cell(row,ft['schema'],'location')]) for row in ft['data']]
            chains={None:[]}
            def chain(index):
                if index in chains:return chains[index]
                pending=[];seen=set();current=index
                while current not in chains:
                    if current in seen or len(pending)>1024:raise ValueError('Invalid Gecko stack chain')
                    seen.add(current);row=st['data'][current];pending.append((current,cell(row,st['schema'],'frame')))
                    current=cell(row,st['schema'],'prefix')
                value=chains[current]
                for current,frame in reversed(pending):
                    value=value+[labels[frame]];chains[current]=value
                return chains[index]
            leaf_samples=Counter();leaf_cpu=Counter();inclusive=Counter();hot_stacks=Counter();cpu_us=0;sample_count=0;cpu_rows=0;event_delays=[]
            for sample in samples['data']:
                at=cell(sample,samples['schema'],'time')
                if at is None or not start<=base+at<=end:continue
                stack=chain(cell(sample,samples['schema'],'stack'))
                sample_count+=1
                if stack:leaf_samples[stack[-1]]+=1
                delay=cell(sample,samples['schema'],'eventDelay')
                if delay is not None:event_delays.append(delay)
                weight=cell(sample,samples['schema'],'threadCPUDelta')
                if weight is None:continue
                if weight<0:raise ValueError('Negative thread CPU delta')
                cpu_rows+=1;cpu_us+=weight
                if stack:
                    leaf_cpu[stack[-1]]+=weight
                    hot_stacks[tuple(stack[-16:])]+=weight
                for label in set(stack):inclusive[label]+=weight
            if not sample_count:continue
            qos=Counter();markers=Counter();marker_durations=Counter();mt=thread['markers']
            for marker in mt['data']:
                at=cell(marker,mt['schema'],'startTime')
                if at is None or not start<=base+at<=end:continue
                name=strings[cell(marker,mt['schema'],'name')];markers[name]+=1
                stop=cell(marker,mt['schema'],'endTime')
                if stop is not None and stop>=at:marker_durations[name]+=stop-at
                payload=cell(marker,mt['schema'],'data')
                if isinstance(payload,dict) and 'QoS' in payload:qos[str(payload['QoS'])]+=1
            event_delays.sort()
            rows.append(dict(pid=thread.get('pid'),tid=thread.get('tid'),thread=thread['name'],process_type=thread.get('processType'),
                samples=sample_count,cpu_rows=cpu_rows,cpu_seconds=cpu_us/1e6,
                percent_one_core=cpu_us/((end-start)*1000)*100,
                event_delay_p95_ms=event_delays[int((len(event_delays)-1)*.95)] if event_delays else None,
                leaf_samples=leaf_samples.most_common(12),leaf_cpu_us=leaf_cpu.most_common(20),
                inclusive_cpu_us=inclusive.most_common(25),
                hot_stacks_cpu_us=[dict(stack=list(stack),cpu_us=weight) for stack,weight in hot_stacks.most_common(12)],
                markers=markers.most_common(15),
                marker_duration_ms=marker_durations.most_common(12),gecko_requested_qos_markers=dict(qos)))
    rows.sort(key=lambda row:row['cpu_seconds'],reverse=True)
    return dict(fixture_epoch_ms=[start,end],diagnostic_only=True,timing_qualification=False,
        cpu_delta_units=units,xul_symbols=dict(path=str(symbols) if symbols else None,debug_id=symbol_id,
            functions=function_count,requested_offsets=len(offsets),matched_offsets=len(names)),
        native_image_debug_ids=list(native),
        threads=rows,method='Samples whose per-process epoch lies inside the fixture interval. Thread CPU deltas are microseconds; leaf/inclusive attribution is statistical and inclusive weights overlap. Gecko Awake QoS markers describe Gecko requests, not independently measured kernel policy. Public symbols apply only to matching recorded XUL debug IDs. Original profiles remain untouched.')


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('directory',type=Path);ap.add_argument('--xul-symbols',type=Path);ap.add_argument('--native-images',type=Path);args=ap.parse_args()
    result=analyze(args.directory,args.xul_symbols,args.native_images)
    (args.directory/'pipeline-gecko-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(threads=len(result['threads']),xul_symbols=result['xul_symbols'],
        hottest=[{key:row[key] for key in ['pid','thread','process_type','cpu_seconds','percent_one_core','leaf_cpu_us']} for row in result['threads'][:5]])))
