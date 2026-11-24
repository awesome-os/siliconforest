#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize a finite static-tab idle run; RSS counts shared pages repeatedly."""
import argparse,json
from pathlib import Path

def snapshot(sample):
    rows={}
    for line in sample['ps'].splitlines()[1:]:
        f=line.split(None,5)
        if len(f)>=6:
            seconds=sum(float(v)*60**i for i,v in enumerate(f[2].split(':')[::-1]))
            rows[int(f[0])]=dict(seconds=seconds,rss_kib=int(f[3]))
    return rows

def analyze(root,tabs):
    data=json.loads((root/'result.json').read_text());samples=data['samples']
    late=[s for s in samples if s['elapsed']>=samples[-1]['elapsed']-33]
    if len(late)<2:raise ValueError('Insufficient late idle samples')
    before,after=snapshot(late[0]),snapshot(late[-1]);wall=late[-1]['elapsed']-late[0]['elapsed']
    seconds=sum(max(0,v['seconds']-before.get(pid,dict(seconds=0))['seconds']) for pid,v in after.items())
    pages=json.loads((root/'pages.json').read_text());startup=json.loads((root/'runtime.json').read_text())['startup']
    static=all('SiliconForest static idle fixture' in p.get('body','') and '/idle-page.html' in p.get('url','') for p in pages)
    return dict(complete=data['measurement_passed'] and len(pages)==tabs and static,
        requested_tabs=tabs,observed_tabs=len(pages),static_local_pages_confirmed=static,
        idle_wall_seconds=wall,cpu_seconds=seconds,percent_one_core=seconds/wall*100,
        summed_rss_mib=sum(v['rss_kib'] for v in after.values())/1024,
        process_to_first_paint_seconds=(startup['firstPaint']-startup['process'])/1000,
        launcher_to_first_paint_seconds=(startup['firstPaint']-data['launcher_wall_start_ms'])/1000 if 'launcher_wall_start_ms' in data else None,
        method='Final33s snapshots of owned family; TIME delta as percent of one core; exited-before-final children omitted. Summed RSS repeats shared pages, not physical footprint. Browser firstPaint is not panel latency.')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('directory',type=Path);ap.add_argument('--tabs',type=int,required=True);a=ap.parse_args();r=analyze(a.directory,a.tabs)
    (a.directory/'idle-analysis.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));raise SystemExit(0 if r['complete'] else 1)
