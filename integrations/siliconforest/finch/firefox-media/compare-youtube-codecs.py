#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare one own-profile content-window pair. No automatic preference install."""
import argparse
import base64
import json
import math
from pathlib import Path
import re


def compare(stage):
    results=[json.loads((stage/(phase+'-admission.json')).read_text()) for phase in ['youtube1080','youtubehw']]
    a,b=results
    runs=[stage/('target-'+phase)/(phase+'-run') for phase in ['youtube1080','youtubehw']]
    launches=[json.loads((run/'cadence-launch.json').read_text()) for run in runs]
    raws=[json.loads((run/'browser/result.json').read_text()) for run in runs]
    def images(raw):
        return sorted((p['role'],match[1],match[2],match[3]) for p in raw['providers']['processes']
            for line in p['images'] if (match:=re.search(r'\s(\S+)\s+<([^>]+)>\s+(.+)$',line)))
    def media_images(raw):
        return sorted((p['type'],match[1],match[2],match[3]) for p in raw['streaming']['playback']['media_providers']['processes']
            for line in p['images'] if (match:=re.search(r'\s(\S+)\s+<([^>]+)>\s+(.+)$',line)))
    checks=dict(both_admitted=a['passed'] is True and b['passed'] is True,
        same_profile=bool(a.get('reused_media_profile')) and a['reused_media_profile']==b.get('reused_media_profile'),
        same_manifest=launches[0]['release']==launches[1]['release'],same_graphics_images=images(raws[0])==images(raws[1]),
        same_media_images=media_images(raws[0])==media_images(raws[1]),
        same_kernel=bool(launches[0].get('kernel_uuid')) and launches[0].get('kernel_uuid')==launches[1].get('kernel_uuid'),
        exact_dimensions=a['dimensions']==b['dimensions']==[1920,1080],
        private_preference_only=a.get('private_av1_disabled') is False and b.get('private_av1_disabled') is True,
        same_content_window=abs(a['start_media']-1)<=.05 and abs(b['start_media']-1)<=.05 and
            abs(a['start_media']-b['start_media'])<=.05 and abs(a['end_media']-b['end_media'])<=.10)
    formats=[r.get('selected_format_after',{}).get('selectedFormat') or {} for r in results]
    checks['source_60fps']=all(f.get('fps')==60 for f in formats)
    color_keys=['primaries','transferCharacteristics','matrixCoefficients']
    colors=[{key:f.get('colorInfo',{}).get(key) for key in color_keys} for f in formats]
    checks['same_known_color_metadata']=colors[0]==colors[1] and all(colors[0].values())
    samples=[r.get('diagnostic_pixels') or {} for r in results];psnr=None;identical=False
    checks['same_sample_time']=all(not s.get('error') and abs(s.get('mediaTime',-1)-10)<=.001 and
        (s.get('width'),s.get('height'))==(128,72) and (s.get('videoWidth'),s.get('videoHeight'))==(1920,1080) for s in samples)
    if checks['same_sample_time']:
        pixels=[base64.b64decode(s['rgbaBase64'],validate=True) for s in samples]
        checks['sample_sizes']=all(len(p)==128*72*4 for p in pixels)
        if checks['sample_sizes']:
            rgb=[[v for index,v in enumerate(p) if index%4!=3] for p in pixels]
            error=sum((x-y)**2 for x,y in zip(*rgb))/len(rgb[0]);identical=error==0
            psnr=10*math.log10(255**2/error) if error else None
            checks['diagnostic_pixel_similarity']=identical or psnr>=35
    else:checks['diagnostic_pixel_similarity']=False
    cpu=[r.get('owned_cpu') or {} for r in results]
    checks['cpu_coverage']=all(c.get('family_complete') is True and c.get('rdd_counter_present') is True for c in cpu)
    checks['lower_rdd_cpu']=cpu[1].get('percent_one_core_by_type',{}).get('rdd',math.inf)<cpu[0].get('percent_one_core_by_type',{}).get('rdd',-1)
    checks['lower_family_cpu']=cpu[1].get('matched_family_percent_one_core',math.inf)<cpu[0].get('matched_family_percent_one_core',-1)
    return dict(passed=all(checks.values()),checks=checks,formats=formats,colors=colors,
        cpu=[{k:c.get(k) for k in ['percent_one_core_by_type','matched_family_percent_one_core','family_complete']} for c in cpu],
        diagnostic_pixels=dict(width=128,height=72,time_seconds=10,identical=identical,psnr_db=psnr),
        visual_review='REQUIRED: inspect both actual renderer captures before any integration decision',
        installed_preference_changed=False,
        limits='One pair, same own profile and negotiated resolution/frame rate/color metadata. Small diagnostic samples are not lossless equality or complete visual-quality proof. Network/site/cache variation remains; neither panel cadence nor general AV1-policy suitability follows. No preference is installed by this tool.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=compare(a.stage);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:r[k] for k in ['passed','checks','formats','cpu','diagnostic_pixels']}))
    raise SystemExit(0 if r['passed'] else 1)
