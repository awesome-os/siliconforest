# SPDX-License-Identifier: MIT
"""Calibrate DOM coordinates against two opaque markers in a real P6 capture."""
import re


def calibrate(path, anchors, scale):
    data=path.read_bytes()
    header=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
    if not header:
        raise ValueError('Expected actual renderer P6 RGB capture')
    width,height=map(int,header.groups());pixels=data[header.end():]
    if len(pixels)!=width*height*3:
        raise ValueError('Unexpected renderer capture length')
    rows=[]
    for anchor in anchors:
        color=bytes(anchor['color']);offset=0;points=[]
        while True:
            found=pixels.find(color,offset)
            if found<0:break
            offset=found+1
            if found%3==0:points.append(((found//3)%width,(found//3)//width))
        if not points:raise ValueError('Opaque DOM calibration marker absent')
        xs,ys=zip(*points);x,y=min(xs),min(ys);w,h=max(xs)-x+1,max(ys)-y+1
        expected=anchor['width']*scale
        if abs(w-expected)>2 or abs(h-expected)>2 or len(points)<w*h*.8:
            raise ValueError('Calibration marker is clipped, ambiguous or not solid')
        rows.append(dict(dom=anchor,pixel_box=[x,y,w,h],pixels=len(points),
                         center=[x+w/2,y+h/2]))
    if len(rows)!=2:raise ValueError('Two independent DOM markers required')
    a,b=rows
    measured=[(b['center'][axis]-a['center'][axis])/
              (b['dom'][key]-a['dom'][key]) for axis,key in enumerate(['x','y'])]
    if any(abs(value-scale)>.02 for value in measured):
        raise ValueError('Measured DOM-to-screen scale disagrees with devicePixelRatio')
    origin=[a['center'][axis]-(a['dom'][key]+a['dom']['width']/2)*scale
            for axis,key in enumerate(['x','y'])]
    return dict(complete=True,origin=origin,scale=scale,measured_scale=measured,
                markers=rows,method='Opaque DOM marker pixel centers in actual source=renderer capture; independent of mozInnerScreenX/Y')
