#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Locate our expected marker in an actual renderer image; no GL geometry claim."""
import argparse
import json
from pathlib import Path
from PIL import Image
import frame_marker as marker


def locate(image,calibration,expected):
    capture=calibration.get('renderer_capture',{})
    if capture.get('source')!='renderer' or capture.get('input_injected') is not False:
        raise ValueError('Actual capture-only renderer admission required')
    width,height=image.size;scale=calibration['rect']['width']*calibration['devicePixelRatio']/1920
    if not .075<=scale<=2:raise ValueError('Unsupported marker scale')
    data=image.convert('L').tobytes();step=max(1,round(4*scale))
    dx=[round((96+column*96+40)*scale) for column in range(18)]
    dy=[round(y*scale) for y in [120,232]];points=[]
    for y in range(-dy[0],height-dy[1],step):
        line=(y+dy[0])*width
        for x in range(1-dx[0],width-dx[-1]-2,step):
            if any((data[line+x+dx[c]]>180)!=bool(value) or 80<=data[line+x+dx[c]]<=180
                   for c,value in enumerate(marker.PREFIX)):continue
            rows=[[sum(data[(y+d)*width+x+column+offset] for offset in [-1,0,1])/3
                   for column in dx] for d in dy]
            try:frame=marker.decode_levels(rows)
            except ValueError:continue
            if frame==expected:points.append((x,y))
    if not points:raise ValueError('No expected marker found at the admitted scale')
    xs,ys=zip(*points);bounds=[min(xs),min(ys),max(xs),max(ys)]
    if bounds[2]-bounds[0]>80*scale+step*2 or bounds[3]-bounds[1]>80*scale+step*2:
        raise ValueError('Multiple spatially separated matches; no unique marker admission')
    x=(bounds[0]+bounds[2])/2;y=(bounds[1]+bounds[3])/2
    return dict(frame_id=expected,scale=scale,matching_origins=len(points),origin_plateau_bounds=bounds,
        video_screen_rect_estimate=dict(x=x,y=y,width=1920*scale,height=1080*scale),
        marker_sample_rows_screen=dict(x=round(x+88*scale),width=round(1728*scale),
            y0=round(y+120*scale),y1=round(y+232*scale)),
        input_injected=False,panel_presentation_proved=False,
        limits='Actual image marker at the admitted video scale. Screen coordinates are not CGL framebuffer coordinates; admit backing dimensions, origin and orientation separately. This is a static image, not frame cadence.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,required=True);p.add_argument('--calibration',type=Path,required=True)
    p.add_argument('--expected',type=int,default=0);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    result=locate(Image.open(a.image),json.loads(a.calibration.read_text()),a.expected)
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
