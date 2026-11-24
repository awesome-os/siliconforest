#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare actual renderer regions for hardware and software WebRender runs."""
import argparse,json
from pathlib import Path
from PIL import Image,ImageChops

def compare(hardware,software):
    metadata=[json.loads((p/'scene-reference.json').read_text()) for p in [hardware,software]]
    images=[Image.open(p/'scene-reference.ppm').convert('RGB') for p in [hardware,software]]
    rows=[]
    for name in ['gl','motion']:
        crops=[]
        for image,data in zip(images,metadata):
            box=data['boxes'][name];x,y,w,h=[box[k] for k in ['x','y','width','height']]
            crops.append(image.crop((round(x),round(y),round(x+w),round(y+h))))
        if crops[0].size!=crops[1].size:raise ValueError('Reference sizes differ: '+name)
        raw=ImageChops.difference(*crops).tobytes();pixels=len(raw)//3
        rows.append(dict(region=name,size=crops[0].size,pixels=pixels,
            exact=sum(raw[i:i+3]==b'\0\0\0' for i in range(0,len(raw),3)),
            beyond_one=sum(any(v>1 for v in raw[i:i+3]) for i in range(0,len(raw),3)),
            max_channel_error=max(raw),mean_channel_error=sum(raw)/len(raw)))
    modes=[]
    for p in [hardware,software]:
        data=json.loads((p/'support.json').read_text())['graphics']
        modes.append(dict(layer_manager=data.get('windowLayerManagerType'),info=data.get('info')))
    sw=modes[1];value=(sw['info'] or {}).get('WebRenderSoftware')
    confirmed=('software' in str(sw['layer_manager']).lower() or str(sw['layer_manager']).upper()=='SWGL'
               or value is True or value==1 or str(value).lower()=='true')
    calibrated=all(data.get('coordinate_calibration',{}).get('complete') for data in metadata)
    return dict(complete=all(r['beyond_one']==0 for r in rows) and confirmed and calibrated,
        coordinate_calibration_confirmed=calibrated,
        software_mode_confirmed=confirmed,modes=modes,regions=rows,
        method='Actual source=renderer RGB crops at frozen CSS animation time0; WebGL interior excludes border. Report exact counts and all errors; at most one channel value rounding accepted. Mode must independently confirm software WebRender.')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('hardware',type=Path);ap.add_argument('software',type=Path);a=ap.parse_args()
    result=compare(a.hardware,a.software);(a.hardware/'software-reference-comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));raise SystemExit(0 if result['complete'] else 1)
