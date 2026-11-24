#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Count painted text pixels in actual compositor captures, never DOM readbacks.

Paint-only transparent-text controls preserve layout. Moving images, occlusion
and live page changes can contaminate differences: visual review remains
required. This classifier cannot turn a challenge/loading page into site proof.
"""
import argparse
import json
import math
from pathlib import Path
import re


def ppm(path):
    data=path.read_bytes();header=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
    if not header:raise ValueError('Expected source=renderer P6 capture')
    w,h=map(int,header.groups());pixels=data[header.end():]
    if len(pixels)!=w*h*3:raise ValueError('Truncated renderer capture')
    source=json.loads(path.with_suffix('.source.json').read_text())
    if source.get('source')!='renderer':raise ValueError('Unverified capture source')
    return w,h,pixels


def analyze(directory,manifest="legibility.json"):
    rows=json.loads((directory/manifest).read_text());results=[]
    for row in rows:
        calibration=row['calibration']
        if not calibration.get('complete'):raise ValueError('DOM-to-renderer calibration absent')
        w,h,a=ppm(directory/row['renderer_images']['visible']);w2,h2,b=ppm(directory/row['renderer_images']['text_hidden'])
        if (w,h)!=(w2,h2):raise ValueError('Renderer geometry changed')
        restored=row['renderer_images'].get('restored')
        if restored is None and row.get('renderer',{}).get('path'):
            restored=Path(row['renderer']['path']).name
        restored_pixels=None
        if restored is not None:
            rw,rh,restored_pixels=ppm(directory/restored)
            if (rw,rh)!=(w,h):raise ValueError('Renderer geometry changed after text restoration')
        scale=calibration['scale'];ox,oy=calibration['origin'];runs=[];han=[];combined=set()
        def points_for(boxes, inset=False):
            points=set()
            for box in boxes:
                # Avoid counting a neighbouring glyph's overhang as this glyph.
                dx=min(1,box['width']*.05) if inset else 0
                x0=max(0,math.floor(ox+(box['x']+dx)*scale));y0=max(0,math.floor(oy+box['y']*scale))
                x1=min(w,math.ceil(ox+(box['x']+box['width']-dx)*scale));y1=min(h,math.ceil(oy+(box['y']+box['height'])*scale))
                points.update(y*w+x for y in range(y0,y1) for x in range(x0,x1))
            return points
        def ink_record(text, font, points):
            ink_points=[p for p in points if any(abs(a[p*3+c]-b[p*3+c])>8 for c in range(3))]
            changed=len(ink_points)
            matched=(sum(all(abs(a[p*3+c]-restored_pixels[p*3+c])<=12 for c in range(3))
                         for p in ink_points) if restored_pixels is not None else None)
            return dict(text=text,font=font,area=len(points),changed_pixels=changed,
                             ink_observed=changed>=max(3,len(points)*.002),
                             restored_match_ratio=matched/changed if matched is not None and changed else None)
        for run in row['runs']:
            points=points_for(run['boxes']);combined.update(points)
            runs.append(ink_record(run['text'],run['font'],points))
            for sample in run.get('han_samples',[]):
                points=points_for(sample['boxes'],inset=True)
                if len(points)>=20:han.append(ink_record(sample['text'],run['font'],points))
        changed=sum(any(abs(a[p*3+c]-b[p*3+c])>8 for c in range(3)) for p in combined)
        eligible=[run for run in runs if len(run['text'].strip())>=3 and run['area']>=20]
        painted=sum(run['ink_observed'] for run in eligible)
        ink_pass=bool(eligible) and painted==len(eligible)
        restore_pass=(bool(eligible) and all(run['restored_match_ratio'] is not None and
                      run['restored_match_ratio']>=.95 for run in eligible)
                      if restored_pixels is not None else None)
        han_pass=(all(s['ink_observed'] and (restored_pixels is None or
                  s['restored_match_ratio'] is not None and s['restored_match_ratio']>=.95)
                  for s in han) if han else None)
        results.append(dict(url=row['url'],renderer_text_area=len(combined),changed_text_pixels=changed,
            coverage=changed/len(combined) if combined else None,eligible_runs=len(eligible),runs_with_ink=painted,
            ink_check_pass=ink_pass,restore_check_pass=restore_pass,
            paint_check_pass=ink_pass and restore_pass is not False and han_pass is not False,runs=runs,
            han_sampling_observed=bool(row.get('han_sampling')),han_measured_characters=len(han),
            han_characters_with_ink=sum(s['ink_observed'] for s in han),han_check_pass=han_pass,han_samples=han,
            legibility_qualified=False,visual_review_required=True,
            method='Actual renderer visible/text-hidden/restored captures and calibrated DOM text rectangles; every eligible run and observed individual Han range requires ink and, when captured, at least95percent restored glyph pixels within12/255 of the original. Han sampling is bounded to256UTF16units per observed text node; no universal per-glyph guarantee. Live changes/occlusion can contaminate: human image review required.'))
    (directory/(Path(manifest).stem+'-text-pixel-coverage.json')).write_text(json.dumps(results,indent=2)+'\n')
    return results


